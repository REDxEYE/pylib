#include "compressed_index_buffer.h"
#include "compressed_vertex_buffer.h"
#include <Python.h>
#include <detex.h>
#include <lz4.h>
#include <vector>

PyObject *module_bc7_decompress(PyObject *, PyObject *);
PyObject *module_bc6_decompress(PyObject *, PyObject *);
PyObject *module_dxt1_decompress(PyObject *, PyObject *);

PyObject *module_dxt5_decompress(PyObject *, PyObject *);

PyObject *module_ati1_decompress(PyObject *, PyObject *);

PyObject *module_ati2_decompress(PyObject *, PyObject *);

PyObject *module_lz4_decompress(PyObject *self, PyObject *args);

PyObject *module_decode_vertex_buffer(PyObject *self, PyObject *args);
PyObject *module_decode_index_buffer(PyObject *self, PyObject *args);
extern "C" {
static PyMethodDef methods[] = {
    {"bc7_decompress", module_bc7_decompress, METH_VARARGS, nullptr},
    {"bc6_decompress", module_bc6_decompress, METH_VARARGS, nullptr},
    {"dxt1_decompress", module_dxt1_decompress, METH_VARARGS, nullptr},
    {"dxt5_decompress", module_dxt5_decompress, METH_VARARGS, nullptr},
    {"ati1_decompress", module_ati1_decompress, METH_VARARGS, nullptr},
    {"ati2_decompress", module_ati2_decompress, METH_VARARGS, nullptr},
    {"lz4_decompress", module_lz4_decompress, METH_VARARGS, nullptr},
    {"decode_vertex_buffer", module_decode_vertex_buffer, METH_VARARGS,
     nullptr},
    {"decode_index_buffer", module_decode_index_buffer, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

static PyModuleDef module = {PyModuleDef_HEAD_INIT, "pylib", nullptr, -1,
                             methods};

__attribute__((dllexport)) PyObject *PyInit_pylib(void) {
  return PyModule_Create(&module);
}
}
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
  uint8_t *src_buf = (uint8_t *)PyBytes_AsString(src);
  uint8_t *dst_buf = (uint8_t *)PyBytes_AsString(dst);
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
    Py_RETURN_NONE;
  }

  dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);
  image_decompress(src, dst, width, height, fmt, flip);
  return dst;
}

PyObject *module_dxt1_decompress(PyObject *self, PyObject *args) {
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BC1);
}

PyObject *module_dxt5_decompress(PyObject *self, PyObject *args) {
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BC3);
}

PyObject *module_ati1_decompress(PyObject *self, PyObject *args) {
  return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC1);
}

PyObject *module_bc7_decompress(PyObject *self, PyObject *args) {
  PyObject *src;
  PyObject *dst;
  int32_t width;
  int32_t height;
  uint32_t flip;

  if (!PyArg_ParseTuple(args, "Siip", &src, &width, &height, &flip)) {
    Py_RETURN_NONE;
  }

  dst = PyBytes_FromStringAndSize(nullptr, width * height * 4);


  uint8_t *src_buf = (uint8_t *)PyBytes_AsString(src);
  uint8_t *dst_buf = (uint8_t *)PyBytes_AsString(dst);
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
  return module_decompress(args, DETEX_TEXTURE_FORMAT_BPTC_FLOAT);
}

PyObject *module_ati2_decompress(PyObject *self, PyObject *args) {
  return module_decompress(args, DETEX_TEXTURE_FORMAT_RGTC2);
}

PyObject *module_lz4_decompress(PyObject *self, PyObject *args) {

  PyObject *src;
  int32_t comp_size;
  int32_t decomp_size;

  if (!PyArg_ParseTuple(args, "Sii", &src, &comp_size, &decomp_size)) {
    Py_RETURN_NONE;
  }

  PyObject *dst = PyBytes_FromStringAndSize(nullptr, decomp_size);
  char *src_buf = PyBytes_AsString(src);
  char *dst_buf = PyBytes_AsString(dst);
  int32_t real_decompressed_size =
      LZ4_decompress_safe(src_buf, dst_buf, comp_size, decomp_size);
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

PyObject *module_decode_vertex_buffer(PyObject *self, PyObject *args) {
  PyObject *src;
  int32_t data_size;
  int32_t size;
  int32_t count;

  if (!PyArg_ParseTuple(args, "Siii", &src, &data_size, &size, &count)) {
    Py_RETURN_NONE;
  }
  std::vector<uint8_t> buffer;
  buffer.resize(data_size);
  memcpy(buffer.data(), PyBytes_AsString(src), data_size);
  std::vector<uint8_t> decoded =
      MeshOptimizerVertexDecoder::DecodeVertexBuffer(count, size, buffer);
  auto buffer_size = (Py_ssize_t)(decoded.size() * sizeof(decoded.front()));
  PyObject *dst = PyBytes_FromStringAndSize(nullptr, buffer_size);
  memcpy(PyBytes_AsString(dst), decoded.data(), buffer_size);
  return dst;
}

PyObject *module_decode_index_buffer(PyObject *self, PyObject *args) {
  PyObject *src;
  int32_t data_size;
  int32_t size;
  int32_t count;

  if (!PyArg_ParseTuple(args, "Siii", &src, &data_size, &size, &count)) {
    Py_RETURN_NONE;
  }

  std::vector<uint8_t> buffer;
  buffer.resize(data_size);
  memcpy(buffer.data(), PyBytes_AsString(src), data_size);
  std::vector<uint8_t> decoded =
      MeshOptimizerIndexDecoder::DecodeIndexBuffer(count, size, buffer);
  auto buffer_size = (Py_ssize_t)(decoded.size() * sizeof(decoded.front()));
  PyObject *dst = PyBytes_FromStringAndSize(nullptr, buffer_size);
  memcpy(PyBytes_AsString(dst), decoded.data(), buffer_size);
  return dst;
}
