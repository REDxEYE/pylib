//
// Created by RED on 11.08.2025.
//

#include "utils/utils.h"

PyObject *type_error(const char* arg, const char* expected, PyObject* got) {
    PyObject* t = PyObject_Type(got);              // new ref
    if (!t) return PyErr_Format(PyExc_RuntimeError, "Failed to get type of %s", arg);
    PyObject* n = PyObject_GetAttrString(t, "__name__"); // new ref
    Py_DECREF(t);
    if (!n) return PyErr_Format(PyExc_RuntimeError, "Failed to get name of type of %s", arg);
    Py_ssize_t cname_size = 0;
    const char* cname = PyUnicode_AsUTF8AndSize(n,&cname_size);       // borrowed char*
    if (!cname) { Py_DECREF(n); return PyErr_Occurred(); }
    auto error = PyErr_Format(PyExc_TypeError, "%s must be %s, not %.100s", arg, expected, cname);
    Py_DECREF(n);
    return error;
}

//! Shared implementation of create_int_enum/create_int_flags: both build a class
//! from ``enum`` with the same member dict, differing only in the base class.
static PyObject *create_enum_of_kind(const char *kind, const char *name,
                                     const std::span<std::pair<std::string, uint32_t>> &members) {
    PyObject *enum_mod = PyImport_ImportModule("enum");
    if (!enum_mod)
        return nullptr;

    PyObject *base_cls = PyObject_GetAttrString(enum_mod, kind);
    Py_DECREF(enum_mod);
    if (!base_cls)
        return nullptr;

    PyObject *members_dict = PyDict_New();
    if (!members_dict) {
        Py_DECREF(base_cls);
        return nullptr;
    }

    for (const auto &kv: members) {
        PyObject *val = PyLong_FromUnsignedLong(kv.second);
        if (!val) {
            Py_DECREF(members_dict);
            Py_DECREF(base_cls);
            return nullptr;
        }
        int failed = PyDict_SetItemString(members_dict, kv.first.c_str(), val) < 0;
        Py_DECREF(val);
        if (failed) {
            Py_DECREF(members_dict);
            Py_DECREF(base_cls);
            return nullptr;
        }
    }

    PyObject *py_name = PyUnicode_FromString(name);
    if (!py_name) {
        Py_DECREF(members_dict);
        Py_DECREF(base_cls);
        return nullptr;
    }
    // PyTuple_Pack does not steal, so drop our own references afterwards.
    PyObject *args = PyTuple_Pack(2, py_name, members_dict);
    Py_DECREF(py_name);
    Py_DECREF(members_dict);
    if (!args) {
        Py_DECREF(base_cls);
        return nullptr;
    }

    PyObject *enum_type = PyObject_CallObject(base_cls, args);
    Py_DECREF(args);
    Py_DECREF(base_cls);

    return enum_type;  // new reference
}

PyObject *create_int_enum(const char *name, const std::span<std::pair<std::string, uint32_t>> &members) {
    return create_enum_of_kind("IntEnum", name, members);
}

PyObject *create_int_flags(const char *name, const std::span<std::pair<std::string, uint32_t>> &members) {
    return create_enum_of_kind("IntFlag", name, members);
}

PyObject *add_submodule(PyObject *parent, const char *name, PyModuleDef *def) {
    PyObject *module = PyModule_Create(def);
    if (!module)
        return nullptr;

    // Make the submodule importable as ``pylib.<name>`` and package-like, so
    // ``from pylib.vtf import ...`` resolves without a Python-side shim.
    std::string qualified_name = std::string("pylib.") + name;
    PyObject *path_list = Py_BuildValue("[s]", qualified_name.c_str());
    if (!path_list) {
        Py_DECREF(module);
        return nullptr;
    }
    int failed = PyModule_AddObject(module, "__path__", path_list) < 0;
    if (failed) {
        Py_DECREF(path_list);
        Py_DECREF(module);
        return nullptr;
    }

    if (PyDict_SetItemString(PyImport_GetModuleDict(), qualified_name.c_str(), module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    // Steals our reference on success, leaving `parent` as the sole owner.
    if (PyModule_AddObject(parent, name, module) < 0) {
        PyDict_DelItemString(PyImport_GetModuleDict(), qualified_name.c_str());
        Py_DECREF(module);
        return nullptr;
    }
    return module;  // borrowed, owned by parent
}

int add_type(PyObject *module, const char *name, PyType_Spec *spec) {
    PyObject *type = PyType_FromSpec(spec);
    if (!type)
        return -1;
    if (PyModule_AddObject(module, name, type) < 0) {  // steals on success
        Py_DECREF(type);
        return -1;
    }
    return 0;
}

int add_int_enum(PyObject *module, const char *name,
                 const std::span<std::pair<std::string, uint32_t>> &members) {
    PyObject *enum_type = create_int_enum(name, members);
    if (!enum_type)
        return -1;
    if (PyModule_AddObject(module, name, enum_type) < 0) {  // steals on success
        Py_DECREF(enum_type);
        return -1;
    }
    return 0;
}

int add_int_flags(PyObject *module, const char *name,
                  const std::span<std::pair<std::string, uint32_t>> &members) {
    PyObject *flags_type = create_int_flags(name, members);
    if (!flags_type)
        return -1;
    if (PyModule_AddObject(module, name, flags_type) < 0) {  // steals on success
        Py_DECREF(flags_type);
        return -1;
    }
    return 0;
}

PyROBytesView::PyROBytesView(PyObject *obj) {
    if (!obj) return;
    if (PyBytes_Check(obj)) {
        owner_ = obj;
        Py_INCREF(owner_);
    } else {
        owner_ = PyBytes_FromObject(obj);
        if (!owner_) return;
    }
    char* p = PyBytes_AsString(owner_);
    if (!p) {
        Py_CLEAR(owner_);
        return;
    }
    Py_ssize_t n = PyBytes_Size(owner_);
    if (n < 0) {
        Py_CLEAR(owner_);
        return;
    }
    data_ = p;
    size_ = static_cast<size_t>(n);
}

void PyROBytesView::move_from(PyROBytesView &other) {
    owner_ = other.owner_;
    data_ = other.data_;
    size_ = other.size_;
    other.owner_ = nullptr;
    other.data_ = nullptr;
    other.size_ = 0;
}

void PyROBytesView::reset() {
    if (owner_) {
        Py_DECREF(owner_);
        owner_ = nullptr;
        data_ = nullptr;
        size_ = 0;
    }
}

PyROBytesView::~PyROBytesView() {
    reset();
}

PyROBytesView &PyROBytesView::operator=(PyROBytesView &&other) noexcept {
    if (this != &other) {
        reset();
        move_from(other);
    }
    return *this;
}

PyROBytesView::PyROBytesView(PyROBytesView &&other) noexcept {
    move_from(other);
}
