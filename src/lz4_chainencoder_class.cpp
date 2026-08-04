#include <cstdint>
#include <climits>
#include <cstring>
#include <vector>
#include "classes/lz4_chainencoder_class.h"
#include "utils/utils.h"

static inline long long round_up_ll(long long value, long long step) {
    return step <= 0 ? value : ((value + step - 1) / step) * step;
}

static inline int clamp_to_int_max(size_t v) {
    return v > (size_t)INT_MAX ? INT_MAX : (int)v;
}

static void prepare(LZ4ChainEncoderObject *self, size_t incoming_size) {
    if (self->output_index + incoming_size <= self->output_length) return;
    const size_t K64 = 64u * 1024u;
    size_t dict_start = (size_t)std::max((long long)self->output_index - (long long)K64, 0LL);
    size_t dict_size  = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    self->output_index = (uint32_t)dict_size;
}

static void load_dict(LZ4ChainEncoderObject *self) {
    const size_t K64 = 64u * 1024u;
    size_t dict_start = (size_t)std::max((long long)self->output_index - (long long)K64, 0LL);
    size_t dict_size  = self->output_index - dict_start;
    LZ4_loadDict(self->stream_state, (const char*)(self->output_buffer + dict_start), clamp_to_int_max(dict_size));
}

static bool append_plain(LZ4ChainEncoderObject *self, const uint8_t *src, size_t src_size) {
    if (src_size == 0) return true;
    if (self->output_index + src_size > self->output_length) return false;
    memmove(self->output_buffer + self->output_index, src, src_size);
    self->output_index += (uint32_t)src_size;
    return true;
}

PyObject* LZ4ChainEncoder_compress(LZ4ChainEncoderObject* self, PyObject* const* args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "compress(src, accel=1) takes 1 or 2 arguments");
        return nullptr;
    }
    PyROBytesView src_view(args[0]);
    if (!src_view) {
        PyErr_SetString(PyExc_TypeError, "src must be bytes-like");
        return nullptr;
    }
    int accel = 1;
    if (nargs == 2) {
        if (!PyLong_Check(args[1])) {
            PyErr_SetString(PyExc_TypeError, "accel must be an int");
            return nullptr;
        }
        long a = PyLong_AsLong(args[1]);
        if (PyErr_Occurred()) return nullptr;
        accel = (int)(a <= 0 ? 1 : a);
    }

    size_t src_size = (size_t)src_view.size();
    if (src_size > (size_t)INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "src too large");
        return nullptr;
    }

    prepare(self, src_size);
    load_dict(self);

    int cap = LZ4_compressBound((int)src_size);
    if (cap <= 0) {
        PyErr_SetString(PyExc_RuntimeError, "LZ4_compressBound failed");
        return nullptr;
    }

    std::vector<char> out((size_t)cap);
    int written = LZ4_compress_fast_continue(
            self->stream_state,
            (const char*)src_view.data(),
            out.data(),
            (int)src_size,
            cap,
            accel
    );
    if (written <= 0) {
        PyErr_SetString(PyExc_RuntimeError, "LZ4_compress_fast_continue failed");
        return nullptr;
    }

    if (!append_plain(self, (const uint8_t*)src_view.data(), src_size)) {
        PyErr_SetString(PyExc_RuntimeError, "dictionary buffer overflow");
        return nullptr;
    }

    return PyBytes_FromStringAndSize(out.data(), written);
}

int LZ4ChainEncoder_init(LZ4ChainEncoderObject *self, PyObject *args, PyObject *kwds) {
    static const char *kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size_in = 0, extra_blocks_in = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", const_cast<char**>(kwlist), &block_size_in, &extra_blocks_in))
        return -1;
    const int K1 = 1024;
    self->block_size   = (uint32_t)round_up_ll(std::max(block_size_in, K1), K1);
    self->extra_blocks = (uint32_t)std::max(extra_blocks_in, 0);
    self->output_length = (uint32_t)((64u * 1024u) + (1u + self->extra_blocks) * self->block_size + 32u);
    self->output_index = 0;
    self->output_buffer = (uint8_t*)malloc((size_t)self->output_length + 8u);
    if (!self->output_buffer) {
        PyErr_NoMemory();
        return -1;
    }
    self->stream_state = LZ4_createStream();
    if (!self->stream_state) {
        free(self->output_buffer);
        self->output_buffer = nullptr;
        PyErr_SetString(PyExc_RuntimeError, "LZ4_createStream failed");
        return -1;
    }
    LZ4_resetStream(self->stream_state);
    return 0;
}

PyObject *LZ4ChainEncoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    auto *self = (LZ4ChainEncoderObject *) PyType_GenericAlloc(type, 0);
    if (!self) return nullptr;
    self->block_size = 0;
    self->extra_blocks = 0;
    self->output_length = 0;
    self->output_index = 0;
    self->output_buffer = nullptr;
    self->stream_state = nullptr;
    return (PyObject*)self;
}

void LZ4ChainEncoder_dealloc(LZ4ChainEncoderObject *self) {
    if (self->output_buffer) free(self->output_buffer);
    if (self->stream_state) LZ4_freeStream(self->stream_state);
    auto tp_free = (freefunc)PyType_GetSlot(Py_TYPE((PyObject *) self), Py_tp_free);
    tp_free((PyObject*)self);
}