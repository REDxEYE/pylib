#ifndef PYLIB_COMPRESSION_MODULE_H
#define PYLIB_COMPRESSION_MODULE_H

#include <Python.h>
#include "utils.h"
#include "lz4_chaindecoder_class.h"

PyObject *py_zstd_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_zstd_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_zstd_decompress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_zstd_compress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_lz4_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_lz4_decompress_continue(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_lz4_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyDoc_STRVAR(py_zstd_decompress_fn_doc,
             "zstd_decompress($module, /, data, decompressed_size)\n"
             "--\n"
             "\n"
             "Decompress data using zstd compression.\n"
);

PyDoc_STRVAR(py_zstd_compress_fn_doc,
             "zstd_compress($module, /, data, compression_level=3)\n"
             "--\n"
             "\n"
             "Compress data using zstd compression.\n"
             "The compression level can be between 1 and 22, default is 3.\n"
);

PyDoc_STRVAR(py_zstd_compress_stream_fn_doc,
             "zstd_compress_stream($module, /, data, compression_level=3)\n"
             "--\n"
             "\n"
             "Compress data using zstd compression in a streaming manner.\n"
             "The compression level can be between 1 and 22, default is 3.\n"
);

PyDoc_STRVAR(py_zstd_decompress_stream_fn_doc,
             "zstd_decompress_stream($module, /, data)\n"
             "--\n"
             "\n"
             "Decompress data using zstd compression in a streaming manner.\n"
);

PyDoc_STRVAR(py_lz4_decompress_fn_doc,
             "lz4_decompress($module, /, data, decompressed_size)\n"
             "--\n"
             "\n"
             "Decompress data using LZ4 compression.\n"
);

PyDoc_STRVAR(py_lz4_decompress_continue_fn_doc,
             "lz4_decompress_continue($module, /, context, data, decompressed_size)\n"
             "--\n"
             "\n"
             "Continue decompressing data using LZ4 compression.\n"
);

PyDoc_STRVAR(py_lz4_compress_fn_doc,
             "lz4_compress($module, /, data)\n"
             "--\n"
             "\n"
             "Compress data using LZ4 compression.\n"
);

static PyMethodDef compression_methods[] = {
        {"zstd_decompress",         CPF(py_zstd_decompress),         METH_FASTCALL, py_zstd_decompress_fn_doc},
        {"zstd_compress",           CPF(py_zstd_compress),           METH_FASTCALL, py_zstd_compress_fn_doc},
        {"zstd_compress_stream",    CPF(py_zstd_compress_stream),    METH_FASTCALL, py_zstd_compress_stream_fn_doc},
        {"zstd_decompress_stream",  CPF(py_zstd_decompress_stream),  METH_FASTCALL, py_zstd_decompress_stream_fn_doc},
        {"lz4_decompress",          CPF(py_lz4_decompress),          METH_FASTCALL, py_lz4_decompress_fn_doc},
        {"lz4_decompress_continue", CPF(py_lz4_decompress_continue), METH_FASTCALL, py_lz4_decompress_continue_fn_doc},
        {"lz4_compress",            CPF(py_lz4_compress),            METH_FASTCALL, py_lz4_compress_fn_doc},
        {nullptr, nullptr, 0,                                                       nullptr}
};

static struct PyModuleDef compression_module_def = {
        PyModuleDef_HEAD_INIT,
        "compression",
        "SourceIO compression module",
        -1,
        compression_methods
};

static PyObject *CompressionModule_Init(PyObject *parent_module) {
    PyObject *compression_module = PyModule_Create(&compression_module_def);
    if (!compression_module) {
        Py_DECREF(compression_module);
        return nullptr;
    }

    PyObject* t = PyType_FromSpec(&LZ4_spec);
    if (!t){
        Py_DECREF(compression_module);
        return nullptr;
    };
    if (PyModule_AddObject(compression_module, "LZ4ChainDecoder", t) < 0) {
        Py_DECREF(t);
        return nullptr;
    }

    if (PyModule_AddObjectRef(parent_module, "compression", compression_module) < 0) {
        Py_DECREF(compression_module);
        Py_DECREF(parent_module);
        return nullptr;
    }

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_SetItemString(modules, "pylib.compression", compression_module) < 0) {
        Py_DECREF(compression_module);
        Py_DECREF(parent_module);
        return nullptr;
    }
    return compression_module;
}

#endif //PYLIB_COMPRESSION_MODULE_H

