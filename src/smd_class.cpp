// Created by RED on 03.12.2025.
#include "classes/smd_class.h"

#include <sstream>


PyTypeObject *SMDModelType = nullptr;
PyTypeObject *SMDNodeType = nullptr;
PyTypeObject *SMDSkeletonType = nullptr;
PyTypeObject *SMDBoneDefType = nullptr;
PyTypeObject *SMDVertexWeightType = nullptr;
PyTypeObject *SMDVertexType = nullptr;
PyTypeObject *SMDTriangleType = nullptr;
PyTypeObject *SMDVAKeyVertexType = nullptr;


typedef void (*freefunc)(void *);

// Helper to free an instance allocated with PyType_GenericAlloc
static void generic_free(PyObject *self) {
    // limited API friendly: use PyObject_Type + PyType_GetSlot
    PyObject *type_obj = PyObject_Type(self); // new ref
    if (!type_obj) {
        // If this somehow fails, last resort:
        PyObject_Free(self);
        return;
    }
    PyTypeObject *tp = (PyTypeObject *) type_obj;
    freefunc f = (freefunc) PyType_GetSlot(tp, Py_tp_free);
    if (f) {
        f(self);
    } else {
        // Fallback: generic free
        PyObject_Free(self);
    }
    Py_DECREF(type_obj);
}

namespace smd::model {
    bool ensure_data(SMDModel *self) {
        if (!self->data) {
            PyErr_SetString(PyExc_RuntimeError, "SMDModel data is not initialized");
            return false;
        }
        return true;
    }

#define ENSURE_DATA(self)  \
    if(!ensure_data(self)) { \
        return nullptr;      \
    }

    PyObject * get_version(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromLong(self->data->version);
    }

    PyObject *get_node_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromUnsignedLong(self->data->nodes.size());
    }

    PyObject *get_frame_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromUnsignedLong(self->data->skeleton.frames.size());
    }

    PyObject *get_triangle_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromUnsignedLong(self->data->triangles.size());
    }

    PyObject *get_nodes(SMDModel *self, void *) {
        ENSURE_DATA(self)
        const auto &nodes = self->data->nodes;

        Py_ssize_t n = (Py_ssize_t) nodes.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *obj = node::create(self, &nodes[i]);

            if (!obj) {
                Py_DECREF(list);
                return nullptr;
            }
            if (PyList_SetItem(list, i, obj) < 0) {
                Py_DECREF(obj);
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    bool parse_smd(SMDData *data, const std::string &input);


    PyObject *get_skeleton(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return skeleton::create(self, &self->data->skeleton);
    }

    PyObject * get_triangles(SMDModel *self, void *) {
        ENSURE_DATA(self)
        const auto &triangles = self->data->triangles;

        Py_ssize_t n = (Py_ssize_t) triangles.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *obj = triangle::create(self, &triangles[i]);

            if (!obj) {
                Py_DECREF(list);
                return nullptr;
            }
            if (PyList_SetItem(list, i, obj) < 0) {
                Py_DECREF(obj);
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    PyObject *smd_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
        // First arg should be PyBytes or PyString
        if (PyTuple_Size(args) != 1) {
            PyErr_SetString(PyExc_TypeError,
                            "SMDModel(input: bytes|str) takes exactly 1 argument");
            return nullptr;
        }
        PyObject *input = PyTuple_GetItem(args, 0);
        // Check that type is str or bytes
        if (!PyBytes_Check(input) && !PyUnicode_Check(input)) {
            PyErr_SetString(PyExc_TypeError, "input must be bytes or str");
            return nullptr;
        }

        SMDModel *self = (SMDModel *) PyType_GenericNew(type, nullptr, nullptr);
        if (!self) return nullptr;
        self->data = new SMDData();

        if (PyBytes_Check(input)) {
            char *input_data = PyBytes_AsString(input);
            Py_ssize_t size = PyBytes_Size(input);
            const std::string tmp(input_data, size);
            parse_smd(self->data, tmp);
        } else if (PyUnicode_Check(input)) {
            Py_ssize_t size;
            const char *input_data = PyUnicode_AsUTF8AndSize(input, &size);
            const std::string tmp(input_data, size);
            parse_smd(self->data, tmp);
        }

        return (PyObject *) self;
    }

    void dealloc(SMDModel *self) {
        delete self->data;
        freefunc(PyType_GetSlot(Py_TYPE((PyObject*)self), Py_tp_free))(self);
    }

    PyObject *repr(SMDModel *self) {
        return PyUnicode_FromFormat("<SMDModel nodes=%zu frames=%zu triangles=%zu>",
                                    self->data->nodes.size(),
                                    self->data->skeleton.frames.size(),
                                    self->data->triangles.size());
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *py_int = (PyObject *) &PyLong_Type;
        Py_INCREF(py_int);
        Py_INCREF((PyObject*)SMDSkeletonType);
        PyDict_SetItemString(ann, "version", py_int);
        PyDict_SetItemString(ann, "node_count", py_int);
        PyDict_SetItemString(ann, "frame_count", py_int);
        PyDict_SetItemString(ann, "triangle_count", py_int);
        PyDict_SetItemString(ann, "skeleton", (PyObject *) SMDSkeletonType);
        Py_DECREF(SMDSkeletonType);
        Py_DECREF(py_int);

        PyObject *str_ann = PyUnicode_FromString("list[SMDNode]");
        if (!str_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "nodes", str_ann) < 0) {
            Py_DECREF(str_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(str_ann);

        str_ann = PyUnicode_FromString("list[SMDTriangle]");
        if (!str_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "triangles", str_ann) < 0) {
            Py_DECREF(str_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(str_ann);

        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }


    bool parse_smd(SMDData *data, const std::string &input) {
        Parser parser(input);
        if (!parser.parse(data)) {
            PyErr_SetString(PyExc_ValueError, parser.errorMessage().c_str());
            return false;
        }
        return true;
    }
}

namespace smd::node {
    PyObject *get_id(SMDNode *self, void *) {
        return PyLong_FromLong(self->data->id);
    }

    PyObject *get_name(SMDNode *self, void *) {
        return PyUnicode_FromString(self->data->name.c_str());
    }

    PyObject *get_parent(SMDNode *self, void *) {
        return PyLong_FromLong(self->data->parent);
    }

    void dealloc(SMDNode *self) {
        Py_XDECREF((PyObject*)self->owner);
        generic_free((PyObject *) self);
    }

    PyObject *repr(SMDNode *self) {
        return PyUnicode_FromFormat("<SMDNode id=%d name='%s' parent_id=%d>",
                                    self->data->id,
                                    self->data->name.c_str(),
                                    self->data->parent);
    }

    PyObject *create(const model::SMDModel *owner, const Node *data) {
        SMDNode *node = (SMDNode *) PyType_GenericAlloc(SMDNodeType, 0);
        if (!node) return nullptr;
        Py_INCREF((PyObject*)owner);
        node->owner = owner;
        node->data = data;
        return (PyObject *) node;
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *py_int = (PyObject *) &PyLong_Type;
        PyObject *py_str = (PyObject *) &PyUnicode_Type;
        Py_INCREF(py_int);
        Py_INCREF(py_str);
        PyDict_SetItemString(ann, "id", py_int);
        PyDict_SetItemString(ann, "name", py_str);
        PyDict_SetItemString(ann, "parent", py_int);
        Py_DECREF(py_int);
        Py_DECREF(py_str);

        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }
}

namespace smd::skeleton {
    PyObject *get_frame_count(SMDSkeleton *self, void *) {
        return PyLong_FromUnsignedLong(self->data->frames.size());
    }

    PyObject *get_frames(SMDSkeleton *self, void *) {
        // Return must be dict[int, list[SMDBoneDef]]
        PyObject *dict = PyDict_New();
        if (!dict) return nullptr;
        for (const auto &frame_pair: self->data->frames) {
            int time = frame_pair.first;
            const auto &bone_defs = frame_pair.second;

            Py_ssize_t n = (Py_ssize_t) bone_defs.size();
            PyObject *list = PyList_New(n);
            if (!list) {
                Py_DECREF(dict);
                return nullptr;
            }

            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *obj = bonedef::create(self, &bone_defs[i]);
                if (!obj) {
                    Py_DECREF(list);
                    Py_DECREF(dict);
                    return nullptr;
                }
                if (PyList_SetItem(list, i, obj) < 0) {
                    Py_DECREF(obj);
                    Py_DECREF(list);
                    Py_DECREF(dict);
                    return nullptr;
                }
            }

            PyObject *py_time = PyLong_FromLong(time);
            if (!py_time) {
                Py_DECREF(list);
                Py_DECREF(dict);
                return nullptr;
            }
            if (PyDict_SetItem(dict, py_time, list) < 0) {
                Py_DECREF(py_time);
                Py_DECREF(list);
                Py_DECREF(dict);
                return nullptr;
            }
            Py_DECREF(py_time);
            Py_DECREF(list);
        }
        return dict;
    }

    void dealloc(SMDSkeleton *self) {
        Py_XDECREF((PyObject*)self->owner);
        generic_free((PyObject *) self);
    }

    PyObject *repr(SMDSkeleton *self) {
        return PyUnicode_FromFormat("<SMDSkeleton frames=%zu>",
                                    self->data->frames.size());
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *py_int = (PyObject *) &PyLong_Type;
        Py_INCREF(py_int);
        PyDict_SetItemString(ann, "frame_count", py_int);
        Py_DECREF(py_int);

        PyObject *nodes_ann = PyUnicode_FromString("dict[int,list[SMDBoneDef]]");
        if (!nodes_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "frames", nodes_ann) < 0) {
            Py_DECREF(nodes_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(nodes_ann);


        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }

    PyObject *create(const model::SMDModel *owner, const Skeleton *data) {
        SMDSkeleton *skeleton = (SMDSkeleton *) PyType_GenericAlloc(SMDSkeletonType, 0);
        if (!skeleton) return nullptr;
        Py_INCREF((PyObject*)owner);
        skeleton->owner = owner;
        skeleton->data = data;
        return (PyObject *) skeleton;
    }
}

namespace smd::bonedef {
    PyObject *get_bone(SMDBoneDef *self, void *) {
        return PyLong_FromLong(self->data->bone);
    }

    PyObject *get_pos(SMDBoneDef *self, void *) {
        return Py_BuildValue("(fff)",
                             self->data->pos[0],
                             self->data->pos[1],
                             self->data->pos[2]);
    }

    PyObject *get_rot(SMDBoneDef *self, void *) {
        return Py_BuildValue("(fff)",
                             self->data->rot[0],
                             self->data->rot[1],
                             self->data->rot[2]);
    }

    void dealloc(SMDBoneDef *self) {
        Py_XDECREF((PyObject*)self->owner);
        generic_free((PyObject *) self);
    }

    PyObject *repr(SMDBoneDef *self) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "<SMDBoneDef bone=%d pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f)>",
                      self->data->bone,
                      self->data->pos[0], self->data->pos[1], self->data->pos[2],
                      self->data->rot[0], self->data->rot[1], self->data->rot[2]
        );

        return PyUnicode_FromString(buf);
    }

    PyObject *create(const skeleton::SMDSkeleton *owner, const BoneDef *data) {
        SMDBoneDef *bonedef = (SMDBoneDef *) PyType_GenericAlloc(SMDBoneDefType, 0);
        if (!bonedef) return nullptr;
        Py_INCREF((PyObject*)owner);
        bonedef->owner = owner;
        bonedef->data = data;
        return (PyObject *) bonedef;
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *py_int = (PyObject *) &PyLong_Type;
        Py_INCREF(py_int);
        PyDict_SetItemString(ann, "bone", py_int);
        Py_DECREF(py_int);

        PyObject *nodes_ann = PyUnicode_FromString("tuple[float,float,float]");
        if (!nodes_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "pos", nodes_ann) < 0) {
            Py_DECREF(nodes_ann);
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "rot", nodes_ann) < 0) {
            Py_DECREF(nodes_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(nodes_ann);

        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }
}

namespace smd::triangle {
    PyObject *get_material(SMDTriangle *self, void *) {
        return PyUnicode_FromString(self->data->material.c_str());
    }

    PyObject * get_vertices(SMDTriangle *self, void *) {
        PyObject *tuple = PyTuple_New(3);
        if (!tuple) return nullptr;

        for (int i = 0; i < 3; ++i) {
            PyObject *obj = vertex::create(self, &self->data->v[i]);
            if (!obj) {
                Py_DECREF(tuple);
                return nullptr;
            }
            if (PyTuple_SetItem(tuple, i, obj) < 0) {
                Py_DECREF(obj);
                Py_DECREF(tuple);
                return nullptr;
            }
        }

        return tuple;
    }

    void dealloc(SMDTriangle *self) {
        Py_XDECREF((PyObject*)self->owner);
        generic_free((PyObject *) self);
    }

    PyObject *repr(SMDTriangle *self) {
        return PyUnicode_FromFormat("<SMDTriangle material='%s'>",
                                    self->data->material.c_str());
    }

    PyObject *create(const model::SMDModel *owner, const Triangle *data) {
        SMDTriangle *triangle = (SMDTriangle *) PyType_GenericAlloc(SMDTriangleType, 0);
        if (!triangle) return nullptr;
        Py_INCREF((PyObject*)owner);
        triangle->owner = owner;
        triangle->data = data;
        return (PyObject *) triangle;
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *py_str = (PyObject *) &PyUnicode_Type;
        Py_INCREF(py_str);
        PyDict_SetItemString(ann, "material", py_str);
        Py_DECREF(py_str);

        PyObject *vertices_ann = PyUnicode_FromString("tuple[SMDVertex,SMDVertex,SMDVertex]");
        if (!vertices_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "vertices", vertices_ann) < 0) {
            Py_DECREF(vertices_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(vertices_ann);

        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }
}

namespace smd::vertex {
    PyObject * get_pos(SMDVertex *self, void *) {
        return Py_BuildValue("(fff)",
                             self->data->pos[0],
                             self->data->pos[1],
                             self->data->pos[2]);
    }

    PyObject * get_normal(SMDVertex *self, void *) {
        return  Py_BuildValue("(fff)",
                              self->data->normal[0],
                              self->data->normal[1],
                              self->data->normal[2]);
    }

    PyObject * get_uv(SMDVertex *self, void *) {
        return Py_BuildValue("(ff)",
                             self->data->uv[0],
                             self->data->uv[1]);
    }

    PyObject * get_weights(SMDVertex *self, void *) {
        const auto &weights = self->data->weights;
        Py_ssize_t n = (Py_ssize_t) weights.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            const auto &w = weights[i];
            PyObject *tuple = Py_BuildValue("(if)", w.bone, w.weight);
            if (!tuple) {
                Py_DECREF(list);
                return nullptr;
            }
            if (PyList_SetItem(list, i, tuple) < 0) {
                Py_DECREF(tuple);
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    void dealloc(SMDVertex *self) {
        Py_XDECREF((PyObject*)self->owner);
        generic_free((PyObject *) self);
    }

    PyObject *repr(SMDVertex *self) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "<SMDVertex pos=(%.3f, %.3f, %.3f)>",
                      self->data->pos[0],
                      self->data->pos[1],
                      self->data->pos[2]);

        return PyUnicode_FromString(buf);
    }

    PyObject *create(const triangle::SMDTriangle *owner, const Vertex *data) {
        SMDVertex *vertex = (SMDVertex *) PyType_GenericAlloc(SMDVertexType, 0);
        if (!vertex) return nullptr;
        Py_INCREF((PyObject*)owner);
        vertex->owner = owner;
        vertex->data = data;
        return (PyObject *) vertex;
    }

    bool add_annotations(PyObject *type) {
        PyObject *ann = PyDict_New();
        if (!ann) return false;

        PyObject *nodes_ann = PyUnicode_FromString("tuple[float,float,float]");
        if (!nodes_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "pos", nodes_ann) < 0) {
            Py_DECREF(nodes_ann);
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "normal", nodes_ann) < 0) {
            Py_DECREF(nodes_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(nodes_ann);

        PyObject *uv_ann = PyUnicode_FromString("tuple[float,float]");
        if (!uv_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "uv", uv_ann) < 0) {
            Py_DECREF(uv_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(uv_ann);

        PyObject *weights_ann = PyUnicode_FromString("list[tuple[int,float]]");
        if (!weights_ann) {
            Py_DECREF(ann);
            return false;
        }
        if (PyDict_SetItemString(ann, "weights", weights_ann) < 0) {
            Py_DECREF(weights_ann);
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(weights_ann);

        if (PyObject_SetAttrString(type, "__annotations__", ann) < 0) {
            Py_DECREF(ann);
            return false;
        }
        Py_DECREF(ann);
        return true;
    }
}
