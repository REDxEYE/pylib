//
// Created by RED on 11.08.2025.
//

#include "modules/mesh_module.h"
#include "meshoptimizer.h"
#include <Python.h>


PyObject *py_decode_vertex_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError,
                        "decode_vertex_buffer(input: bytes, vertex_size: int, vertex_count: int) takes exactly 3 arguments");
        return nullptr;
    }
    PyROBytesView data(args[0]);

    if (!data) {
        PyErr_SetString(PyExc_TypeError, "input must be a bytes like object");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2])) {
        PyErr_SetString(PyExc_TypeError, "vertex_size and vertex_count must be integers");
        return nullptr;
    }


    Py_ssize_t vertex_size = PyLong_AsSsize_t(args[1]);
    Py_ssize_t vertex_count = PyLong_AsSsize_t(args[2]);
    if (vertex_size <= 0 || vertex_count < 0) {
        PyErr_SetString(PyExc_ValueError, "vertex_size must be > 0 and vertex_count must be >= 0");
        return nullptr;
    }

    Py_ssize_t out_size;
    if (!mul_checked_psszt(vertex_size, vertex_count, &out_size)) {
        PyErr_SetString(PyExc_OverflowError, "vertex_count * vertex_size overflowed");
        return nullptr;
    }

    PyObject *output = PyBytes_FromStringAndSize(nullptr, out_size);
    if (!output) return nullptr;

    char *dst = PyBytes_AsString(output);
    if (!dst) {
        Py_DECREF(output);
        return nullptr;
    }

    const size_t vtx_count = (size_t) vertex_count;
    const size_t vtx_size = (size_t) vertex_size;
    const size_t enc_size = data.size();

    int rc = meshopt_decodeVertexBuffer(dst, vtx_count, vtx_size,
                                        (const unsigned char *) data.data(), enc_size);
    if (rc > 0) {
        Py_DECREF(output);
        PyErr_Format(PyExc_ValueError, "meshopt_decodeVertexBuffer failed (code %d)", rc);
        return nullptr;
    }

    return output;
}

PyObject *py_decode_index_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError,
                        "decode_index_buffer(input: bytes, index_size: int, index_count: int) takes exactly 3 arguments");
        return nullptr;
    }
    PyROBytesView data(args[0]);

    if (!data) {
        PyErr_SetString(PyExc_TypeError, "input must be a bytes like object");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2])) {
        PyErr_SetString(PyExc_TypeError, "index_size and index_count must be integers");
        return nullptr;
    }

    Py_ssize_t index_size = PyLong_AsSsize_t(args[1]);
    Py_ssize_t index_count = PyLong_AsSsize_t(args[2]);
    if (index_size <= 0 || index_count < 0) {
        PyErr_SetString(PyExc_ValueError, "index_size must be > 0 and index_count must be >= 0");
        return nullptr;
    }
    if (index_size != 2 && index_size != 4) {
        PyErr_SetString(PyExc_ValueError, "index_size must be 2 or 4");
        return nullptr;
    }

    Py_ssize_t out_size;
    if (!mul_checked_psszt(index_size, index_count, &out_size)) {
        PyErr_SetString(PyExc_OverflowError, "index_count * index_size overflowed");
        return nullptr;
    }

    PyObject *output = PyBytes_FromStringAndSize(nullptr, out_size);
    if (!output) return nullptr;

    char *dst = PyBytes_AsString(output);
    if (!dst) {
        Py_DECREF(output);
        return nullptr;
    }

    const size_t idx_count = (size_t) index_count;
    const size_t idx_size = (size_t) index_size;
    const size_t enc_size = data.size();

    int rc = meshopt_decodeIndexBuffer(dst, idx_count, idx_size,
                                       (const unsigned char *) data.data(), enc_size);
    if (rc > 0) {
        Py_DECREF(output);
        PyErr_Format(PyExc_ValueError, "meshopt_decodeIndexBuffer failed (code %d)", rc);
        return nullptr;
    }

    return output;
}