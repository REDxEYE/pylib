// Created by RED on 03.12.2025.
#ifndef PYLIB_SMD_CLASS_H
#define PYLIB_SMD_CLASS_H
#include <Python.h>
#include <vector>

#include "utils/smd_parser.h"

#define METHOD_DOC(name, doc) \
    PyDoc_STRVAR(name##_doc, doc);

#define GETTER_METHOD_DEF(name)\
{#name, getter(get_##name), nullptr, get_##name##_doc, nullptr}

static PyObject *no_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyErr_SetString(PyExc_TypeError, "This type cannot be instantiated from Python");
    return nullptr;
}

extern PyTypeObject *SMDModelType;
extern PyTypeObject *SMDNodeType;
extern PyTypeObject *SMDSkeletonType;
extern PyTypeObject *SMDBoneDefType;
extern PyTypeObject *SMDVertexWeightType;
extern PyTypeObject *SMDVertexType;
extern PyTypeObject *SMDTriangleType;
extern PyTypeObject *SMDVAKeyVertexType;

namespace smd::model {
    struct SMDModel {
        PyObject_HEAD
        SMDData *data;
    };

    METHOD_DOC(get_version, "Version (int).");

    PyObject *get_version(SMDModel *self, void *);

    METHOD_DOC(get_node_count, "Node count (int).");

    PyObject *get_node_count(SMDModel *self, void *);

    METHOD_DOC(get_frame_count, "Frame count (int).");

    PyObject *get_frame_count(SMDModel *self, void *);

    METHOD_DOC(get_triangle_count, "Triangle count (int).");

    PyObject *get_triangle_count(SMDModel *self, void *);

    METHOD_DOC(get_nodes, "Nodes (SMDNode).");

    PyObject *get_nodes(SMDModel *self, void *);

    METHOD_DOC(get_skeleton, "Skeleton (SMDSkeleton).");

    PyObject *get_skeleton(SMDModel *self, void *);

    METHOD_DOC(get_triangles, "Triangles (list[SMDTriangle]).");

    PyObject *get_triangles(SMDModel *self, void *);

    PyObject *smd_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);

    void dealloc(SMDModel *self);

    PyObject *repr(SMDModel *self);

    static PyGetSetDef getset[] = {
        GETTER_METHOD_DEF(version),
        GETTER_METHOD_DEF(node_count),
        GETTER_METHOD_DEF(frame_count),
        GETTER_METHOD_DEF(triangle_count),
        GETTER_METHOD_DEF(nodes),
        GETTER_METHOD_DEF(skeleton),
        GETTER_METHOD_DEF(triangles),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_new, (void *) smd_new},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDModel",
        sizeof(SMDModel),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

namespace smd::node {
    struct SMDNode {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Node *data;
    };

    METHOD_DOC(node_get_id, "Id (int).");

    PyObject *get_id(SMDNode *self, void *);

    METHOD_DOC(node_get_name, "Name (str).");

    PyObject *get_name(SMDNode *self, void *);

    METHOD_DOC(node_get_parent, "Parent Id (int).");

    PyObject *get_parent(SMDNode *self, void *);

    void dealloc(SMDNode *self);

    PyObject *repr(SMDNode *self);

    PyObject *create(const model::SMDModel *owner, const Node *data);

    static PyGetSetDef getset[] = {
        {"id", getter(get_id), nullptr, node_get_id_doc, nullptr},
        {"name", getter(get_name), nullptr, node_get_name_doc, nullptr},
        {"parent", getter(get_parent), nullptr, node_get_parent_doc, nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_new, (void *) nullptr},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDNode",
        sizeof(SMDNode),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

namespace smd::skeleton {
    struct SMDSkeleton {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Skeleton *data;
    };

    METHOD_DOC(get_frame_count, "Frame count (int).");

    PyObject *get_frame_count(SMDSkeleton *self, void *);

    METHOD_DOC(get_frames, "Frames (dict[int, list[SMDBoneDef]]).");

    PyObject *get_frames(SMDSkeleton *self, void *);

    static PyGetSetDef getset[] = {
        GETTER_METHOD_DEF(frame_count),
        GETTER_METHOD_DEF(frames),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    void dealloc(SMDSkeleton *self);

    PyObject *repr(SMDSkeleton *self);

    PyObject *create(const model::SMDModel *owner, const Skeleton *data);

    static PyType_Slot class_slots[] = {
        {Py_tp_new, (void *) nullptr},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDSkeleton",
        sizeof(SMDSkeleton),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

namespace smd::bonedef {
    struct SMDBoneDef {
        PyObject_HEAD
        const skeleton::SMDSkeleton *owner;
        const BoneDef *data;
    };

    METHOD_DOC(get_bone, "Id (int).");

    PyObject *get_bone(SMDBoneDef *self, void *);

    METHOD_DOC(get_pos, "Position (int).");

    PyObject *get_pos(SMDBoneDef *self, void *);

    METHOD_DOC(get_rot, "Rotation euler (int).");

    PyObject *get_rot(SMDBoneDef *self, void *);

    void dealloc(SMDBoneDef *self);

    PyObject *repr(SMDBoneDef *self);

    PyObject *create(const skeleton::SMDSkeleton *owner, const BoneDef *data);

    static PyGetSetDef getset[] = {
        GETTER_METHOD_DEF(bone),
        GETTER_METHOD_DEF(pos),
        GETTER_METHOD_DEF(rot),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_new, (void *) nullptr},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDBoneDef",
        sizeof(SMDBoneDef),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

namespace smd::triangle {
    struct SMDTriangle {
        PyObject_HEAD
        const model::SMDModel *owner;
        const Triangle *data;
    };

    METHOD_DOC(get_material, "Material (str).");

    PyObject *get_material(SMDTriangle *self, void *);

    METHOD_DOC(get_vertices, "Triangles (tuple[SMDTriangle,SMDTriangle,SMDTriangle]).");

    PyObject *get_vertices(SMDTriangle *self, void *);

    static PyGetSetDef getset[] = {
        GETTER_METHOD_DEF(material),
        GETTER_METHOD_DEF(vertices),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    void dealloc(SMDTriangle *self);

    PyObject *repr(SMDTriangle *self);

    PyObject *create(const model::SMDModel *owner, const Triangle *data);

    static PyType_Slot class_slots[] = {
        {Py_tp_new, nullptr},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDTriangle",
        sizeof(SMDTriangle),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

namespace smd::vertex {
    struct SMDVertex {
        PyObject_HEAD
        const triangle::SMDTriangle *owner;
        const Vertex *data;
    };

    METHOD_DOC(get_pos, "Position (tuple[float,float,float]).");

    PyObject *get_pos(SMDVertex *self, void *);

    METHOD_DOC(get_normal, "Normal (tuple[float,float,float]).");

    PyObject *get_normal(SMDVertex *self, void *);

    METHOD_DOC(get_uv, "UV (tuple[float,float]).");

    PyObject *get_uv(SMDVertex *self, void *);

    METHOD_DOC(get_weights, "Weights (list[tuple[int,float]]).");

    PyObject *get_weights(SMDVertex *self, void *);

    void dealloc(SMDVertex *self);

    PyObject *repr(SMDVertex *self);

    PyObject *create(const triangle::SMDTriangle *owner, const Vertex *data);

    static PyGetSetDef getset[] = {
        GETTER_METHOD_DEF(pos),
        GETTER_METHOD_DEF(normal),
        GETTER_METHOD_DEF(uv),
        GETTER_METHOD_DEF(weights),
        {nullptr, nullptr, nullptr, nullptr, nullptr}
    };

    static PyMethodDef methods[] = {
        // {"load", CPF(SMD_load), METH_FASTCALL, SMD_load_doc},
        {nullptr, nullptr, 0, nullptr}
    };

    static PyType_Slot class_slots[] = {
        {Py_tp_new, (void *) nullptr},
        {Py_tp_dealloc, (void *) dealloc},
        {Py_tp_methods, (void *) methods},
        {Py_tp_getset, (void *) getset},
        {Py_tp_repr, (void *) repr},
        {0, nullptr}
    };

    static PyType_Spec class_spec = {
        "pylib.mesh.SMDVertex",
        sizeof(SMDVertex),
        0,
        Py_TPFLAGS_DEFAULT,
        class_slots
    };

    bool add_annotations(PyObject *type);
}

#endif //PYLIB_SMD_CLASS_H
