#include <algorithm>
#include <cstdint>
#include <climits>
#include <cstring>
#include "classes/lz4_chaindecoder_class.h"
#include <structmember.h>
#include "utils/utils.h"

static inline long long round_up_ll(long long value, long long step) {
    if (step <= 0) return value;
    const long long q = (value + step - 1) / step;
    return q * step;
}

static inline int clamp_to_int_max(size_t v) {
    return v > (size_t)INT_MAX ? INT_MAX : (int)v;
}

//! Make room for \p block_size bytes after the write cursor, compacting the
//! buffer down to the last 64 KiB of history if needed. Returns false if the
//! block cannot fit even after compaction.
static bool prepare(LZ4ChainDecoderObject* self, size_t block_size) {
    if ((size_t)self->output_index + block_size <= self->output_length) return true;
    const size_t k64 = 64u * 1024u;
    size_t dict_start = (size_t)std::max((long long)self->output_index - (long long)k64, 0LL);
    size_t dict_size  = self->output_index - dict_start;
    if (dict_size + block_size > self->output_length) return false;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    LZ4_setStreamDecode(self->stream_state, (const char*)self->output_buffer, clamp_to_int_max(dict_size));
    self->output_index = (uint32_t)dict_size;
    return true;
}

static Py_ssize_t decode(LZ4ChainDecoderObject* self, const uint8_t* src, size_t src_size, size_t block_size) {
    if (block_size == 0) block_size = self->block_size;
    if (!prepare(self, block_size)) {
        PyErr_Format(PyExc_ValueError, "block_size %zu exceeds decoder capacity %zu", block_size,
                     (size_t)self->output_length - 64u * 1024u);
        return -1;
    }
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
    if (decoded < 0) {
        PyErr_Format(PyExc_ValueError, "LZ4_decompress_safe_continue failed: %i", decoded);
        return -1;
    }
    self->output_index += (uint32_t)decoded;
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
    if (decoded < 0)
        return false;  // decode() set the exception
    if ((size_t)decoded > dst_size) {
        PyErr_SetString(PyExc_RuntimeError, "decoded size exceeds requested block_size");
        return false;
    }
    if (decoded == 0) return true;
    return drain(self, dst, -decoded, decoded);
}

static PyObject* LZ4ChainDecoder_decompress(LZ4ChainDecoderObject* self, PyObject* const* args, Py_ssize_t nargs) {
    // tp_new leaves the buffers NULL; only __init__ allocates them.
    if (!self->output_buffer || !self->stream_state) {
        PyErr_SetString(PyExc_RuntimeError, "LZ4ChainDecoder is not initialized");
        return nullptr;
    }
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

    Py_ssize_t requested = PyLong_AsSsize_t(args[1]);
    if (PyErr_Occurred()) return nullptr;
    if (requested < 0) {
        PyErr_Format(PyExc_ValueError, "block_size must not be negative, got %zd", requested);
        return nullptr;
    }
    // LZ4 takes int sizes; reject rather than silently clamp.
    if (requested > INT_MAX || data_view.size() > (Py_ssize_t)INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "src and block_size must not exceed INT_MAX bytes");
        return nullptr;
    }
    size_t decompressed_size = (size_t)requested;

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

static int LZ4ChainDecoder_init(LZ4ChainDecoderObject* self, PyObject* args, PyObject* kwds) {
    static const char* kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size_in = 0;
    int extra_blocks_in = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", const_cast<char**>(kwlist),
                                     &block_size_in, &extra_blocks_in))
        return -1;

    const int k1 = 1024;
    const auto block_size = (uint32_t)round_up_ll(std::max(block_size_in, k1), k1);
    const auto extra_blocks = (uint32_t)std::max(extra_blocks_in, 0);
    const uint64_t output_length = 64ull * 1024ull + (1ull + extra_blocks) * block_size + 32ull;
    if (output_length > (uint64_t)UINT32_MAX - 8u) {
        PyErr_SetString(PyExc_ValueError, "block_size * (extra_blocks + 1) is too large");
        return -1;
    }

    // Allocate into locals and only swap on full success, so a failed re-init
    // leaves the previous state intact and a successful one frees it.
    auto* output_buffer = (uint8_t*)malloc((size_t)output_length + 8u);
    if (!output_buffer) {
        PyErr_NoMemory();
        return -1;
    }
    LZ4_streamDecode_t* stream_state = LZ4_createStreamDecode();
    if (!stream_state) {
        free(output_buffer);
        PyErr_SetString(PyExc_RuntimeError, "LZ4_createStreamDecode failed");
        return -1;
    }
    LZ4_setStreamDecode(stream_state, nullptr, 0);

    free(self->output_buffer);
    if (self->stream_state) LZ4_freeStreamDecode(self->stream_state);
    self->block_size    = block_size;
    self->extra_blocks  = extra_blocks;
    self->output_length = (uint32_t)output_length;
    self->output_index  = 0;
    self->output_buffer = output_buffer;
    self->stream_state  = stream_state;
    return 0;
}

static PyObject* LZ4ChainDecoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
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

static void LZ4ChainDecoder_dealloc(LZ4ChainDecoderObject* self) {
    if (self->output_buffer) free(self->output_buffer);
    if (self->stream_state) LZ4_freeStreamDecode(self->stream_state);
    PyTypeObject* tp = Py_TYPE((PyObject *) self);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))((PyObject*) self);
    Py_DECREF(tp);
}

PyDoc_STRVAR(LZ4ChainDecoder_decompress_doc,
             "decompress($self, src, block_size, /)\n"
             "--\n"
             "\n"
             "Decompress one dependent LZ4 block from 'src' into a bytes object of length 'block_size'.\n"
             "Raises ValueError if the data is corrupt or a size exceeds INT_MAX or the decoder capacity.\n");

PyDoc_STRVAR(LZ4ChainDecoder_type_doc,
             "LZ4ChainDecoder(block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Chain decoder for dependent LZ4 blocks with a rolling 64 KiB dictionary.\n"
             "block_size is rounded up to a multiple of 1024 (minimum 1024).\n");

static PyMemberDef LZ4ChainDecoder_members[] = {
        {"block_size",   T_UINT, (Py_ssize_t) offsetof(LZ4ChainDecoderObject, block_size),   READONLY, "block size"},
        {"extra_blocks", T_UINT, (Py_ssize_t) offsetof(LZ4ChainDecoderObject, extra_blocks), READONLY, "extra blocks"},
        {nullptr}
};

static PyMethodDef LZ4ChainDecoder_methods[] = {
        {"decompress", (PyCFunction) (void (*)(void)) LZ4ChainDecoder_decompress, METH_FASTCALL,
         LZ4ChainDecoder_decompress_doc},
        {nullptr, nullptr, 0, nullptr}
};

static PyType_Slot LZ4ChainDecoder_slots[] = {
        {Py_tp_new,     (void *) LZ4ChainDecoder_new},
        {Py_tp_init,    (void *) LZ4ChainDecoder_init},
        {Py_tp_dealloc, (void *) LZ4ChainDecoder_dealloc},
        {Py_tp_methods, (void *) LZ4ChainDecoder_methods},
        {Py_tp_members, (void *) LZ4ChainDecoder_members},
        {Py_tp_doc,     (void *) LZ4ChainDecoder_type_doc},
        {0,             nullptr}
};

PyType_Spec LZ4ChainDecoder_spec = {
        "pylib.compression.LZ4ChainDecoder",
        sizeof(LZ4ChainDecoderObject),
        0,
        Py_TPFLAGS_DEFAULT,
        LZ4ChainDecoder_slots
};