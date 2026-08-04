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
    if (PyErr_Occurred())
        return nullptr;
    if (decompressed_size < 0)
        return PyErr_Format(PyExc_ValueError, "decompressed_size must not be negative, got %zd", decompressed_size);

    // Allocate through PyBytes so an absurd size raises MemoryError rather than
    // letting std::vector throw std::bad_alloc across the C boundary, which
    // terminates the process. `_PyBytes_Resize` is excluded from the stable ABI at
    // every version, so a short result is copied into a correctly-sized object
    // instead of being trimmed in place.
    PyObject *scratch = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    if (!scratch)
        return nullptr;
    size_t bytes_written = ZSTD_decompress(PyBytes_AsString(scratch), (size_t) decompressed_size, data_view.data(),
                                           data_view.size());
    if (ZSTD_isError(bytes_written)) {
        Py_DECREF(scratch);
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %s", ZSTD_getErrorName(bytes_written));
    }
    if ((Py_ssize_t) bytes_written == decompressed_size)
        return scratch;
    PyObject *res = PyBytes_FromStringAndSize(PyBytes_AsString(scratch), (Py_ssize_t) bytes_written);
    Py_DECREF(scratch);
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
    // `Py_DECREF` used to be called on this raw `new char[]` buffer on the error
    // path, corrupting the heap. A vector removes the possibility entirely.
    std::vector<char> compressed_data(compressed_size);
    Py_ssize_t bytes_written = (Py_ssize_t) ZSTD_compress(compressed_data.data(), compressed_size, data_view.data(),
                                                          data_view.size(), compression_level);
    if (ZSTD_isError(bytes_written)) {
        return PyErr_Format(PyExc_ValueError, "Compression failed: %s", ZSTD_getErrorName(bytes_written));
    }
    return PyBytes_FromStringAndSize(compressed_data.data(), bytes_written);
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
    if (!result) {
        ZSTD_freeDStream(dstream);
        return nullptr;
    }

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
            // Sets `result` to NULL on failure (and already dropped both refs), so
            // it must be checked before the next iteration dereferences it.
            PyBytes_ConcatAndDel(&result, PyBytes_FromStringAndSize(out_buffer, (Py_ssize_t) output.pos));
            if (!result) {
                ZSTD_freeDStream(dstream);
                return nullptr;
            }
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
    if (!result) {
        ZSTD_freeCStream(cstream);
        return nullptr;
    }

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
            if (!result) {
                ZSTD_freeCStream(cstream);
                return nullptr;
            }
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
            if (!result) {
                ZSTD_freeCStream(cstream);
                return nullptr;
            }
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

    long decompressed_size = PyLong_AsLong(args[1]);
    if (PyErr_Occurred())
        return nullptr;
    // A negative size reached PyBytes_FromStringAndSize unchecked, which returns
    // NULL, and PyBytes_AsString(NULL) then crashed the interpreter.
    if (decompressed_size < 0)
        return PyErr_Format(PyExc_ValueError, "decompressed_size must not be negative, got %ld", decompressed_size);
    if (decompressed_size > INT_MAX)
        return PyErr_Format(PyExc_ValueError, "decompressed_size too large for LZ4: %ld", decompressed_size);

    PyObject *decompressed_data = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) decompressed_size);
    if (!decompressed_data)
        return nullptr;
    int bytes_written = LZ4_decompress_safe(data_view.data(), PyBytes_AsString(decompressed_data),
                                            (int) data_view.size(),
                                            (int) decompressed_size);
    if (bytes_written < 0) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %i", bytes_written);
    }
    if (bytes_written != decompressed_size) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression size mismatch: expected %ld, got %i", decompressed_size,
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

    if (!data_view)
        return type_error("data", "bytes", args[1]);

    char *context = PyBytes_AsString(args[0]);
    if (PyBytes_Size(args[0]) < (Py_ssize_t) sizeof(LZ4_streamDecode_t))
        return PyErr_Format(PyExc_ValueError, "context must be at least %zd bytes",
                            (Py_ssize_t) sizeof(LZ4_streamDecode_t));

    Py_ssize_t decompressed_size = PyLong_AsLongLong(args[2]);
    if (PyErr_Occurred())
        return nullptr;
    if (decompressed_size < 0)
        return PyErr_Format(PyExc_ValueError, "decompressed_size must not be negative, got %zd", decompressed_size);
    if (decompressed_size > INT_MAX)
        return PyErr_Format(PyExc_ValueError, "decompressed_size too large for LZ4: %zd", decompressed_size);

    PyObject *decompressed_data = PyBytes_FromStringAndSize(nullptr, decompressed_size);
    if (!decompressed_data)
        return nullptr;
    int bytes_written = LZ4_decompress_safe_continue((LZ4_streamDecode_t *) context, data_view.data(),
                                                     PyBytes_AsString(decompressed_data), (int) data_view.size(),
                                                     (int) decompressed_size);
    if (bytes_written < 0) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression failed: %i", bytes_written);
    }
    if (bytes_written != decompressed_size) {
        Py_DECREF(decompressed_data);
        return PyErr_Format(PyExc_ValueError, "Decompression size mismatch: expected %zd, got %i", decompressed_size,
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
    // LZ4_MAX_INPUT_SIZE, not the 0x7FFFFFF (one digit short of 0x7FFFFFFF) that
    // used to be here, which rejected valid inputs between 128 MiB and 2 GiB.
    if (data_size > LZ4_MAX_INPUT_SIZE) {
        return PyErr_Format(PyExc_ValueError, "Data size too large for LZ4 compression: %zu bytes", data_size);
    }

    auto compressed_size = LZ4_compressBound((int) data_size);
    std::vector<char> compressed_data(compressed_size);
    int bytes_written = LZ4_compress_default(data_view.data(), compressed_data.data(), (int) data_size,
                                             (int) compressed_size);
    if (bytes_written <= 0 && data_size != 0) {
        return PyErr_Format(PyExc_ValueError, "Compression failed: %i", bytes_written);
    }
    return PyBytes_FromStringAndSize(compressed_data.data(), bytes_written);
}
