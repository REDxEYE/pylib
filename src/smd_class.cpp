// Created by RED on 03.12.2025.
#include "classes/smd_class.h"

#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>


PyTypeObject *SMDModelType = nullptr;
PyTypeObject *SMDNodeType = nullptr;
PyTypeObject *SMDSkeletonType = nullptr;
PyTypeObject *SMDBoneDefType = nullptr;
PyTypeObject *SMDVertexType = nullptr;
PyTypeObject *SMDTriangleType = nullptr;

#define GETTER_DEF(name, doc) \
    {#name, (getter) get_##name, nullptr, PyDoc_STR(doc), nullptr}

// SMD names are raw bytes (commonly cp1252): try strict UTF-8, fall back to
// latin-1 (lossless) instead of raising.
static PyObject *decode_smd_string(const std::string &s) {
    PyObject *res = PyUnicode_DecodeUTF8(s.data(), (Py_ssize_t) s.size(), nullptr);
    if (res || !PyErr_ExceptionMatches(PyExc_UnicodeDecodeError)) return res;
    PyErr_Clear();
    return PyUnicode_DecodeLatin1(s.data(), (Py_ssize_t) s.size(), nullptr);
}

// Final step of every tp_dealloc: free through the type's tp_free slot and
// drop the instance's reference to its heap type.
static void heap_free(PyObject *self) {
    PyTypeObject *tp = Py_TYPE(self);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))(self);
    Py_DECREF(tp);
}

static PyObject *float3_tuple(const float v[3]) {
    return Py_BuildValue("(fff)", v[0], v[1], v[2]);
}

namespace smd::model {
    struct SMDModel {
        PyObject_HEAD
        SMDData *data;
    };
}

namespace smd::node {
    struct SMDNode {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Node *data;
    };

    static PyObject *create(const model::SMDModel *owner, const Node *data);
}

namespace smd::skeleton {
    struct SMDSkeleton {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Skeleton *data;
    };

    static PyObject *create(const model::SMDModel *owner, const Skeleton *data);
}

namespace smd::bonedef {
    struct SMDBoneDef {
        PyObject_HEAD
        const skeleton::SMDSkeleton *owner;
        const BoneDef *data;
    };

    static PyObject *create(const skeleton::SMDSkeleton *owner, const BoneDef *data);
}

namespace smd::triangle {
    struct SMDTriangle {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Triangle *data;
    };

    static PyObject *create(const model::SMDModel *owner, const Triangle *data);
}

namespace smd::vertex {
    struct SMDVertex {
        PyObject_HEAD
        const triangle::SMDTriangle *owner;
        const Vertex *data;
    };

    static PyObject *create(const triangle::SMDTriangle *owner, const Vertex *data);
}

namespace smd::model {
    static bool ensure_data(SMDModel *self) {
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

    static PyObject *get_version(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromLong(self->data->version);
    }

    static PyObject *get_kind(SMDModel *self, void *) {
        ENSURE_DATA(self)
        const char *kind = "unknown";
        switch (self->data->kind) {
            case FileKind::ReferenceMesh: kind = "reference"; break;
            case FileKind::Animation: kind = "animation"; break;
            case FileKind::VertexAnimation: kind = "vertex_animation"; break;
            case FileKind::Unknown: break;
        }
        return PyUnicode_FromString(kind);
    }

    static PyObject *get_node_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromSize_t(self->data->nodes.size());
    }

    static PyObject *get_frame_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromSize_t(self->data->skeleton.frames.size());
    }

    static PyObject *get_triangle_count(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return PyLong_FromSize_t(self->data->triangles.size());
    }

    static PyObject *get_nodes(SMDModel *self, void *) {
        ENSURE_DATA(self)
        const auto &nodes = self->data->nodes;

        Py_ssize_t n = (Py_ssize_t) nodes.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *obj = node::create(self, &nodes[i]);
            // PyList_SetItem steals `obj` even on failure.
            if (!obj || PyList_SetItem(list, i, obj) < 0) {
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    static PyObject *get_skeleton(SMDModel *self, void *) {
        ENSURE_DATA(self)
        return skeleton::create(self, &self->data->skeleton);
    }

    static PyObject *get_triangles(SMDModel *self, void *) {
        ENSURE_DATA(self)
        const auto &triangles = self->data->triangles;

        Py_ssize_t n = (Py_ssize_t) triangles.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            PyObject *obj = triangle::create(self, &triangles[i]);
            if (!obj || PyList_SetItem(list, i, obj) < 0) {
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    static PyObject *get_vertex_animation(SMDModel *self, void *) {
        ENSURE_DATA(self)
        PyObject *dict = PyDict_New();
        if (!dict) return nullptr;

        for (const auto &[time, frame]: self->data->vertexAnimation.frames) {
            Py_ssize_t n = (Py_ssize_t) frame.size();
            PyObject *list = PyList_New(n);
            if (!list) {
                Py_DECREF(dict);
                return nullptr;
            }
            for (Py_ssize_t i = 0; i < n; ++i) {
                const VAKeyVertex &v = frame[i];
                PyObject *item = Py_BuildValue("(i(fff)(fff))", v.refIndex,
                                               v.pos[0], v.pos[1], v.pos[2],
                                               v.normal[0], v.normal[1], v.normal[2]);
                if (!item || PyList_SetItem(list, i, item) < 0) {
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
            int rc = PyDict_SetItem(dict, py_time, list);
            Py_DECREF(py_time);
            Py_DECREF(list);
            if (rc < 0) {
                Py_DECREF(dict);
                return nullptr;
            }
        }
        return dict;
    }

    static bool parse_smd(SMDData *data, std::string_view input) {
        Parser parser(input);
        if (!parser.parse(data)) {
            PyErr_SetString(PyExc_ValueError, parser.errorMessage().c_str());
            return false;
        }
        return true;
    }

    static PyObject *smd_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
        if (kwargs && PyDict_Size(kwargs) != 0) {
            PyErr_SetString(PyExc_TypeError, "SMDModel() takes no keyword arguments");
            return nullptr;
        }
        if (PyTuple_Size(args) != 1) {
            PyErr_SetString(PyExc_TypeError,
                            "SMDModel(input: bytes|str) takes exactly 1 argument");
            return nullptr;
        }
        PyObject *input = PyTuple_GetItem(args, 0);
        if (!PyBytes_Check(input) && !PyUnicode_Check(input)) {
            PyErr_SetString(PyExc_TypeError, "input must be bytes or str");
            return nullptr;
        }

        SMDModel *self = (SMDModel *) PyType_GenericNew(type, nullptr, nullptr);
        if (!self) return nullptr;

        const char *input_data;
        Py_ssize_t size;
        if (PyBytes_Check(input)) {
            input_data = PyBytes_AsString(input);
            size = PyBytes_Size(input);
        } else {
            input_data = PyUnicode_AsUTF8AndSize(input, &size);
        }
        if (!input_data) {  // e.g. lone surrogates in a str
            Py_DECREF(self);
            return nullptr;
        }

        // C++ exceptions must not cross back into the interpreter: an uncaught
        // one terminates the process.
        bool ok = false;
        try {
            self->data = new SMDData();
            ok = parse_smd(self->data, std::string_view(input_data, (size_t) size));
        } catch (const std::bad_alloc &) {
            PyErr_NoMemory();
        } catch (const std::exception &e) {
            PyErr_SetString(PyExc_ValueError, e.what());
        }
        if (!ok) {
            Py_DECREF(self);  // dealloc deletes `data`
            return nullptr;
        }
        return (PyObject *) self;
    }

    static void dealloc(SMDModel *self) {
        delete self->data;
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDModel *self) {
        ENSURE_DATA(self)
        return PyUnicode_FromFormat("<SMDModel nodes=%zu frames=%zu triangles=%zu>",
                                    self->data->nodes.size(),
                                    self->data->skeleton.frames.size(),
                                    self->data->triangles.size());
    }

#undef ENSURE_DATA

    static PyGetSetDef getset[] = {
        GETTER_DEF(version, "SMD format version (int)."),
        GETTER_DEF(kind, "File kind deduced from content (str): 'reference' (triangles + 1 skeleton frame), "
                         "'animation' (skeleton frames, no triangles), 'vertex_animation' (has a "
                         "vertexanimation block) or 'unknown'."),
        GETTER_DEF(node_count, "Number of nodes (int)."),
        GETTER_DEF(frame_count, "Number of skeleton frames (int)."),
        GETTER_DEF(triangle_count, "Number of triangles (int)."),
        GETTER_DEF(nodes, "Nodes (list[SMDNode])."),
        GETTER_DEF(skeleton, "Skeleton (SMDSkeleton)."),
        GETTER_DEF(triangles, "Triangles (list[SMDTriangle])."),
        GETTER_DEF(vertex_animation,
                   "Vertex animation frames (dict[int, list[tuple[int, tuple[float, float, float], "
                   "tuple[float, float, float]]]]): frame time -> list of (vertex_id, pos, normal). "
                   "Empty if the file has no vertexanimation block."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_doc, (void *) PyDoc_STR("SMDModel(input: bytes | str)\n--\n\n"
                                       "Parse an SMD/VTA text file. Raises ValueError on malformed input.")},
        {Py_tp_new, (void *) smd_new},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDModel",
        sizeof(SMDModel),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };
}

namespace smd::node {
    static PyObject *get_id(SMDNode *self, void *) {
        return PyLong_FromLong(self->data->id);
    }

    static PyObject *get_name(SMDNode *self, void *) {
        return decode_smd_string(self->data->name);
    }

    static PyObject *get_parent(SMDNode *self, void *) {
        return PyLong_FromLong(self->data->parent);
    }

    static void dealloc(SMDNode *self) {
        Py_XDECREF((PyObject *) self->owner);
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDNode *self) {
        PyObject *name = decode_smd_string(self->data->name);
        if (!name) return nullptr;
        PyObject *res = PyUnicode_FromFormat("<SMDNode id=%d name='%U' parent_id=%d>",
                                             self->data->id, name, self->data->parent);
        Py_DECREF(name);
        return res;
    }

    static PyObject *create(const model::SMDModel *owner, const Node *data) {
        SMDNode *node = (SMDNode *) PyType_GenericAlloc(SMDNodeType, 0);
        if (!node) return nullptr;
        Py_INCREF((PyObject *) owner);
        node->owner = owner;
        node->data = data;
        return (PyObject *) node;
    }

    static PyGetSetDef getset[] = {
        GETTER_DEF(id, "Node id (int)."),
        GETTER_DEF(name, "Node name (str)."),
        GETTER_DEF(parent, "Parent node id (int), -1 for root nodes."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDNode",
        sizeof(SMDNode),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        class_slots
    };
}

namespace smd::skeleton {
    static PyObject *get_frame_count(SMDSkeleton *self, void *) {
        return PyLong_FromSize_t(self->data->frames.size());
    }

    static PyObject *get_frames(SMDSkeleton *self, void *) {
        PyObject *dict = PyDict_New();
        if (!dict) return nullptr;
        for (const auto &[time, bone_defs]: self->data->frames) {
            Py_ssize_t n = (Py_ssize_t) bone_defs.size();
            PyObject *list = PyList_New(n);
            if (!list) {
                Py_DECREF(dict);
                return nullptr;
            }

            for (Py_ssize_t i = 0; i < n; ++i) {
                PyObject *obj = bonedef::create(self, &bone_defs[i]);
                if (!obj || PyList_SetItem(list, i, obj) < 0) {
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
            int rc = PyDict_SetItem(dict, py_time, list);
            Py_DECREF(py_time);
            Py_DECREF(list);
            if (rc < 0) {
                Py_DECREF(dict);
                return nullptr;
            }
        }
        return dict;
    }

    static void dealloc(SMDSkeleton *self) {
        Py_XDECREF((PyObject *) self->owner);
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDSkeleton *self) {
        return PyUnicode_FromFormat("<SMDSkeleton frames=%zu>",
                                    self->data->frames.size());
    }

    static PyObject *create(const model::SMDModel *owner, const Skeleton *data) {
        SMDSkeleton *skeleton = (SMDSkeleton *) PyType_GenericAlloc(SMDSkeletonType, 0);
        if (!skeleton) return nullptr;
        Py_INCREF((PyObject *) owner);
        skeleton->owner = owner;
        skeleton->data = data;
        return (PyObject *) skeleton;
    }

    static PyGetSetDef getset[] = {
        GETTER_DEF(frame_count, "Number of frames (int)."),
        GETTER_DEF(frames, "Frames (dict[int, list[SMDBoneDef]]): frame time -> bone transforms."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDSkeleton",
        sizeof(SMDSkeleton),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        class_slots
    };
}

namespace smd::bonedef {
    static PyObject *get_bone(SMDBoneDef *self, void *) {
        return PyLong_FromLong(self->data->bone);
    }

    static PyObject *get_pos(SMDBoneDef *self, void *) {
        return float3_tuple(self->data->pos);
    }

    static PyObject *get_rot(SMDBoneDef *self, void *) {
        return float3_tuple(self->data->rot);
    }

    static void dealloc(SMDBoneDef *self) {
        Py_XDECREF((PyObject *) self->owner);
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDBoneDef *self) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "<SMDBoneDef bone=%d pos=(%.3f, %.3f, %.3f) rot=(%.3f, %.3f, %.3f)>",
                      self->data->bone,
                      self->data->pos[0], self->data->pos[1], self->data->pos[2],
                      self->data->rot[0], self->data->rot[1], self->data->rot[2]
        );

        return PyUnicode_FromString(buf);
    }

    static PyObject *create(const skeleton::SMDSkeleton *owner, const BoneDef *data) {
        SMDBoneDef *bonedef = (SMDBoneDef *) PyType_GenericAlloc(SMDBoneDefType, 0);
        if (!bonedef) return nullptr;
        Py_INCREF((PyObject *) owner);
        bonedef->owner = owner;
        bonedef->data = data;
        return (PyObject *) bonedef;
    }

    static PyGetSetDef getset[] = {
        GETTER_DEF(bone, "Bone (node) id (int)."),
        GETTER_DEF(pos, "Position (tuple[float, float, float])."),
        GETTER_DEF(rot, "Rotation as Euler angles in radians (tuple[float, float, float])."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDBoneDef",
        sizeof(SMDBoneDef),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        class_slots
    };
}

namespace smd::triangle {
    static PyObject *get_material(SMDTriangle *self, void *) {
        return decode_smd_string(self->data->material);
    }

    static PyObject *get_vertices(SMDTriangle *self, void *) {
        PyObject *tuple = PyTuple_New(3);
        if (!tuple) return nullptr;

        for (int i = 0; i < 3; ++i) {
            PyObject *obj = vertex::create(self, &self->data->v[i]);
            // PyTuple_SetItem steals `obj` even on failure.
            if (!obj || PyTuple_SetItem(tuple, i, obj) < 0) {
                Py_DECREF(tuple);
                return nullptr;
            }
        }

        return tuple;
    }

    static void dealloc(SMDTriangle *self) {
        Py_XDECREF((PyObject *) self->owner);
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDTriangle *self) {
        PyObject *material = decode_smd_string(self->data->material);
        if (!material) return nullptr;
        PyObject *res = PyUnicode_FromFormat("<SMDTriangle material='%U'>", material);
        Py_DECREF(material);
        return res;
    }

    static PyObject *create(const model::SMDModel *owner, const Triangle *data) {
        SMDTriangle *triangle = (SMDTriangle *) PyType_GenericAlloc(SMDTriangleType, 0);
        if (!triangle) return nullptr;
        Py_INCREF((PyObject *) owner);
        triangle->owner = owner;
        triangle->data = data;
        return (PyObject *) triangle;
    }

    static PyGetSetDef getset[] = {
        GETTER_DEF(material, "Material name (str)."),
        GETTER_DEF(vertices, "Vertices (tuple[SMDVertex, SMDVertex, SMDVertex])."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDTriangle",
        sizeof(SMDTriangle),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        class_slots
    };
}

namespace smd::vertex {
    static PyObject *get_pos(SMDVertex *self, void *) {
        return float3_tuple(self->data->pos);
    }

    static PyObject *get_normal(SMDVertex *self, void *) {
        return float3_tuple(self->data->normal);
    }

    static PyObject *get_uv(SMDVertex *self, void *) {
        return Py_BuildValue("(ff)", self->data->uv[0], self->data->uv[1]);
    }

    static PyObject *get_weights(SMDVertex *self, void *) {
        const auto &weights = self->data->weights;
        Py_ssize_t n = (Py_ssize_t) weights.size();
        PyObject *list = PyList_New(n);
        if (!list) return nullptr;

        for (Py_ssize_t i = 0; i < n; ++i) {
            const auto &w = weights[i];
            PyObject *tuple = Py_BuildValue("(if)", w.bone, w.weight);
            if (!tuple || PyList_SetItem(list, i, tuple) < 0) {
                Py_DECREF(list);
                return nullptr;
            }
        }

        return list;
    }

    static void dealloc(SMDVertex *self) {
        Py_XDECREF((PyObject *) self->owner);
        heap_free((PyObject *) self);
    }

    static PyObject *repr(SMDVertex *self) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "<SMDVertex pos=(%.3f, %.3f, %.3f)>",
                      self->data->pos[0],
                      self->data->pos[1],
                      self->data->pos[2]);

        return PyUnicode_FromString(buf);
    }

    static PyObject *create(const triangle::SMDTriangle *owner, const Vertex *data) {
        SMDVertex *vertex = (SMDVertex *) PyType_GenericAlloc(SMDVertexType, 0);
        if (!vertex) return nullptr;
        Py_INCREF((PyObject *) owner);
        vertex->owner = owner;
        vertex->data = data;
        return (PyObject *) vertex;
    }

    static PyGetSetDef getset[] = {
        GETTER_DEF(pos, "Position (tuple[float, float, float])."),
        GETTER_DEF(normal, "Normal (tuple[float, float, float])."),
        GETTER_DEF(uv, "Texture coordinates (tuple[float, float])."),
        GETTER_DEF(weights,
                   "Skinning weights (list[tuple[int, float]]) as (bone_id, weight); empty when the "
                   "line has no weight list (the vertex is then fully bound to its parent bone)."),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    PyType_Spec class_spec = {
        "pylib.mesh.SMDVertex",
        sizeof(SMDVertex),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        class_slots
    };
}
