//
// Created by MED45 on 19.03.2022.
//

#ifndef PYLIB_LZ4_MODULE_HPP
#define PYLIB_LZ4_MODULE_HPP

#define PY_SSIZE_T_CLEAN

#include "py_helpers.h"
#include <Python.h>
#include <cstdint>
#include <structmember.h>
#include <utils.hpp>
#include <lz4.h>

typedef struct {
    PyObject_HEAD;
    uint32_t block_size;
    uint32_t extra_blocks;
    uint32_t output_length;
    uint32_t output_index;
    uint8_t* output_buffer;
    LZ4_streamDecode_t* stream_state;
} LZ4ChainDecoder;

PyMODINIT_FUNC lz4_submodule_init(void);

PyObject* module_lz4_decompress(PyObject* self, PyObject* args);

void LZ4ChainDecoder_dealloc(LZ4ChainDecoder* self);

PyObject* LZ4ChainDecoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds);

int LZ4ChainDecoder_init(LZ4ChainDecoder* self, PyObject* args, PyObject* kwds);

PyObject* LZ4ChainDecoder_decompress(LZ4ChainDecoder* self, PyObject* args);


static PyMethodDef lz4_methods[] = {{"decompress", module_lz4_decompress, METH_VARARGS, "Decompress lz4 compressed buffer"},
                                    {nullptr,      nullptr, 0,                          nullptr}};

static PyModuleDef lz4_module = {PyModuleDef_HEAD_INIT, "pylib.lz4", nullptr,
                                 -1, lz4_methods};

static PyMemberDef LZ4ChainDecoder_members[] = {
        {"block_size",   T_INT, (Py_ssize_t) offsetof(LZ4ChainDecoder, block_size),   0, "block size"},
        {"extra_blocks", T_INT, (Py_ssize_t) offsetof(LZ4ChainDecoder, extra_blocks), 0, "extra blocks"},
        {nullptr} /* Sentinel */
};

static PyMethodDef LZ4ChainDecoder_methods[] = {
        {"decompress", (PyCFunction) LZ4ChainDecoder_decompress, METH_VARARGS, "Decompress chain block"},
        {nullptr} /* Sentinel */
};

static PyType_Slot LZ4ChainDecoder_Type_slots[] = {
        {Py_tp_dealloc, (void*) LZ4ChainDecoder_dealloc},
        {Py_tp_methods, (void*) LZ4ChainDecoder_methods},
        {Py_tp_members, (void*) LZ4ChainDecoder_members},
        {Py_tp_init,    (void*) LZ4ChainDecoder_init},
        {Py_tp_new,     (void*) LZ4ChainDecoder_new},
        {Py_tp_doc,     (void*) "LZ4 chain decoder"},
        {0,             nullptr},
};

static PyType_Spec LZ4ChainDecoderTypeSpec{
        .name = "pylib.lz4.LZ4ChainDecoder",
        .basicsize = sizeof(LZ4ChainDecoder),
        .itemsize = 0,
        .flags = Py_TPFLAGS_DEFAULT,
        .slots = LZ4ChainDecoder_Type_slots,
};

#endif // PYLIB_LZ4_MODULE_HPP
