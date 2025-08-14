#ifndef PYLIB_LZ4_CHAINDECODER_CLASS_H
#define PYLIB_LZ4_CHAINDECODER_CLASS_H

#include <cstdint>
#include <Python.h>
#include <structmember.h>
#include "lz4.h"
#include "lz4hc.h"


typedef struct {
    PyObject_HEAD
    uint32_t block_size;
    uint32_t extra_blocks;
    uint32_t output_length;
    uint32_t output_index;
    uint8_t *output_buffer;
    LZ4_streamDecode_t *stream_state;
} LZ4ChainDecoderObject;


PyDoc_STRVAR(LZ4ChainDecoder_new_doc,
             "LZ4ChainDecoder($type, /, block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Create a decoder with given block size and number of extra blocks.\n");

PyDoc_STRVAR(LZ4ChainDecoder_decompress_doc,
             "decompress($self, /, src, block_size)\n"
             "--\n"
             "\n"
             "Decompress one chain block from 'src' into an output buffer of size 'block_size' and return bytes.\n");

PyDoc_STRVAR(LZ4ChainDecoder_type_doc,
             "LZ4ChainDecoder(block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Chain decoder for LZ4 blocks. Call decompress(src, block_size) to get bytes.\n");


PyObject *LZ4ChainDecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);

int LZ4ChainDecoder_init(LZ4ChainDecoderObject *self, PyObject *args, PyObject *kwds);

void LZ4ChainDecoder_dealloc(LZ4ChainDecoderObject *self);

PyObject *LZ4ChainDecoder_decompress(LZ4ChainDecoderObject *self, PyObject *const *args, Py_ssize_t nargs);

static PyMemberDef LZ4ChainDecoder_members[] = {
        {"block_size",   T_INT, (Py_ssize_t) offsetof(LZ4ChainDecoderObject, block_size),   0, "block size"},
        {"extra_blocks", T_INT, (Py_ssize_t) offsetof(LZ4ChainDecoderObject, extra_blocks), 0, "extra blocks"},
        {nullptr} /* Sentinel */
};

static PyMethodDef LZ4ChainDecoder_methods[] = {
        {"decompress", (PyCFunction) LZ4ChainDecoder_decompress, METH_FASTCALL, LZ4ChainDecoder_decompress_doc},
        {nullptr,      nullptr, 0,                                              nullptr}
};

static PyType_Slot LZ4ChainDecoder_slots[] = {
        {Py_tp_new,     (void *) LZ4ChainDecoder_new},
        {Py_tp_init,    (void *) LZ4ChainDecoder_init},
        {Py_tp_dealloc, (void *) LZ4ChainDecoder_dealloc},
        {Py_tp_methods, (void *) LZ4ChainDecoder_methods},
        {Py_tp_doc,     (void *) LZ4ChainDecoder_type_doc},
        {0,             nullptr}
};

static PyType_Spec LZ4_spec = {
        "pylib.compresssion.LZ4ChainDecoder",
        sizeof(LZ4ChainDecoderObject),
        0,
        Py_TPFLAGS_DEFAULT,
        LZ4ChainDecoder_slots
};

#endif //PYLIB_LZ4_CHAINDECODER_CLASS_H
