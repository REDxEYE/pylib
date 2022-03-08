//
// Created by MED45 on 08.03.2022.
//
#include <texture_compression.h>
#include <detex.h>

static void image_flip(uint32_t *data, uint32_t width, uint32_t height) {
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

static void image_decompress(PyObject *src, PyObject *dst, int32_t width,
                             int32_t height, uint32_t fmt, uint32_t flip) {
  auto *src_buf = (uint8_t *)PyBytes_AsString(src);
  auto *dst_buf = (uint8_t *)PyBytes_AsString(dst);
  detexTexture texture;

  assert(src != nullptr);
  assert(dst != nullptr);
  assert(PyBytes_Size(dst) <= width * height * 4);

  texture.format = fmt;
  texture.data = src_buf;
  texture.width = width;
  texture.height = height;
  texture.width_in_blocks =
      (int32_t)(width / (detexGetCompressedBlockSize(fmt) / 2));
  texture.height_in_blocks =
      (int32_t)(height / (detexGetCompressedBlockSize(fmt) / 2));

  if (!detexDecompressTextureLinear(&texture, dst_buf,
                                    DETEX_PIXEL_FORMAT_RGBA8)) {
    PyErr_Format(PyExc_ValueError, "Buffer cannot be decompressed: %s",
                 detexGetErrorMessage());
  }

  if (flip) {
    image_flip((uint32_t *)dst_buf, width, height);
  }
}

static PyObject *module_decompress(PyObject *args, uint32_t fmt) {
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
  (void)self;
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BC1);
}

PyObject *module_dxt5_decompress(PyObject *self, PyObject *args) {
  (void)self;
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BC3);
}

PyObject *module_ati1_decompress(PyObject *self, PyObject *args) {
  (void)self;
  return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC1);
}

PyObject *module_bc7_decompress(PyObject *self, PyObject *args) {
  (void)self;
  PyObject *src;
  PyObject *dst;
  int32_t width;
  int32_t height;
  uint32_t flip;

  if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
    return nullptr;
  }

  dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


  auto *src_buf = (uint8_t *)PyBytes_AsString(src);
  auto *dst_buf = (uint8_t *)PyBytes_AsString(dst);
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
    image_flip((uint32_t *)dst_buf, width, height);
  }
  return dst;
}

PyObject *module_bc6_decompress(PyObject *self, PyObject *args) {
  (void)self;
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BPTC_FLOAT);
}

PyObject *module_ati2_decompress(PyObject *self, PyObject *args) {
  (void)self;
  return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC2);
}