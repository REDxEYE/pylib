#include <cstdint>
#include "lz4_chaindecoder_class.h"
#include "utils.h"


void prepare(LZ4ChainDecoderObject *self, size_t block_size) {
    if (self->output_index + block_size <= self->output_length)
        return;
    size_t dict_start = std::max(self->output_index - (1024 * 64), 0u);
    size_t dict_size = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    LZ4_setStreamDecode(self->stream_state, (const char *) self->output_buffer, (int) dict_size);
    self->output_index = dict_size;
}

Py_ssize_t decode(LZ4ChainDecoderObject *self, const uint8_t *src, size_t src_size, size_t block_size) {
    if (block_size <= 0) {
        block_size = self->block_size;
    }
    prepare(self, block_size);
    uint8_t *tmp = self->output_buffer + self->output_index;
    Py_ssize_t decoded_size = LZ4_decompress_safe_continue(self->stream_state, (const char *) src, (char *) tmp,
                                                        (int) src_size, (int) block_size);
    if (decoded_size > 0) {
        self->output_index += decoded_size;
    }
    return decoded_size;
}

bool drain(LZ4ChainDecoderObject *self, uint8_t *dst, Py_ssize_t offset, Py_ssize_t size) {
    offset += self->output_index;
    if (offset < 0 || size < 0 || offset + size > self->output_index) {
        PyErr_Format(PyExc_ValueError,
                     "Invalid offset(%lli), size(%lli) or offset+size > %lli",
                     offset, size, self->output_index);
        return false;
    }
    memmove(dst, self->output_buffer + offset, size);
    return true;
}

bool decode_and_drain(LZ4ChainDecoderObject *self, const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size) {
    Py_ssize_t decoded = decode(self, src, src_size, dst_size);
    if (decoded <= 0 || dst_size < decoded) {
        PyErr_Format(PyExc_ValueError, "Received error code from LZ4: %lli", decoded);
        return false;
    }
    return drain(self, dst, -decoded, decoded);
};


PyObject* LZ4ChainDecoder_decompress(LZ4ChainDecoderObject* self, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "decompress(src, block_size) takes exactly 2 argument");
        return nullptr;
    }
    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "src must be bytes");
        return nullptr;
    }
    if (!PyLong_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError, "block_size must be an integer");
        return nullptr;
    }

    PyObject *src = args[0];
    uint32_t decompressed_size = PyLong_AsUnsignedLong(args[1]);

    PyObject *dst = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);

    if (!decode_and_drain(self, src_buf, PyBytes_Size(src), dst_buf, decompressed_size)) {
        return nullptr;
    }
    return dst;
}

inline long long round_up(long long value, long long step) {
    return (value + step - 1) / (step * step);
}

int LZ4ChainDecoder_init(LZ4ChainDecoderObject *self, PyObject *args, PyObject *kwds) {
    static const char *kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size;
    int extra_blocks;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", const_cast<char **>(kwlist), &block_size, &extra_blocks))
        return -1;
    self->block_size = round_up(std::max(block_size, 1024), 1024);
    self->extra_blocks = std::max(extra_blocks, 0);
    self->output_length = (1024 * 64) + (1 + self->extra_blocks) * self->block_size + 32;
    self->output_index = 0;

    self->output_buffer = (uint8_t *) malloc(self->output_length + 8);
    memset(self->output_buffer, 0, self->output_length + 8);
    self->stream_state = LZ4_createStreamDecode();
    return 0;
}

PyObject *LZ4ChainDecoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    LZ4ChainDecoderObject *self;
    self = (LZ4ChainDecoderObject *) PyType_GenericAlloc(type, 0);
    return (PyObject *) self;
}

void LZ4ChainDecoder_dealloc(LZ4ChainDecoderObject *self) {
    free(self->output_buffer);
    LZ4_freeStreamDecode(self->stream_state);
    freefunc(PyType_GetSlot(Py_TYPE(self), Py_tp_free))(self);
}