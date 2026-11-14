#include <algorithm>
#include <cstdint>
#include <climits>
#include <cstring>
#include "classes/lz4_chainencoder_class.h"
#include <structmember.h>
#include "utils/utils.h"

static inline long long round_up_ll(long long value, long long step) {
    return step <= 0 ? value : ((value + step - 1) / step) * step;
}

//! Make room for \p incoming_size bytes after the write cursor, compacting the
//! buffer down to the last 64 KiB of history if needed. Returns true if the
//! history moved, in which case the stream's dictionary must be reloaded.
static bool prepare(LZ4ChainEncoderObject *self, size_t incoming_size) {
    if (self->output_index + incoming_size <= self->output_length) return false;
    const size_t K64 = 64u * 1024u;
    size_t dict_start = (size_t)std::max((long long)self->output_index - (long long)K64, 0LL);
    size_t dict_size  = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    self->output_index = (uint32_t)dict_size;
    return true;
}

static PyObject* LZ4ChainEncoder_compress(LZ4ChainEncoderObject* self, PyObject* const* args, Py_ssize_t nargs) {
    // tp_new leaves the buffers NULL; only __init__ allocates them.
    if (!self->output_buffer || !self->stream_state) {
        PyErr_SetString(PyExc_RuntimeError, "LZ4ChainEncoder is not initialized");
        return nullptr;
    }
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
        accel = a <= 0 ? 1 : (a > INT_MAX ? INT_MAX : (int)a);
    }

    size_t src_size = (size_t)src_view.size();
    if (src_size > (size_t)LZ4_MAX_INPUT_SIZE) {
        PyErr_Format(PyExc_ValueError, "src too large for LZ4: %zu bytes", src_size);
        return nullptr;
    }
    // Reject before touching any state: prepare() keeps up to 64 KiB of history,
    // so a block larger than the remaining capacity can never be buffered.
    if (src_size > (size_t)self->output_length - 64u * 1024u) {
        PyErr_Format(PyExc_ValueError, "src of %zu bytes exceeds encoder capacity %zu", src_size,
                     (size_t)self->output_length - 64u * 1024u);
        return nullptr;
    }

    int cap = LZ4_compressBound((int)src_size);
    if (cap <= 0 && src_size != 0) {
        PyErr_SetString(PyExc_RuntimeError, "LZ4_compressBound failed");
        return nullptr;
    }
    // Allocated through PyBytes so a huge bound raises MemoryError instead of
    // std::bad_alloc escaping into the interpreter.
    PyObject* out = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t)cap);
    if (!out) return nullptr;

    // Ring-buffer pattern: the block is copied into the history buffer and
    // compressed from there, so it directly follows the previous block and LZ4
    // keeps using it as a prefix dictionary. The dictionary only has to be
    // reloaded when prepare() compacted (moved) the history.
    if (prepare(self, src_size)) {
        LZ4_loadDict(self->stream_state, (const char*)self->output_buffer, (int)self->output_index);
    }
    char* block = (char*)(self->output_buffer + self->output_index);
    if (src_size) memcpy(block, src_view.data(), src_size);

    int written = LZ4_compress_fast_continue(
            self->stream_state,
            block,
            PyBytes_AsString(out),
            (int)src_size,
            cap,
            accel
    );
    if (written <= 0 && src_size != 0) {
        Py_DECREF(out);
        PyErr_SetString(PyExc_RuntimeError, "LZ4_compress_fast_continue failed");
        return nullptr;
    }
    self->output_index += (uint32_t)src_size;

    if (written == cap) return out;
    // `_PyBytes_Resize` is not in the limited API; copy into an exact-size object.
    PyObject* res = PyBytes_FromStringAndSize(PyBytes_AsString(out), written);
    Py_DECREF(out);
    return res;
}

static int LZ4ChainEncoder_init(LZ4ChainEncoderObject *self, PyObject *args, PyObject *kwds) {
    static const char *kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size_in = 0, extra_blocks_in = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", const_cast<char**>(kwlist), &block_size_in, &extra_blocks_in))
        return -1;
    const int K1 = 1024;
    const auto block_size   = (uint32_t)round_up_ll(std::max(block_size_in, K1), K1);
    const auto extra_blocks = (uint32_t)std::max(extra_blocks_in, 0);
    const uint64_t output_length = 64ull * 1024ull + (1ull + extra_blocks) * block_size + 32ull;
    if (output_length > (uint64_t)UINT32_MAX - 8u) {
        PyErr_SetString(PyExc_ValueError, "block_size * (extra_blocks + 1) is too large");
        return -1;
    }
    // Allocate into locals and only swap on full success, so a failed re-init
    // leaves the previous state intact and a successful one frees it.
    auto *output_buffer = (uint8_t*)malloc((size_t)output_length + 8u);
    if (!output_buffer) {
        PyErr_NoMemory();
        return -1;
    }
    LZ4_stream_t *stream_state = LZ4_createStream();
    if (!stream_state) {
        free(output_buffer);
        PyErr_SetString(PyExc_RuntimeError, "LZ4_createStream failed");
        return -1;
    }
    LZ4_resetStream(stream_state);

    free(self->output_buffer);
    if (self->stream_state) LZ4_freeStream(self->stream_state);
    self->block_size    = block_size;
    self->extra_blocks  = extra_blocks;
    self->output_length = (uint32_t)output_length;
    self->output_index  = 0;
    self->output_buffer = output_buffer;
    self->stream_state  = stream_state;
    return 0;
}

static PyObject *LZ4ChainEncoder_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
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

static void LZ4ChainEncoder_dealloc(LZ4ChainEncoderObject *self) {
    if (self->output_buffer) free(self->output_buffer);
    if (self->stream_state) LZ4_freeStream(self->stream_state);
    PyTypeObject *tp = Py_TYPE((PyObject *) self);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))((PyObject *) self);
    Py_DECREF(tp);
}

PyDoc_STRVAR(LZ4ChainEncoder_compress_doc,
             "compress($self, src, accel=1, /)\n"
             "--\n"
             "\n"
             "Compress one block 'src' as a dependent LZ4 block using the rolling 64 KiB dictionary.\n"
             "Returns compressed bytes. 'accel' is the LZ4 acceleration (values <= 0 mean 1;\n"
             "higher is faster but compresses less). Raises ValueError if 'src' exceeds the\n"
             "encoder capacity.\n");

PyDoc_STRVAR(LZ4ChainEncoder_type_doc,
             "LZ4ChainEncoder(block_size, extra_blocks)\n"
             "--\n"
             "\n"
             "Chain encoder for raw LZ4 blocks with a dependent-block dictionary; output is\n"
             "decodable by LZ4ChainDecoder with the same parameters.\n"
             "block_size is rounded up to a multiple of 1024 (minimum 1024).\n");

static PyMemberDef LZ4ChainEncoder_members[] = {
        {"block_size",   T_UINT, (Py_ssize_t) offsetof(LZ4ChainEncoderObject, block_size),   READONLY, "block size"},
        {"extra_blocks", T_UINT, (Py_ssize_t) offsetof(LZ4ChainEncoderObject, extra_blocks), READONLY, "extra blocks"},
        {nullptr}
};

static PyMethodDef LZ4ChainEncoder_methods[] = {
        {"compress", (PyCFunction) (void (*)(void)) LZ4ChainEncoder_compress, METH_FASTCALL,
         LZ4ChainEncoder_compress_doc},
        {nullptr, nullptr, 0, nullptr}
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

PyType_Spec LZ4ChainEncoder_spec = {
        "pylib.compression.LZ4ChainEncoder",
        sizeof(LZ4ChainEncoderObject),
        0,
        Py_TPFLAGS_DEFAULT,
        LZ4ChainEncoder_slots
};
