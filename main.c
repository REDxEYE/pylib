#include <Python.h>
#include <detex.h>

PyObject *module_dxt1_decompress(PyObject *, PyObject *);

PyObject *module_dxt5_decompress(PyObject *, PyObject *);

PyObject *module_ati1_decompress(PyObject *, PyObject *);

PyObject *module_ati2_decompress(PyObject *, PyObject *);

static PyMethodDef methods[] = {
        {"dxt1_decompress", module_dxt1_decompress, METH_VARARGS, NULL},
        {"dxt5_decompress", module_dxt5_decompress, METH_VARARGS, NULL},
        {"ati1_decompress", module_ati1_decompress, METH_VARARGS, NULL},
        {"ati2_decompress", module_ati2_decompress, METH_VARARGS, NULL},
        {NULL, NULL, 0,                                           NULL}
};

static PyModuleDef module = {
        PyModuleDef_HEAD_INIT,
        "pylib",
        NULL,
        -1,
        methods
};

static void
image_flip(uint32_t *data, uint32_t width, uint32_t height) {
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

static PyObject *
image_decompress(PyObject *src, PyObject *dst, uint32_t width, uint32_t height, uint32_t fmt, uint32_t flip) {
    uint8_t *src_buf = (uint8_t *) PyBytes_AsString(src);
    uint8_t *dst_buf = (uint8_t *) PyBytes_AsString(dst);
    detexTexture texture;

    assert(src != NULL);
    assert(dst != NULL);
    assert(PyBytes_Size(dst) <= width * height * 4);

    texture.format = fmt;
    texture.data = src_buf;
    texture.width = width;
    texture.height = height;
    texture.width_in_blocks = (int32_t) (width / (detexGetCompressedBlockSize(fmt) / 2));
    texture.height_in_blocks = (int32_t) (height / (detexGetCompressedBlockSize(fmt) / 2));

    if (!detexDecompressTextureLinear(&texture, dst_buf, DETEX_PIXEL_FORMAT_RGBA8)) {
        PyErr_Format(PyExc_ValueError, "Buffer cannot be decompressed: %s", detexGetErrorMessage());
    }

    if (flip) {
        image_flip((uint32_t *) dst_buf, width, height);
    }

    return dst;
}

static PyObject *
module_decompress(PyObject *args, uint32_t fmt) {
    PyObject *src;
    PyObject *dst;
    uint32_t width;
    uint32_t height;
    uint32_t flip;

    if (!PyArg_ParseTuple(args, "SIIp", &src, &width, &height, &flip)) {
        return NULL;
    }

    dst = PyBytes_FromStringAndSize(NULL, width * height * 4);

    return image_decompress(src, dst, width, height, fmt, flip);
}

PyObject *
module_dxt1_decompress(PyObject *self, PyObject *args) {
    return module_decompress(args, DETEX_TEXTURE_FORMAT_BC1);
}

PyObject *
module_dxt5_decompress(PyObject *self, PyObject *args) {
    return module_decompress(args, DETEX_TEXTURE_FORMAT_BC3);
}

PyObject *
module_ati1_decompress(PyObject *self, PyObject *args) {
    return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC1);
}

PyObject *
module_ati2_decompress(PyObject *self, PyObject *args) {
    return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC2);
}

__attribute__((dllexport))
PyObject *PyInit_pylib(void) {
    return PyModule_Create(&module);
}