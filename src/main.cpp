#include <compressed_index_buffer.h>
#include <compressed_vertex_buffer.h>
#pragma clang diagnostic push

#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#include <Python.h>
#include <lz4.h>
#include <texture_compression.h>
#include <vector>
extern "C" {

PyObject *module_lz4_decompress(PyObject *self, PyObject *args);
PyObject *module_decode_vertex_buffer(PyObject *self, PyObject *args);
PyObject *module_decode_index_buffer(PyObject *self, PyObject *args);

static PyMethodDef methods[] = {
    {"lz4_decompress", module_lz4_decompress, METH_VARARGS, nullptr},
    {"decode_vertex_buffer", module_decode_vertex_buffer, METH_VARARGS,
     nullptr},
    {"decode_index_buffer", module_decode_index_buffer, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

static PyModuleDef module = {PyModuleDef_HEAD_INIT, "pylib", nullptr, -1,
                             methods};

PyMODINIT_FUNC PyInit_pylib(void) {
  PyObject *m = PyModule_Create(&module);
  if (m == nullptr)
    return nullptr;

  PyObject *m_t = PyModule_Create(&texture_module);
  Py_XINCREF(m_t);
  if (PyModule_AddObject(m, "texture", m_t) < 0) {
    Py_XDECREF(m_t);
    Py_CLEAR(m_t);
    Py_DECREF(m);
    return nullptr;
  }

  return m;
}
}

PyObject *module_lz4_decompress(PyObject *self, PyObject *args) {
  (void)self;
  PyObject *src;
  int32_t comp_size;
  int32_t decomp_size;

  if (!PyArg_ParseTuple(args, "Sii", &src, &comp_size, &decomp_size)) {
    return nullptr;
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
  (void)self;
  PyObject *src;
  int32_t data_size;
  int32_t size;
  int32_t count;

  if (!PyArg_ParseTuple(args, "Siii", &src, &data_size, &size, &count)) {
    return nullptr;
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
  (void)self;
  PyObject *src;
  int32_t data_size;
  int32_t size;
  int32_t count;

  if (!PyArg_ParseTuple(args, "Siii", &src, &data_size, &size, &count)) {
    return nullptr;
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

#pragma clang diagnostic pop