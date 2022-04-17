
#include <lz4_module.h>
#include <lz4.h>
#include <string>

PyObject* module_lz4_decompress(PyObject* self, PyObject* args) {
    (void) self;
    PyObject* src;
    int32_t comp_size;
    int32_t decomp_size;


    if (!PyArg_ParseTuple(args, "Sii", &src, &comp_size, &decomp_size)) {
        return nullptr;
    }
    PyObject* dst = PyBytes_FromStringAndSize(nullptr, decomp_size);
    int32_t real_decompressed_size = LZ4_decompress_safe(PyBytes_AsString(src), PyBytes_AsString(dst),
                                                         comp_size, decomp_size);
    if (real_decompressed_size <= 0) {
        switch (real_decompressed_size) {
            case -1:
                PyErr_Format(PyExc_ValueError, "Input buffer is None or empty");
                break;
            case 0:
                PyErr_Format(PyExc_ValueError, "Empty output buffer");
                break;
            default:
                PyErr_Format(PyExc_ValueError, "Overflow error");
                break;
        }
    }
    if (real_decompressed_size != decomp_size) {
        PyErr_Format(PyExc_ValueError, "Failed to decompress buffer");
    }
    return dst;
}

PyObject* lz4_submodule_init(void) {
    PyObject* m, * lz4cd;
    lz4cd = PyType_FromSpec(&LZ4ChainDecoderTypeSpec);
    if (lz4cd == nullptr)
        return nullptr;

    m = PyModule_Create(&lz4_module);
    ADD_OBJECT(m, lz4cd, "LZ4ChainDecoder");

    if (m == nullptr)
        return nullptr;

    return m;
}

void prepare(LZ4ChainDecoder* self, size_t block_size) {
    if (self->output_index + block_size <= self->output_length)
        return;
    size_t dict_start = MAX(self->output_index - (1024 * 64), 0);
    size_t dict_size = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    LZ4_setStreamDecode(self->stream_state, (const char*) self->output_buffer, (int) dict_size);
    self->output_index = dict_size;
}

ssize_t decode(LZ4ChainDecoder* self, const uint8_t* src, size_t src_size, size_t block_size) {
    if (block_size <= 0) {
        block_size = self->block_size;
    }
    prepare(self, block_size);
    uint8_t* tmp = self->output_buffer + self->output_index;
    ssize_t decoded_size = LZ4_decompress_safe_continue(self->stream_state, (const char*) src, (char*) tmp,
                                                        (int) src_size, (int) block_size);
    if (decoded_size > 0) {
        self->output_index += decoded_size;
    }
    return decoded_size;
}

bool drain(LZ4ChainDecoder* self, uint8_t* dst, ssize_t offset, size_t size) {
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

bool decode_and_drain(LZ4ChainDecoder* self, const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size) {
    ssize_t decoded = decode(self, src, src_size, dst_size);
    if (decoded <= 0 || dst_size < decoded) {
        PyErr_Format(PyExc_ValueError, "Received error code from LZ4: %lli", decoded);
        return false;
    }
    return drain(self, dst, -decoded, decoded);
};


PyObject* LZ4ChainDecoder_decompress(LZ4ChainDecoder* self, PyObject* args) {
    PyObject* src;
    uint32_t decompressed_size;
    if (!PyArg_ParseTuple(args, "Si", &src, &decompressed_size)) {
        return nullptr;
    }
    PyObject* dst = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    auto* src_buf = (uint8_t*) PyBytes_AsString(src);
    auto* dst_buf = (uint8_t*) PyBytes_AsString(dst);

    if (!decode_and_drain(self, src_buf, PyBytes_Size(src), dst_buf, decompressed_size)) {
        return nullptr;
    }
    return dst;
}

int LZ4ChainDecoder_init(LZ4ChainDecoder* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"block_size", "extra_blocks", nullptr};
    int block_size;
    int extra_blocks;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "ii", kwlist, &block_size, &extra_blocks))
        return -1;
    self->block_size = round_up(MAX(block_size, 1024), 1024);
    self->extra_blocks = MAX(extra_blocks, 0);
    self->output_length = (1024 * 64) + (1 + self->extra_blocks) * self->block_size + 32;
    self->output_index = 0;

    self->output_buffer = (uint8_t*) malloc(self->output_length + 8);
    memset(self->output_buffer, 0, self->output_length + 8);
    self->stream_state = LZ4_createStreamDecode();
    return 0;
}

PyObject* LZ4ChainDecoder_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    LZ4ChainDecoder* self;
    self = (LZ4ChainDecoder*) PyType_GenericAlloc(type, 0);
    return (PyObject*) self;
}

void LZ4ChainDecoder_dealloc(LZ4ChainDecoder* self) {
    free(self->output_buffer);
    LZ4_freeStreamDecode(self->stream_state);
    freefunc(PyType_GetSlot(Py_TYPE(self), Py_tp_free))(self);
}
