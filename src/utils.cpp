//
// Created by RED on 11.08.2025.
//

#include "utils.h"

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

PyObject *create_int_enum(const char *name, const std::span<std::pair<std::string, uint32_t>> &members) {
    PyObject* enum_mod = PyImport_ImportModule("enum");
    if (!enum_mod)
        return nullptr;

    PyObject* intenum_cls = PyObject_GetAttrString(enum_mod, "IntEnum");
    Py_DECREF(enum_mod);
    if (!intenum_cls)
        return nullptr;

    PyObject* members_dict = PyDict_New();
    if (!members_dict) {
        Py_DECREF(intenum_cls);
        return nullptr;
    }

    for (const auto& kv : members) {
        PyObject* val = PyLong_FromUnsignedLong(kv.second);
        if (!val) {
            Py_DECREF(members_dict);
            Py_DECREF(intenum_cls);
            return nullptr;
        }
        if (PyDict_SetItemString(members_dict, kv.first.c_str(), val) < 0) {
            Py_DECREF(val);
            Py_DECREF(members_dict);
            Py_DECREF(intenum_cls);
            return nullptr;
        }
        Py_DECREF(val);
    }

    PyObject* args = PyTuple_Pack(2, PyUnicode_FromString(name), members_dict);
    Py_DECREF(members_dict);
    if (!args) {
        Py_DECREF(intenum_cls);
        return nullptr;
    }

    PyObject* enum_type = PyObject_CallObject(intenum_cls, args);
    Py_DECREF(args);
    Py_DECREF(intenum_cls);

    return enum_type;  // new reference
}
