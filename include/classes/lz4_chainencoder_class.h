#ifndef PYLIB_LZ4_CHAINENCODER_CLASS_H
#define PYLIB_LZ4_CHAINENCODER_CLASS_H

#include <cstdint>
#include <Python.h>
#include "lz4.h"

typedef struct {
    PyObject_HEAD
    uint32_t block_size;
    uint32_t extra_blocks;
    uint32_t output_length;
    uint32_t output_index;
    uint8_t *output_buffer;
    LZ4_stream_t *stream_state;
} LZ4ChainEncoderObject;

extern PyType_Spec LZ4ChainEncoder_spec;

#endif //PYLIB_LZ4_CHAINENCODER_CLASS_H
