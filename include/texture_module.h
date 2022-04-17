//
// Created by MED45 on 08.03.2022.
//

#ifndef PYLIB_TEXTURE_MODULE_H
#define PYLIB_TEXTURE_MODULE_H

#include <Python.h>



PyObject *module_bc7_decompress(PyObject *, PyObject *);
PyObject *module_bc6_decompress(PyObject *, PyObject *);
PyObject *module_dxt1_decompress(PyObject *, PyObject *);
PyObject *module_dxt5_decompress(PyObject *, PyObject *);
PyObject *module_ati1_decompress(PyObject *, PyObject *);
PyObject *module_ati2_decompress(PyObject *, PyObject *);

static PyMethodDef texture_methods[] = {
    {"bc7_decompress", module_bc7_decompress, METH_VARARGS,
     "Decode BC7 compressed texture"},
    {"bc6_decompress", module_bc6_decompress, METH_VARARGS,
     "Decode BC6 compressed texture"},
    {"dxt1_decompress", module_dxt1_decompress, METH_VARARGS,
     "Decode DXT1/BC1 compressed texture"},
    {"dxt5_decompress", module_dxt5_decompress, METH_VARARGS,
     "Decode DXT5/BC3 compressed texture"},
    {"ati1_decompress", module_ati1_decompress, METH_VARARGS,
     "Decode ATI1 compressed texture"},
    {"ati2_decompress", module_ati2_decompress, METH_VARARGS,
     "Decode ATI2 compressed texture"},
    {nullptr, nullptr, 0, nullptr}};

static PyModuleDef texture_module = {PyModuleDef_HEAD_INIT, "pylib.texture",
                                     nullptr, -1, texture_methods};

#endif // PYLIB_TEXTURE_MODULE_H
