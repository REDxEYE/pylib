#ifndef PYLIB_MESH_MODULE_H
#define PYLIB_MESH_MODULE_H

#include <Python.h>
#include "utils/utils.h"

PyObject *py_decode_vertex_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_decode_index_buffer(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyDoc_STRVAR(py_decode_vertex_buffer_fn_doc,
             "decode_vertex_buffer($module, /, input_data, vertex_size, vertex_count)\n"
             "--\n"
             "\n"
             "Decode compressed vertex buffer.\n"
);
PyDoc_STRVAR(py_decode_index_buffer_fn_doc,
             "decode_index_buffer($module, /, input_data, index_size, index_count)\n"
             "--\n"
             "\n"
             "Decode compressed index buffer.\n"
);

static PyMethodDef mesh_methods[] = {
        {"decode_vertex_buffer", CPF(py_decode_vertex_buffer), METH_FASTCALL, py_decode_vertex_buffer_fn_doc},
        {"decode_index_buffer",  CPF(py_decode_index_buffer),  METH_FASTCALL, py_decode_index_buffer_fn_doc},

        {nullptr,                nullptr, 0,                                  nullptr}
};

static struct PyModuleDef mesh_module_def = {
        PyModuleDef_HEAD_INIT,
        "mesh",
        "SourceIO mesh module",
        -1,
        mesh_methods
};

static PyObject *MeshModule_Init(PyObject *parent_module) {
    return add_submodule(parent_module, "mesh", &mesh_module_def);
}

#endif //PYLIB_MESH_MODULE_H
