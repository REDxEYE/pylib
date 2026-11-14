#include "modules/compression_module.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>
#include "utils/utils.h"
#include "classes/lz4_chaindecoder_class.h"
#include "classes/lz4_chainencoder_class.h"

#include "zstd.h"
#include "lz4.h"

static PyObject *py_zstd_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2)return PyErr_Format(PyExc_TypeError, "zstd_decompress() takes 2 positional args");
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        return type_error("data", "bytes", args[0]);
    }
    if (!PyLong_Check(args[1])) {
        return type_error("decompressed_size", "int", args[1]);
    }

    Py_ssize_t decompressed_size = PyLong_AsSsize_t(args[1]);
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
    size_t bytes_written;
    char *dst = PyBytes_AsString(scratch);
    Py_BEGIN_ALLOW_THREADS
    bytes_written = ZSTD_decompress(dst, (size_t) decompressed_size, data_view.data(), data_view.size());
    Py_END_ALLOW_THREADS
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

//! Copy the first \p used bytes of a scratch bytes object into an exact-size
//! one (`_PyBytes_Resize` is not in the limited API) and drop the scratch.
static PyObject *shrink_bytes(PyObject *scratch, Py_ssize_t used) {
    if (PyBytes_Size(scratch) == used)
        return scratch;
    PyObject *res = PyBytes_FromStringAndSize(PyBytes_AsString(scratch), used);
    Py_DECREF(scratch);
    return res;
}

//! Ensure \p buf has at least \p chunk writable bytes past \p used. `resize`
//! grows capacity geometrically, so repeated calls stay amortised linear.
//! Returns false with MemoryError set instead of letting the exception escape.
static bool reserve_tail(std::vector<char> &buf, size_t used, size_t chunk) {
    try {
        if (buf.size() - used < chunk)
            buf.resize(used + chunk);
    } catch (const std::bad_alloc &) {
        PyErr_NoMemory();
        return false;
    } catch (const std::length_error &) {
        PyErr_NoMemory();
        return false;
    }
    return true;
}

//! Parse a zstd compression level, accepting the library's full range
//! ZSTD_minCLevel()..ZSTD_maxCLevel() (negative levels are the fast modes).
static bool parse_level(PyObject *obj, int *out) {
    long level = PyLong_AsLong(obj);
    if (level == -1 && PyErr_Occurred()) {
        if (PyErr_ExceptionMatches(PyExc_OverflowError)) {
            PyErr_Clear();
            PyErr_Format(PyExc_ValueError, "Compression level must be between %d and %d",
                         ZSTD_minCLevel(), ZSTD_maxCLevel());
        }
        return false;
    }
    if (level < ZSTD_minCLevel() || level > ZSTD_maxCLevel()) {
        PyErr_Format(PyExc_ValueError, "Compression level must be between %d and %d, got %ld",
                     ZSTD_minCLevel(), ZSTD_maxCLevel(), level);
        return false;
    }
    *out = (int) level;
    return true;
}

static PyObject *py_zstd_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1)
        return PyErr_Format(PyExc_TypeError, "zstd_compress() takes at least 1 positional arg");
    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);
    int compression_level = 3;
    if (nargs > 1) {
        if (!PyLong_Check(args[1]))
            return type_error("compression_level", "int", args[1]);
        if (!parse_level(args[1], &compression_level))
            return nullptr;
    }
    size_t bound = ZSTD_compressBound(data_view.size());
    if (ZSTD_isError(bound) || bound > (size_t) PY_SSIZE_T_MAX)
        return PyErr_Format(PyExc_ValueError, "Data too large for zstd compression: %zu bytes", data_view.size());
    // Allocated through PyBytes so a huge bound raises MemoryError instead of
    // std::bad_alloc escaping into the interpreter.
    PyObject *scratch = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) bound);
    if (!scratch)
        return nullptr;
    size_t bytes_written;
    char *dst = PyBytes_AsString(scratch);
    Py_BEGIN_ALLOW_THREADS
    bytes_written = ZSTD_compress(dst, bound, data_view.data(), data_view.size(), compression_level);
    Py_END_ALLOW_THREADS
    if (ZSTD_isError(bytes_written)) {
        Py_DECREF(scratch);
        return PyErr_Format(PyExc_ValueError, "Compression failed: %s", ZSTD_getErrorName(bytes_written));
    }
    return shrink_bytes(scratch, (Py_ssize_t) bytes_written);
}

static PyObject *py_zstd_decompress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1)
        return PyErr_Format(PyExc_TypeError, "zstd_decompress_stream() takes 1 positional arg");

    PyROBytesView data_view(args[0]);
    if (!data_view) {
        return type_error("data", "bytes", args[0]);
    }
    if (data_view.size() == 0)
        return PyBytes_FromStringAndSize(nullptr, 0);

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

    ZSTD_inBuffer input = {data_view.data(), (size_t) data_view.size(), 0};
    const size_t out_capacity = 65536;
    // Output is decoded straight into a growable buffer and turned into bytes
    // once at the end; concatenating bytes per chunk was quadratic.
    std::vector<char> out;
    size_t used = 0;

    // Keep calling until the input is consumed *and* the decoder had room to
    // spare, i.e. nothing is left buffered internally. Running out of input is
    // not success on its own: a frame may still be incomplete (ret != 0), and
    // the decoder starts the next frame automatically when frames are
    // concatenated, so ret == 0 mid-input must not stop the loop either.
    size_t ret = 0;
    while (true) {
        if (!reserve_tail(out, used, out_capacity)) {
            ZSTD_freeDStream(dstream);
            return nullptr;
        }
        ZSTD_outBuffer output = {out.data() + used, out_capacity, 0};
        Py_BEGIN_ALLOW_THREADS
        ret = ZSTD_decompressStream(dstream, &output, &input);
        Py_END_ALLOW_THREADS
        if (ZSTD_isError(ret)) {
            ZSTD_freeDStream(dstream);
            return PyErr_Format(PyExc_ValueError, "Decompression failed: %s", ZSTD_getErrorName(ret));
        }
        used += output.pos;
        // ret == 0: a frame ended and everything was flushed. With no input left
        // that is the end of the stream; another call would just ask for the
        // next frame's header (non-zero) and look like truncation.
        if (input.pos == input.size && (ret == 0 || output.pos < output.size))
            break;
    }

    ZSTD_freeDStream(dstream);
    if (ret != 0)
        return PyErr_Format(PyExc_ValueError, "Decompression failed: truncated zstd stream");
    if (used > (size_t) PY_SSIZE_T_MAX)
        return PyErr_NoMemory();
    return PyBytes_FromStringAndSize(out.data(), (Py_ssize_t) used);
}

static PyObject *py_zstd_compress_stream(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1)
        return PyErr_Format(PyExc_TypeError, "zstd_compress_stream() takes at least 1 positional arg");

    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);
    int compression_level = 3;
    if (nargs > 1) {
        if (!PyLong_Check(args[1]))
            return type_error("compression_level", "int", args[1]);
        if (!parse_level(args[1], &compression_level))
            return nullptr;
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

    ZSTD_inBuffer input = {data_view.data(), (size_t) data_view.size(), 0};
    const size_t out_capacity = 65536;
    // Compressed straight into a growable buffer, converted to bytes once.
    std::vector<char> out;
    size_t used = 0;

    while (input.pos < input.size) {
        if (!reserve_tail(out, used, out_capacity)) {
            ZSTD_freeCStream(cstream);
            return nullptr;
        }
        ZSTD_outBuffer output = {out.data() + used, out_capacity, 0};
        size_t ret;
        Py_BEGIN_ALLOW_THREADS
        ret = ZSTD_compressStream(cstream, &output, &input);
        Py_END_ALLOW_THREADS
        if (ZSTD_isError(ret)) {
            ZSTD_freeCStream(cstream);
            return PyErr_Format(PyExc_ValueError, "Compression failed: %s", ZSTD_getErrorName(ret));
        }
        used += output.pos;
    }

    // End the stream
    while (true) {
        if (!reserve_tail(out, used, out_capacity)) {
            ZSTD_freeCStream(cstream);
            return nullptr;
        }
        ZSTD_outBuffer output = {out.data() + used, out_capacity, 0};
        size_t ret;
        Py_BEGIN_ALLOW_THREADS
        ret = ZSTD_endStream(cstream, &output);
        Py_END_ALLOW_THREADS
        if (ZSTD_isError(ret)) {
            ZSTD_freeCStream(cstream);
            return PyErr_Format(PyExc_ValueError, "Compression end failed: %s", ZSTD_getErrorName(ret));
        }
        used += output.pos;
        if (ret == 0)
            break;
    }

    ZSTD_freeCStream(cstream);
    if (used > (size_t) PY_SSIZE_T_MAX)
        return PyErr_NoMemory();
    return PyBytes_FromStringAndSize(out.data(), (Py_ssize_t) used);
}

static PyObject *py_lz4_decompress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2)
        return PyErr_Format(PyExc_TypeError, "lz4_decompress() takes 2 positional args");

    PyROBytesView data_view(args[0]);
    if (!data_view)
        return type_error("data", "bytes", args[0]);
    if (!PyLong_Check(args[1]))
        return type_error("decompressed_size", "int", args[1]);

    Py_ssize_t decompressed_size = PyLong_AsSsize_t(args[1]);
    if (PyErr_Occurred())
        return nullptr;
    // A negative size reached PyBytes_FromStringAndSize unchecked, which returns
    // NULL, and PyBytes_AsString(NULL) then crashed the interpreter.
    if (decompressed_size < 0)
        return PyErr_Format(PyExc_ValueError, "decompressed_size must not be negative, got %zd", decompressed_size);
    if (decompressed_size > INT_MAX)
        return PyErr_Format(PyExc_ValueError, "decompressed_size too large for LZ4: %zd", decompressed_size);
    if (data_view.size() > (Py_ssize_t) INT_MAX)
        return PyErr_Format(PyExc_ValueError, "Data too large for LZ4: %zd bytes", data_view.size());

    PyObject *decompressed_data = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) decompressed_size);
    if (!decompressed_data)
        return nullptr;
    int bytes_written;
    char *dst = PyBytes_AsString(decompressed_data);
    Py_BEGIN_ALLOW_THREADS
    bytes_written = LZ4_decompress_safe(data_view.data(), dst, (int) data_view.size(), (int) decompressed_size);
    Py_END_ALLOW_THREADS
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

static PyObject *py_lz4_compress(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
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

    int compressed_size = LZ4_compressBound((int) data_size);
    // Allocated through PyBytes so a huge bound raises MemoryError instead of
    // std::bad_alloc escaping into the interpreter.
    PyObject *scratch = PyBytes_FromStringAndSize(nullptr, (Py_ssize_t) compressed_size);
    if (!scratch)
        return nullptr;
    int bytes_written;
    char *dst = PyBytes_AsString(scratch);
    Py_BEGIN_ALLOW_THREADS
    bytes_written = LZ4_compress_default(data_view.data(), dst, (int) data_size, compressed_size);
    Py_END_ALLOW_THREADS
    if (bytes_written < 0 || (bytes_written == 0 && data_size != 0)) {
        Py_DECREF(scratch);
        return PyErr_Format(PyExc_ValueError, "Compression failed: %i", bytes_written);
    }
    return shrink_bytes(scratch, (Py_ssize_t) bytes_written);
}

PyDoc_STRVAR(py_zstd_decompress_fn_doc,
             "zstd_decompress($module, data, decompressed_size, /)\n"
             "--\n"
             "\n"
             "Decompress a zstd frame into at most 'decompressed_size' bytes and return them.\n"
             "Raises ValueError on corrupt data or a negative size.\n"
);

PyDoc_STRVAR(py_zstd_compress_fn_doc,
             "zstd_compress($module, data, compression_level=3, /)\n"
             "--\n"
             "\n"
             "Compress data into a single zstd frame.\n"
             "compression_level ranges from zstd's minimum (negative, fastest) to its maximum\n"
             "(22); 0 selects the zstd default. Raises ValueError for out-of-range levels.\n"
);

PyDoc_STRVAR(py_zstd_compress_stream_fn_doc,
             "zstd_compress_stream($module, data, compression_level=3, /)\n"
             "--\n"
             "\n"
             "Compress data using the zstd streaming API.\n"
             "compression_level ranges from zstd's minimum (negative, fastest) to its maximum\n"
             "(22); 0 selects the zstd default. Raises ValueError for out-of-range levels.\n"
);

PyDoc_STRVAR(py_zstd_decompress_stream_fn_doc,
             "zstd_decompress_stream($module, data, /)\n"
             "--\n"
             "\n"
             "Decompress one or more concatenated zstd frames using the streaming API.\n"
             "Raises ValueError on corrupt or truncated data.\n"
);

PyDoc_STRVAR(py_lz4_decompress_fn_doc,
             "lz4_decompress($module, data, decompressed_size, /)\n"
             "--\n"
             "\n"
             "Decompress a raw LZ4 block that must decode to exactly 'decompressed_size' bytes.\n"
             "Raises ValueError on corrupt data, size mismatch, or sizes above INT_MAX.\n"
);

PyDoc_STRVAR(py_lz4_compress_fn_doc,
             "lz4_compress($module, data, /)\n"
             "--\n"
             "\n"
             "Compress data into a raw LZ4 block. Raises ValueError if data exceeds LZ4_MAX_INPUT_SIZE.\n"
);

static PyMethodDef compression_methods[] = {
        {"zstd_decompress",        CPF(py_zstd_decompress),        METH_FASTCALL, py_zstd_decompress_fn_doc},
        {"zstd_compress",          CPF(py_zstd_compress),          METH_FASTCALL, py_zstd_compress_fn_doc},
        {"zstd_compress_stream",   CPF(py_zstd_compress_stream),   METH_FASTCALL, py_zstd_compress_stream_fn_doc},
        {"zstd_decompress_stream", CPF(py_zstd_decompress_stream), METH_FASTCALL, py_zstd_decompress_stream_fn_doc},
        {"lz4_decompress",         CPF(py_lz4_decompress),         METH_FASTCALL, py_lz4_decompress_fn_doc},
        {"lz4_compress",           CPF(py_lz4_compress),           METH_FASTCALL, py_lz4_compress_fn_doc},
        {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef compression_module_def = {
        PyModuleDef_HEAD_INIT,
        "pylib.compression",
        "SourceIO compression module",
        -1,
        compression_methods
};

PyObject *CompressionModule_Init(PyObject *parent_module) {
    PyObject *module = add_submodule(parent_module, "compression", &compression_module_def);
    if (!module)
        return nullptr;

    if (add_type(module, "LZ4ChainDecoder", &LZ4ChainDecoder_spec) < 0)
        return nullptr;
    if (add_type(module, "LZ4ChainEncoder", &LZ4ChainEncoder_spec) < 0)
        return nullptr;

    return module;
}
