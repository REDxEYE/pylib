//
// Created by RED on 11.08.2025.
//

#include "modules/mesh_module.h"
#include "classes/smd_class.h"
#include "utils/utils.h"
#include "meshoptimizer.h"
#include <Python.h>


static PyObject *py_decode_vertex_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
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
    if (PyErr_Occurred())
        return nullptr;
    // meshoptimizer only asserts these; in release builds a larger vertex_size
    // overflows its 256-byte stack buffer.
    if (vertex_size <= 0 || vertex_size > 256 || vertex_size % 4 != 0 || vertex_count < 0) {
        PyErr_SetString(PyExc_ValueError,
                        "vertex_size must be a multiple of 4 in 4..256 and vertex_count must be >= 0");
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
    int rc;
    // `data` and the unpublished `output` stay alive for the whole call.
    Py_BEGIN_ALLOW_THREADS
    rc = meshopt_decodeVertexBuffer(dst, vtx_count, vtx_size,
                                    (const unsigned char *) data.data(), enc_size);
    Py_END_ALLOW_THREADS
    if (rc != 0) {  // 0 = success, negative = malformed/truncated input
        Py_DECREF(output);
        PyErr_Format(PyExc_ValueError, "meshopt_decodeVertexBuffer failed (code %d)", rc);
        return nullptr;
    }

    return output;
}

static PyObject *py_decode_index_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
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
    if (PyErr_Occurred())
        return nullptr;
    if (index_size <= 0 || index_count < 0) {
        PyErr_SetString(PyExc_ValueError, "index_size must be > 0 and index_count must be >= 0");
        return nullptr;
    }
    // The decoder emits whole triangles; a partial one would be written past
    // the end of the output buffer.
    if (index_count % 3 != 0) {
        PyErr_SetString(PyExc_ValueError, "index_count must be a multiple of 3");
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
    int rc;
    Py_BEGIN_ALLOW_THREADS
    rc = meshopt_decodeIndexBuffer(dst, idx_count, idx_size,
                                   (const unsigned char *) data.data(), enc_size);
    Py_END_ALLOW_THREADS
    if (rc != 0) {  // 0 = success, negative = malformed/truncated input
        Py_DECREF(output);
        PyErr_Format(PyExc_ValueError, "meshopt_decodeIndexBuffer failed (code %d)", rc);
        return nullptr;
    }

    return output;
}

PyDoc_STRVAR(py_decode_vertex_buffer_fn_doc,
             "decode_vertex_buffer($module, input_data, vertex_size, vertex_count, /)\n"
             "--\n"
             "\n"
             "Decode a meshoptimizer-compressed vertex buffer.\n"
             "\n"
             "input_data: bytes-like encoded data; vertex_size: bytes per vertex (multiple of 4,\n"
             "4..256); vertex_count: number of vertices (>= 0).\n"
             "Returns bytes of length vertex_size * vertex_count.\n"
             "Raises TypeError, ValueError (bad sizes or malformed data) or OverflowError.\n"
);
PyDoc_STRVAR(py_decode_index_buffer_fn_doc,
             "decode_index_buffer($module, input_data, index_size, index_count, /)\n"
             "--\n"
             "\n"
             "Decode a meshoptimizer-compressed index buffer.\n"
             "\n"
             "input_data: bytes-like encoded data; index_size: 2 or 4; index_count: number of\n"
             "indices (>= 0, multiple of 3).\n"
             "Returns bytes of length index_size * index_count.\n"
             "Raises TypeError, ValueError (bad sizes or malformed data) or OverflowError.\n"
);

static PyMethodDef mesh_methods[] = {
    {"decode_vertex_buffer", CPF(py_decode_vertex_buffer), METH_FASTCALL, py_decode_vertex_buffer_fn_doc},
    {"decode_index_buffer", CPF(py_decode_index_buffer), METH_FASTCALL, py_decode_index_buffer_fn_doc},
    {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef mesh_module_def = {
    PyModuleDef_HEAD_INIT,
    "pylib.mesh",
    "SourceIO mesh module",
    -1,
    mesh_methods
};

PyObject *MeshModule_Init(PyObject *parent_module) {
    PyObject *mod = add_submodule(parent_module, "mesh", &mesh_module_def);
    if (!mod)
        return nullptr;

    // The wrappers allocate child objects from C via these globals.
    if (add_type(mod, "SMDModel", &smd::model::class_spec, &SMDModelType) < 0)
        return nullptr;
    if (add_type(mod, "SMDNode", &smd::node::class_spec, &SMDNodeType) < 0)
        return nullptr;
    if (add_type(mod, "SMDSkeleton", &smd::skeleton::class_spec, &SMDSkeletonType) < 0)
        return nullptr;
    if (add_type(mod, "SMDBoneDef", &smd::bonedef::class_spec, &SMDBoneDefType) < 0)
        return nullptr;
    if (add_type(mod, "SMDTriangle", &smd::triangle::class_spec, &SMDTriangleType) < 0)
        return nullptr;
    if (add_type(mod, "SMDVertex", &smd::vertex::class_spec, &SMDVertexType) < 0)
        return nullptr;
    return mod;
}
