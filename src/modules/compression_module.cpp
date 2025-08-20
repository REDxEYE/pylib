#include "modules/compression_module.h"
#include <vector>
#include "utils/utils.h"

#include "zstd.h"
#include "lz4.h"

PyObject *py_zstd_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2)return PyErr_Format(PyExc_TypeError, "zstd_decompress() takes 2 positional args");
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        return type_error("data_view", "bytes", args[0]);
    }
    if (!PyLong_Check(args[1])) {
        return type_error("decompressed_size", "int", args[1]);
    }

    Py_ssize_t decompressed_size = PyLong_AsLongLong(args[1]);
    std::vector<uint8_t> decompressed_buffer(decompressed_size);
    size_t bytes_written = ZSTD_decompress(decompressed_buffer.data(), decompressed_size, data_view.data(),
                                           data_view.size());
    if (ZSTD_isError(bytes_written)) {
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %s", ZSTD_getErrorName(bytes_written));
    }

    PyObject *res = PyBytes_FromStringAndSize(reinterpret_cast<const char *>(decompressed_buffer.data()), bytes_written);
    return res;
}

PyObject *py_zstd_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1)
        return PyErr_Format(PyExc_TypeError, "zstd_compress() takes at least 1 positional arg");
    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data_view", "bytes", args[0]);
    int compression_level = 3;
    if (nargs > 1) {
        if (!PyLong_Check(args[1]))
            return type_error("compression_level", "int", args[1]);
        compression_level = PyLong_AsLong(args[1]);
        if (compression_level < 1 || compression_level > 22)
            return PyErr_Format(PyExc_ValueError, "Compression level must be between 1 and 22, got %d",
                                compression_level);
    }
    auto compressed_size = (Py_ssize_t) ZSTD_compressBound(data_view.size());
    char *compressed_data = new char[compressed_size];
    Py_ssize_t bytes_written = (Py_ssize_t) ZSTD_compress(compressed_data, compressed_size, data_view.data(),
                                                          data_view.size(), compression_level);
    if (ZSTD_isError(bytes_written)) {
        Py_DECREF(compressed_data);
        return PyErr_Format(PyExc_ValueError, "Compression failed: %s", ZSTD_getErrorName(bytes_written));
    }
    PyObject *res = PyBytes_FromStringAndSize(compressed_data, bytes_written);
    delete[] compressed_data;
    return res;
}

PyObject *py_zstd_decompress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1)
        return PyErr_Format(PyExc_TypeError, "zstd_decompress_stream() takes 1 positional arg");

    PyROBytesView data_view(args[0]);
    if (!data_view) {
        return type_error("data", "bytes", args[0]);
    }
    ZSTD_DStream *dstream = ZSTD_createDStream();
    if (!dstream) {
        return PyErr_Format(PyExc_RuntimeError, "Failed to create ZSTD decompression stream");
    }
    size_t init_result = ZSTD_initDStream(dstream);
    if (ZSTD_isError(init_result)) {
        ZSTD_freeDStream(dstream);
        return PyErr_Format(PyExc_RuntimeError, "Failed to initialize ZSTD decompression stream: %s",
                            ZSTD_getErrorName(init_result));
    }

    ZSTD_inBuffer input = {data_view.data(), data_view.size(), 0};
    size_t out_capacity = 65536;
    PyObject *result = PyBytes_FromStringAndSize(nullptr, 0);

    while (input.pos < input.size) {
        char out_buffer[65536];
        ZSTD_outBuffer output = {out_buffer, out_capacity, 0};
        size_t ret = ZSTD_decompressStream(dstream, &output, &input);
        if (ZSTD_isError(ret)) {
            Py_DECREF(result);
            ZSTD_freeDStream(dstream);
            return PyErr_Format(PyExc_ValueError, "Decompression failed: %s", ZSTD_getErrorName(ret));
        }
        if (output.pos > 0) {
            PyBytes_ConcatAndDel(&result, PyBytes_FromStringAndSize(out_buffer, (Py_ssize_t) output.pos));
        }
        if (ret == 0) break;
    }

    ZSTD_freeDStream(dstream);
    return result;
}

PyObject *py_zstd_compress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1)
        return PyErr_Format(PyExc_TypeError, "zstd_compress_stream() takes at least 1 positional arg");

    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);
    int compression_level = 3;
    if (nargs > 1) {
        if (!PyLong_Check(args[1]))
            return type_error("compression_level", "int", args[1]);
        compression_level = PyLong_AsLong(args[1]);
        if (compression_level < 1 || compression_level > 22)
            return PyErr_Format(PyExc_ValueError, "Compression level must be between 1 and 22, got %d",
                                compression_level);
    }

    ZSTD_CStream *cstream = ZSTD_createCStream();
    if (!cstream)
        return PyErr_Format(PyExc_RuntimeError, "Failed to create ZSTD compression stream");
    size_t init_result = ZSTD_initCStream(cstream, compression_level);
    if (ZSTD_isError(init_result)) {
        ZSTD_freeCStream(cstream);
        return PyErr_Format(PyExc_RuntimeError, "Failed to initialize ZSTD compression stream: %s",
                            ZSTD_getErrorName(init_result));
    }

    ZSTD_inBuffer input = {data_view.data(), data_view.size(), 0};
    size_t out_capacity = 65536;
    PyObject *result = PyBytes_FromStringAndSize(nullptr, 0);

    while (input.pos < input.size) {
        char out_buffer[65536];
        ZSTD_outBuffer output = {out_buffer, out_capacity, 0};
        size_t ret = ZSTD_compressStream(cstream, &output, &input);
        if (ZSTD_isError(ret)) {
            Py_DECREF(result);
            ZSTD_freeCStream(cstream);
            return PyErr_Format(PyExc_ValueError, "Compression failed: %s", ZSTD_getErrorName(ret));
        }
        if (output.pos > 0) {
            PyBytes_ConcatAndDel(&result, PyBytes_FromStringAndSize(out_buffer, (Py_ssize_t) output.pos));
        }
    }

    // End the stream
    int finished = 0;
    while (!finished) {
        char out_buffer[65536];
        ZSTD_outBuffer output = {out_buffer, out_capacity, 0};
        size_t ret = ZSTD_endStream(cstream, &output);
        if (ZSTD_isError(ret)) {
            Py_DECREF(result);
            ZSTD_freeCStream(cstream);
            return PyErr_Format(PyExc_ValueError, "Compression end failed: %s", ZSTD_getErrorName(ret));
        }
        if (output.pos > 0) {
            PyBytes_ConcatAndDel(&result, PyBytes_FromStringAndSize(out_buffer, (Py_ssize_t) output.pos));
        }
        if (ret == 0) finished = 1;
    }

    ZSTD_freeCStream(cstream);
    return result;
}

PyObject *py_lz4_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2)
        return PyErr_Format(PyExc_TypeError, "lz4_decompress() takes 2 positional args");

    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);
    if (!PyLong_Check(args[1]))
        return type_error("decompressed_size", "int", args[1]);

    int decompressed_size = PyLong_AsLong(args[1]);
    PyObject *decompressed_data = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    int bytes_written = LZ4_decompress_safe(data_view.data(), PyBytes_AsString(decompressed_data),
                                            (int) data_view.size(),
                                            decompressed_size);
    if (bytes_written < 0) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %i", bytes_written);
    }
    if (bytes_written != decompressed_size) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression size mismatch: expected %zd, got %zu", decompressed_size,
                            bytes_written);
    }
    return decompressed_data;
}

PyObject *py_lz4_decompress_continue(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3)
        return PyErr_Format(PyExc_TypeError, "lz4_decompress_continue() takes 3 positional args");

    if (!PyBytes_Check(args[0])) {
        return type_error("context", "bytes", args[0]);
    }
    PyROBytesView data_view(args[1]);
    if (!PyBytes_Check(args[1])) {
        return type_error("data", "bytes", args[1]);
    }
    if (!PyLong_Check(args[2]))
        return type_error("decompressed_size", "int", args[2]);

    char *context = PyBytes_AsString(args[0]);
    Py_ssize_t decompressed_size = PyLong_AsLongLong(args[2]);
    PyObject *decompressed_data = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    int bytes_written = LZ4_decompress_safe_continue((LZ4_streamDecode_t *) context, data_view.data(),
                                                     PyBytes_AsString(decompressed_data), (int) data_view.size(),
                                                     (int) decompressed_size);
    if (bytes_written < 0) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %i", bytes_written);
    }
    if (bytes_written != decompressed_size) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression size mismatch: expected %zd, got %zu", decompressed_size,
                            bytes_written);
    }
    return decompressed_data;
}

PyObject *py_lz4_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1)
        return PyErr_Format(PyExc_TypeError, "lz4_compress() takes at least 1 positional arg");
    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);

    size_t data_size = data_view.size();
    if (data_size > 0x7FFFFFF) {
        return PyErr_Format(PyExc_ValueError, "Data size too large for LZ4 compression: %zd bytes", data_size);
    }

    auto compressed_size = LZ4_compressBound((int) data_size);
    char *compressed_data = new char[compressed_size];
    int bytes_written = LZ4_compress_default(data_view.data(), compressed_data, (int) data_size,
                                             (int) compressed_size);
    if (bytes_written < 0) {
        delete[] compressed_data;
        return PyErr_Format(PyExc_ValueError, "Compression failed: %i", bytes_written);
    }
    PyObject *res = PyBytes_FromStringAndSize(compressed_data, bytes_written);
    delete[] compressed_data;
    return res;
}
