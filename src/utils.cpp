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