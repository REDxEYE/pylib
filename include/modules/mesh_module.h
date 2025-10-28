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

static PyObject* MeshModule_Init(PyObject* parent_module){
    PyObject * module = PyModule_Create(&mesh_module_def);
    if(!module){
        Py_DECREF(module);
        return nullptr;
    }

    if(PyModule_AddObject(parent_module, "mesh", module) < 0){
        Py_DECREF(module);
        return nullptr;
    }
    Py_INCREF(module);

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_SetItemString(modules, "pylib.mesh", module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }
    // Set __path__ attribute for the submodule
    PyObject *path_list = Py_BuildValue("[s]", "pylib/mesh");
    if (path_list) {
        Py_INCREF(path_list);
        if (PyModule_AddObject(module, "__path__", path_list) < 0) {
            Py_DECREF(path_list);
            Py_DECREF(module);
            return nullptr;
        }
    }
    return module;
}

#endif //PYLIB_MESH_MODULE_H
