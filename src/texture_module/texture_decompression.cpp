//
// Created by MED45 on 08.03.2022.
//
#include <detex.h>
#include <texture_module.h>
#include "bc1.h"
#include "bc3.h"
#include "bc4.h"
#include "bc5.h"


#define CHECK_ERROR(error_code) \
    do{                            \
    switch (error_code) {\
        case INPUT_BUFFER_SMALL:\
            PyErr_Format(PyExc_ValueError, "Input buffer is to small");\
            return nullptr;\
        case OUTPUT_BUFFER_SMALL:\
            PyErr_Format(PyExc_ValueError, "Output buffer is to small");\
            return nullptr;\
        case NO_ERROR:\
            break;              \
    }                               \
}while(0)

#define CHECK_ASSERTS(src, dst, height, width) \
    do{                      \
        assert(src != nullptr);\
        assert(dst != nullptr);\
        assert(PyBytes_Size(dst) <= width * height * 4);\
    }while(0)

void image_flip(uint32_t *data, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height / 2; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t *a = &data[y * width + x];
            uint32_t *b = &data[(height - y - 1) * width + x];
            *a ^= *b;
            *b ^= *a;
            *a ^= *b;
        }
    }
}

void image_decompress(PyObject *src, PyObject *dst, int32_t width,
                      int32_t height, uint32_t fmt, uint32_t flip) {
    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);
    detexTexture texture;

    assert(src != nullptr);
    assert(dst != nullptr);
    assert(PyBytes_Size(dst) <= width * height * 4);
    printf("FMT: %x, %ix%i", fmt, width, height);
    texture.format = fmt;
    texture.data = src_buf;
    texture.width = width;
    texture.height = height;
    texture.width_in_blocks =
            (int32_t) (width / (detexGetCompressedBlockSize(fmt) / 2));
    texture.height_in_blocks =
            (int32_t) (height / (detexGetCompressedBlockSize(fmt) / 2));

    if (!detexDecompressTextureLinear(&texture, dst_buf,
                                      DETEX_PIXEL_FORMAT_RGBA8)) {
        PyErr_Format(PyExc_ValueError, "Buffer cannot be decompressed: %s",
                     detexGetErrorMessage());
    }

    if (flip) {
        image_flip((uint32_t *) dst_buf, width, height);
    }
}

PyObject *module_decompress(PyObject *args, uint32_t fmt) {
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);
    image_decompress(src, dst, width, height, fmt, flip);
    return dst;
}

PyObject *module_dxt1_decompress(PyObject *self, PyObject *args) {
    (void) self;
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);

    CHECK_ASSERTS(src, dst, width, height);

    ERROR_CODES error_code = decode_bc1_buffer(
            reinterpret_cast<char *>(src_buf), PyBytes_Size(src),
            reinterpret_cast<RGBA8888 *>(dst_buf), PyBytes_Size(dst),
            width, height
    );
    CHECK_ERROR(error_code);

    return dst;
}

PyObject *module_dxt5_decompress(PyObject *self, PyObject *args) {
    (void) self;
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);

    CHECK_ASSERTS(src, dst, width, height);

    ERROR_CODES error_code = decode_bc3_buffer(
            reinterpret_cast<char *>(src_buf), PyBytes_Size(src),
            reinterpret_cast<RGBA8888 *>(dst_buf), PyBytes_Size(dst),
            width, height
    );
    CHECK_ERROR(error_code);

    return dst;
}

PyObject *module_ati1_decompress(PyObject *self, PyObject *args) {
    (void) self;
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);

    CHECK_ASSERTS(src, dst, width, height);

    ERROR_CODES error_code = decode_bc4_buffer(
            reinterpret_cast<char *>(src_buf), PyBytes_Size(src),
            reinterpret_cast<RGBA8888 *>(dst_buf), PyBytes_Size(dst),
            width, height
    );
    CHECK_ERROR(error_code);

    return dst;
}

PyObject *module_bc7_decompress(PyObject *self, PyObject *args) {
    (void) self;
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);
    detexTexture texture;

    assert(src != nullptr);
    assert(dst != nullptr);
    assert(PyBytes_Size(dst) <= width * height * 4);

    texture.format = DETEX_TEXTURE_FORMAT_BPTC;
    texture.data = src_buf;
    texture.width = width;
    texture.height = height;
    texture.width_in_blocks = (width + 3) / 4;
    texture.height_in_blocks = (height + 3) / 4;

    if (!detexDecompressTextureLinear(&texture, dst_buf,
                                      DETEX_PIXEL_FORMAT_RGBA8)) {
        PyErr_Format(PyExc_ValueError, "Buffer cannot be decompressed: %s",
                     detexGetErrorMessage());
    }

    if (flip) {
        image_flip((uint32_t *) dst_buf, width, height);
    }
    return dst;
}

PyObject *module_bc6_decompress(PyObject *self, PyObject *args) {
    (void) self;
    return module_decompress(args, DETEX_TEXTURE_FORMAT_BPTC_FLOAT);
}

PyObject *module_ati2_decompress(PyObject *self, PyObject *args) {
    (void) self;
    PyObject *src;
    PyObject *dst;
    int32_t width;
    int32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
        return nullptr;
    }

    dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


    auto *src_buf = (uint8_t *) PyBytes_AsString(src);
    auto *dst_buf = (uint8_t *) PyBytes_AsString(dst);

    CHECK_ASSERTS(src, dst, width, height);

    ERROR_CODES error_code = decode_bc5_buffer(
            reinterpret_cast<char *>(src_buf), PyBytes_Size(src),
            reinterpret_cast<RGBA8888 *>(dst_buf), PyBytes_Size(dst),
            width, height
    );
    CHECK_ERROR(error_code);

    return dst;
}