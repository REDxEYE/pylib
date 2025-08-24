#ifndef PYLIB_LZ4_CHAINENCODER_CLASS_H
#define PYLIB_LZ4_CHAINENCODER_CLASS_H
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
    LZ4_stream_t *stream_state;
} LZ4ChainEncoderObject;

PyDoc_STRVAR(LZ4ChainEncoder_new_doc,
             "LZ4ChainEncoder($type, /, block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Create an LZ4 dependent-block encoder with a rolling 64KiB dictionary.\n");

PyDoc_STRVAR(LZ4ChainEncoder_compress_doc,
             "compress($self, /, src, accel=1)\n"
             "--\n"
             "\n"
             "Compress one uncompressed block 'src' using the rolling dictionary.\n"
             "Returns compressed bytes. 'accel' is LZ4 acceleration (1=faster&denser, higher=faster&weaker).\n");

PyDoc_STRVAR(LZ4ChainEncoder_type_doc,
             "LZ4ChainEncoder(block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Chain encoder for raw LZ4 blocks with dependent-block dictionary.\n");

PyObject *LZ4ChainEncoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);
int LZ4ChainEncoder_init(LZ4ChainEncoderObject *self, PyObject *args, PyObject *kwds);
void LZ4ChainEncoder_dealloc(LZ4ChainEncoderObject *self);
PyObject *LZ4ChainEncoder_compress(LZ4ChainEncoderObject *self, PyObject *const *args, Py_ssize_t nargs);

static PyMemberDef LZ4ChainEncoder_members[] = {
        {"block_size",   T_INT, (Py_ssize_t) offsetof(LZ4ChainEncoderObject, block_size),   0, "block size"},
        {"extra_blocks", T_INT, (Py_ssize_t) offsetof(LZ4ChainEncoderObject, extra_blocks), 0, "extra blocks"},
        {nullptr}
};

static PyMethodDef LZ4ChainEncoder_methods[] = {
        {"compress", (PyCFunction) LZ4ChainEncoder_compress, METH_FASTCALL, LZ4ChainEncoder_compress_doc},
        {nullptr,    nullptr,                                  0,            nullptr}
};

static PyType_Slot LZ4ChainEncoder_slots[] = {
        {Py_tp_new,     (void *) LZ4ChainEncoder_new},
        {Py_tp_init,    (void *) LZ4ChainEncoder_init},
        {Py_tp_dealloc, (void *) LZ4ChainEncoder_dealloc},
        {Py_tp_methods, (void *) LZ4ChainEncoder_methods},
        {Py_tp_members, (void *) LZ4ChainEncoder_members},
        {Py_tp_doc,     (void *) LZ4ChainEncoder_type_doc},
        {0,             nullptr}
};

static PyType_Spec LZ4ChainEncoder_spec = {
        "pylib.compresssion.LZ4ChainEncoder",
        sizeof(LZ4ChainEncoderObject),
        0,
        Py_TPFLAGS_DEFAULT,
        LZ4ChainEncoder_slots
};
#endif //PYLIB_LZ4_CHAINENCODER_CLASS_H
