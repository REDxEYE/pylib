//
// Created by RED on 12.08.2025.
//

#ifndef PYLIB_VTF_MODULE_H
#define PYLIB_VTF_MODULE_H

#include <Python.h>
#include "utils.h"
#include "vtf_class.h"
#include "VTFFile.h"


PyObject *py_load_vtf_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs);


PyDoc_STRVAR(py_load_vtf_texture_fn_doc,
             "load_vtf_texture($module, /, input_data)\n"
             "--\n"
             "\n"
             "Load VTF texture from input data.\n"
);

PyDoc_STRVAR(mod_version_doc,
             "version($module, /)\n"
             "--\n"
             "\n"
             "Return VTFLib version string.");


PyObject* mod_get_version(PyObject*, PyObject* const*, Py_ssize_t);

static PyMethodDef vtf_methods[] = {
        {"load_vtf_texture", CPF(py_load_vtf_texture), METH_FASTCALL, py_load_vtf_texture_fn_doc},
        {"version",          CPF(mod_get_version),     METH_FASTCALL, mod_version_doc},
        {nullptr, nullptr, 0,                                         nullptr}
};


static struct PyModuleDef vtf_module_def = {
        PyModuleDef_HEAD_INIT,
        "pylib.vtf",
        "SourceIO vtf module",
        -1,
        vtf_methods,
};

static PyObject *VTFModule_Init(PyObject *parent_module) {
    PyObject *vtf_module = PyModule_Create(&vtf_module_def);
    if (!vtf_module) {
        Py_DECREF(vtf_module);
        return nullptr;
    }

    if (PyModule_AddObjectRef(parent_module, "vtf", vtf_module) < 0) {
        Py_DECREF(vtf_module);
        Py_DECREF(parent_module);
        return nullptr;
    }

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_SetItemString(modules, "pylib.vtf", vtf_module) < 0) {
        Py_DECREF(vtf_module);
        Py_DECREF(parent_module);
        return nullptr;
    }

    PyObject* t = PyType_FromSpec(&vtf_class_spec);
    if (!t) return nullptr;
    if (PyModule_AddObject(vtf_module, "VTFFile", t) < 0) {
        Py_DECREF(t);
        return nullptr;
    }
    /* expose a couple enum constants for convenience */
    PyModule_AddIntConstant(vtf_module, "IMAGE_FORMAT_RGBA8888", (int)IMAGE_FORMAT_RGBA8888);
    PyModule_AddIntConstant(vtf_module, "MIPMAP_FILTER_BOX", (int)MIPMAP_FILTER_BOX);
    PyModule_AddIntConstant(vtf_module, "SHARPEN_FILTER_NONE", (int)SHARPEN_FILTER_NONE);


    return vtf_module;
}

#endif //PYLIB_VTF_MODULE_H
