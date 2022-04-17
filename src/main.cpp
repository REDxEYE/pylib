#include <compressed_index_buffer.h>
#include <compressed_vertex_buffer.h>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

#include <Python.h>
#include <lz4.h>
#include <lz4_module.h>
#include <py_helpers.h>
#include <texture_module.h>
#include <vector>

extern "C" {

PyObject* module_decode_vertex_buffer(PyObject* self, PyObject* args);
PyObject* module_decode_index_buffer(PyObject* self, PyObject* args);

static PyMethodDef methods[] = {
        {"decode_vertex_buffer", module_decode_vertex_buffer, METH_VARARGS, nullptr},
        {"decode_index_buffer",  module_decode_index_buffer,  METH_VARARGS, nullptr},
        {nullptr,                nullptr, 0,                                nullptr}};

static PyModuleDef module = {PyModuleDef_HEAD_INIT, "pylib", nullptr, -1,
                             methods};

PyMODINIT_FUNC PyInit_pylib(void) {
    PyObject* m = PyModule_Create(&module);
    if (m == nullptr)
        return nullptr;

    PyObject* m_t = PyModule_Create(&texture_module);
    ADD_OBJECT(m, m_t, "texture");

    PyObject* m_lz = lz4_submodule_init();
    ADD_OBJECT(m, m_lz, "lz4");

    return m;
}
}

PyObject* module_decode_vertex_buffer(PyObject* self, PyObject* args) {
    (void) self;
    PyObject* src;
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
    auto buffer_size = (Py_ssize_t) (decoded.size() * sizeof(decoded.front()));
    PyObject* dst = PyBytes_FromStringAndSize(nullptr, buffer_size);
    memcpy(PyBytes_AsString(dst), decoded.data(), buffer_size);
    return dst;
}

PyObject* module_decode_index_buffer(PyObject* self, PyObject* args) {
    (void) self;
    PyObject* src;
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
    auto buffer_size = (Py_ssize_t) (decoded.size() * sizeof(decoded.front()));
    PyObject* dst = PyBytes_FromStringAndSize(nullptr, buffer_size);
    memcpy(PyBytes_AsString(dst), decoded.data(), buffer_size);
    return dst;
}

#pragma clang diagnostic pop