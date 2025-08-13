#ifndef PYLIB_MESH_MODULE_H
#define PYLIB_MESH_MODULE_H

#include <Python.h>
#include "utils.h"

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

static PyObject* MeshModule_Init(PyObject* parent_module){
    PyObject * mesh_module = PyModule_Create(&mesh_module_def);
    if(!mesh_module){
        Py_DECREF(mesh_module);
        return nullptr;
    }

    if(PyModule_AddObjectRef(parent_module, "mesh", mesh_module)<0){
        Py_DECREF(mesh_module);
        Py_DECREF(parent_module);
        return nullptr;
    }

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_SetItemString(modules, "pylib.mesh", mesh_module) < 0) {
        Py_DECREF(mesh_module);
        Py_DECREF(parent_module);
        return nullptr;
    }
    return mesh_module;
}

#endif //PYLIB_MESH_MODULE_H
