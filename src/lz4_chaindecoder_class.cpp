#include <cstdint>
#include <climits>
#include <cstring>
#include "classes/lz4_chaindecoder_class.h"
#include "utils/utils.h"

static inline long long round_up_ll(long long value, long long step) {
    if (step <= 0) return value;
    const long long q = (value + step - 1) / step;
    return q * step;
}

static inline int clamp_to_int_max(size_t v) {
    return v > (size_t)INT_MAX ? INT_MAX : (int)v;
}

static void prepare(LZ4ChainDecoderObject* self, size_t block_size) {
    if (self->output_index + block_size <= self->output_length) return;
    const size_t k64 = 64u * 1024u;
    size_t dict_start = (size_t)std::max((long long)self->output_index - (long long)k64, 0LL);
    size_t dict_size  = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    LZ4_setStreamDecode(self->stream_state, (const char*)self->output_buffer, clamp_to_int_max(dict_size));
    self->output_index = (uint32_t)dict_size;
}

static Py_ssize_t decode(LZ4ChainDecoderObject* self, const uint8_t* src, size_t src_size, size_t block_size) {
    if (block_size == 0) block_size = self->block_size;
    prepare(self, block_size);
    uint8_t* dst = self->output_buffer + self->output_index;

    const int srcLen = clamp_to_int_max(src_size);
    const int maxDst = clamp_to_int_max(block_size);

    const int decoded = LZ4_decompress_safe_continue(
            self->stream_state,
            (const char*)src,
            (char*)dst,
            srcLen,
            maxDst
    );
    if (decoded > 0) self->output_index += (uint32_t)decoded;
    return decoded;
}

static bool drain(LZ4ChainDecoderObject* self, uint8_t* dst, Py_ssize_t offset, Py_ssize_t size) {
    offset += self->output_index;
    if (offset < 0 || size < 0 || (Py_ssize_t)self->output_index < offset + size) {
        PyErr_Format(PyExc_ValueError, "invalid slice: offset=%lli size=%lli total=%u",
                     (long long)offset, (long long)size, (unsigned)self->output_index);
        return false;
    }
    memmove(dst, self->output_buffer + offset, (size_t)size);
    return true;
}

static bool decode_and_drain(LZ4ChainDecoderObject* self, const uint8_t* src, size_t src_size,
                             uint8_t* dst, size_t dst_size) {
    Py_ssize_t decoded = decode(self, src, src_size, dst_size);
    if (decoded < 0) {
        PyErr_Format(PyExc_ValueError, "LZ4_decompress_safe_continue failed: %lli", (long long)decoded);
        return false;
    }
    if ((size_t)decoded > dst_size) {
        PyErr_SetString(PyExc_RuntimeError, "decoded size exceeds requested block_size");
        return false;
    }
    if (decoded == 0) return true;
    return drain(self, dst, -decoded, decoded);
}

PyObject* LZ4ChainDecoder_decompress(LZ4ChainDecoderObject* self, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "decompress(src, block_size) takes exactly 2 arguments");
        return nullptr;
    }
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        PyErr_SetString(PyExc_TypeError, "src must be bytes-like");
        return nullptr;
    }
    if (!PyLong_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError, "block_size must be an int");
        return nullptr;
    }

    unsigned long ul = PyLong_AsUnsignedLong(args[1]);
    if (PyErr_Occurred()) return nullptr;
    size_t decompressed_size = (size_t)ul;

    PyObject* dst = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t)decompressed_size);
    if (!dst) return nullptr;
    auto* dst_buf = (uint8_t*)PyBytes_AsString(dst);

    if (!decode_and_drain(self,
                          (const uint8_t*)data_view.data(), (size_t)data_view.size(),
                          dst_buf, decompressed_size)) {
        Py_DECREF(dst);
        return nullptr;
    }
    return dst;
}

int LZ4ChainDecoder_init(LZ4ChainDecoderObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size_in = 0;
    int extra_blocks_in = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", const_cast<char**>(kwlist),
                                     &block_size_in, &extra_blocks_in))
        return -1;

    const int k1 = 1024;
    self->block_size   = (uint32_t)round_up_ll(std::max(block_size_in, k1), k1);
    self->extra_blocks = (uint32_t)std::max(extra_blocks_in, 0);
    self->output_length = (uint32_t)((64u * 1024u) + (1u + self->extra_blocks) * self->block_size + 32u);
    self->output_index = 0;

    self->output_buffer = (uint8_t*)malloc((size_t)self->output_length + 8u);
    if (!self->output_buffer) {
        PyErr_NoMemory();
        return -1;
    }

    self->stream_state = LZ4_createStreamDecode();
    if (!self->stream_state) {
        free(self->output_buffer);
        self->output_buffer = nullptr;
        PyErr_SetString(PyExc_RuntimeError, "LZ4_createStreamDecode failed");
        return -1;
    }

    LZ4_setStreamDecode(self->stream_state, nullptr, 0);
    return 0;
}

PyObject* LZ4ChainDecoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    auto* self = (LZ4ChainDecoderObject*)PyType_GenericAlloc(type, 0);
    if (!self) return nullptr;
    self->block_size = 0;
    self->extra_blocks = 0;
    self->output_length = 0;
    self->output_index = 0;
    self->output_buffer = nullptr;
    self->stream_state = nullptr;
    return (PyObject*)self;
}

void LZ4ChainDecoder_dealloc(LZ4ChainDecoderObject* self) {
    if (self->output_buffer) free(self->output_buffer);
    if (self->stream_state) LZ4_freeStreamDecode(self->stream_state);
    auto tp_free = (freefunc) PyType_GetSlot(Py_TYPE(self), Py_tp_free);
    tp_free((PyObject *) self);
}