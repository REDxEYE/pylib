#include <string>
#include <functional>
#include <cstring>
#include "modules/image_module.h"
#include "tinyexr.h"
#include "ext/stb_image_write.h"
#include "ext/bcdec.h"
#include "ext/etc.h"
#include "utils/bcdec_helper.h"


static int valid_png_channels(int ch) {
    return (ch == 1 || ch == 2 || ch == 3 || ch == 4);
}

#ifdef _WIN32

static FILE *open_file_write_unicode(PyObject *path_obj) {
    if (!PyUnicode_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "file_path must be a str");
        return nullptr;
    }
    Py_ssize_t wlen = 0;
    wchar_t *wpath = PyUnicode_AsWideCharString(path_obj, &wlen);
    if (!wpath) return nullptr;
    FILE *f = _wfopen(wpath, L"wb");
    PyMem_Free(wpath);
    if (!f) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path_obj);
    }
    return f;
}

#else
static FILE* open_file_write_unicode(PyObject* path_obj) {
    if (!PyUnicode_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "file_path must be a str");
        return nullptr;
    }
    PyObject* bytes = nullptr;
    if (!PyUnicode_FSConverter(path_obj, &bytes)) {
        return nullptr; // converter sets error
    }
    const char* cpath = PyBytes_AsString(bytes);
    FILE* f = fopen(cpath, "wb");
    if (!f) {
        Py_DECREF(bytes);
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path_obj);
        return nullptr;
    }
    Py_DECREF(bytes);
    return f;
}
#endif

// stb callback to FILE*
static void stb_write_to_FILE(void *context, void *data, int size) {
    FILE *f = (FILE *) context;
    (void) fwrite(data, 1, (size_t) size, f);
}

// memory buffer for encode_* functions
typedef struct {
    unsigned char *data;
    size_t size;
    size_t cap;
    int failed;
} MemBuf;

static void membuf_init(MemBuf *b) {
    b->data = nullptr;
    b->size = 0;
    b->cap = 0;
    b->failed = 0;
}

static void membuf_free(MemBuf *b) {
    free(b->data);
    b->data = nullptr;
    b->size = b->cap = 0;
    b->failed = 0;
}

static int membuf_reserve(MemBuf *b, size_t extra) {
    if (b->failed) return 0;
    size_t need = b->size + extra;
    if (need <= b->cap) return 1;
    size_t newcap = b->cap ? b->cap : 4096;
    while (newcap < need) {
        size_t next = newcap * 2;
        if (next < newcap) {
            b->failed = 1;
            return 0;
        } // overflow
        newcap = next;
    }
    unsigned char *p = (unsigned char *) realloc(b->data, newcap);
    if (!p) {
        b->failed = 1;
        return 0;
    }
    b->data = p;
    b->cap = newcap;
    return 1;
}

static void stb_write_to_membuf(void *context, void *data, int size) {
    MemBuf *b = (MemBuf *) context;
    if (size <= 0) return;
    if (!membuf_reserve(b, (size_t) size)) return;
    memcpy(b->data + b->size, data, (size_t) size);
    b->size += (size_t) size;
}

PyObject *py_save_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 5) {
        PyErr_SetString(PyExc_TypeError,
                        "save_png(image_data: bytes, width: int, height: int, channels: int, file_path: str) "
                        "takes exactly 5 arguments");
        return nullptr;
    }
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        PyErr_SetString(PyExc_TypeError, "image_data must be bytes");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2]) || !PyLong_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "width, height, channels must be integers");
        return nullptr;
    }

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    Py_ssize_t ch = PyLong_AsSsize_t(args[3]);
    if (w <= 0 || h <= 0 || !valid_png_channels((int) ch)) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0; channels must be 1,2,3, or 4");
        return nullptr;
    }

    Py_ssize_t row_stride, expected;
    if (!mul_checked_psszt(w, ch, &row_stride) || !mul_checked_psszt(row_stride, h, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels overflowed");
        return nullptr;
    }
    if (data_view.size() != expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match width*height*channels (%zd)",
                     data_view.size(), expected);
        return nullptr;
    }

    FILE *f = open_file_write_unicode((PyObject *) args[4]);
    if (!f) return nullptr;

    int ok = stbi_write_png_to_func(stb_write_to_FILE, f, (int) w, (int) h, (int) ch, data_view.data(),
                                    (int) row_stride);
    int ferr = fflush(f);
    fclose(f);

    if (!ok || ferr != 0) {
        PyErr_SetString(PyExc_IOError, "failed to write PNG");
        return nullptr;
    }

    Py_RETURN_NONE;
}

PyObject *py_encode_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "encode_png(image_data: bytes, width: int, height: int, channels: int) "
                        "takes exactly 4 arguments");
        return nullptr;
    }

    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "image_data must be bytes");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2]) || !PyLong_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "width, height, channels must be integers");
        return nullptr;
    }

    const char *data = PyBytes_AsString(args[0]);
    if (!data) return nullptr;
    const Py_ssize_t data_len = PyBytes_Size(args[0]);

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    Py_ssize_t ch = PyLong_AsSsize_t(args[3]);
    if (w <= 0 || h <= 0 || !valid_png_channels((int) ch)) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0; channels must be 1,2,3, or 4");
        return nullptr;
    }

    Py_ssize_t row_stride, expected;
    if (!mul_checked_psszt(w, ch, &row_stride) || !mul_checked_psszt(row_stride, h, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels overflowed");
        return nullptr;
    }
    if (data_len != expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match width*height*channels (%zd)",
                     data_len, expected);
        return nullptr;
    }

    MemBuf buf;
    membuf_init(&buf);
    int ok = stbi_write_png_to_func(stb_write_to_membuf, &buf, (int) w, (int) h, (int) ch, data, (int) row_stride);

    if (!ok || buf.failed) {
        membuf_free(&buf);
        PyErr_SetString(PyExc_RuntimeError, "PNG encoding failed");
        return nullptr;
    }

    PyObject *out = PyBytes_FromStringAndSize((const char *) buf.data, (Py_ssize_t) buf.size);
    membuf_free(&buf);
    return out;
}

PyObject *py_save_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 5) {
        PyErr_SetString(PyExc_TypeError,
                        "save_exr(image_data: bytes(float32), width: int, height: int, channels: int, file_path: str) "
                        "takes exactly 5 arguments");
        return nullptr;
    }
    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "image_data must be bytes containing little-endian float32");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2]) || !PyLong_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "width, height, channels must be integers");
        return nullptr;
    }

    const char *data = PyBytes_AsString(args[0]);
    if (!data) return nullptr;
    const Py_ssize_t data_len = PyBytes_Size(args[0]);

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    Py_ssize_t ch = PyLong_AsSsize_t(args[3]);

    if (w <= 0 || h <= 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return nullptr;
    }
    if (!(ch == 1 || ch == 3 || ch == 4)) {
        PyErr_SetString(PyExc_ValueError, "channels must be 1 (Y), 3 (RGB), or 4 (RGBA)");
        return nullptr;
    }

    Py_ssize_t px_count;
    if (!mul_checked_psszt(w, h, &px_count)) {
        PyErr_SetString(PyExc_OverflowError, "width * height overflowed");
        return nullptr;
    }
    Py_ssize_t expected;
    if (!mul3_checked_psszt(px_count, ch, (Py_ssize_t) 4, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels * 4 overflowed");
        return nullptr;
    }
    if (data_len != expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match width*height*channels*4 (%zd)",
                     data_len, expected);
        return nullptr;
    }

    // Deinterleave interleaved float32 into planar channels as TinyEXR expects.
    const float *src = (const float *) data;
    const int channels = (int) ch;
    const int width = (int) w;
    const int height = (int) h;

    float **images = (float **) malloc((size_t) channels * sizeof(float *));
    if (!images) {
        PyErr_NoMemory();
        return nullptr;
    }

    size_t plane_bytes = (size_t) px_count * sizeof(float);
    for (int c = 0; c < channels; ++c) {
        images[c] = (float *) malloc(plane_bytes);
        if (!images[c]) {
            for (int j = 0; j < c; ++j) free(images[j]);
            free(images);
            PyErr_NoMemory();
            return nullptr;
        }
    }

    // EXR channel order is B,G,R,(A) by convention for many tools; we'll write R,G,B,(A) labels but TinyEXR requires
    // images[] to be in channel order matching header.channels[].name. We'll map accordingly below.
    for (Py_ssize_t i = 0; i < px_count; ++i) {
        for (int c = 0; c < channels; ++c) {
            images[c][i] = src[i * channels + c];
        }
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);

    image.num_channels = channels;
    image.images = (unsigned char **) images; // reinterpret, TinyEXR uses unsigned char*

    header.num_channels = channels;
    EXRChannelInfo *chan_info = (EXRChannelInfo *) malloc((size_t) channels * sizeof(EXRChannelInfo));
    int *pixel_types = (int *) malloc((size_t) channels * sizeof(int));
    int *requested_pixel_types = (int *) malloc((size_t) channels * sizeof(int));
    if (!chan_info || !pixel_types || !requested_pixel_types) {
        if (chan_info) free(chan_info);
        if (pixel_types) free(pixel_types);
        if (requested_pixel_types) free(requested_pixel_types);
        for (int c = 0; c < channels; ++c) free(images[c]);
        free(images);
        PyErr_NoMemory();
        return nullptr;
    }

    // Names and component order: write "R","G","B","A" for 3/4; for 1 channel write "Y"
    static const char *RGBA[4] = {"R", "G", "B", "A"};
    for (int c = 0; c < channels; ++c) {
        const char *name = (channels == 1) ? "Y" : RGBA[c];
        // Copy channel name (<=255 including NUL)
        size_t len = strlen(name);
        if (len > 255) len = 255;
        memcpy(chan_info[c].name, name, len + 1);
        pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;        // input type
        requested_pixel_types[c] = TINYEXR_PIXELTYPE_HALF; // store as HALF to shrink file size (common practice)
    }

    header.channels = chan_info;
    header.pixel_types = pixel_types;
    header.requested_pixel_types = requested_pixel_types;

    image.width = width;
    image.height = height;

    FILE *f = open_file_write_unicode((PyObject *) args[4]);
    if (!f) {
        free(requested_pixel_types);
        free(pixel_types);
        free(chan_info);
        for (int c = 0; c < channels; ++c) free(images[c]);
        free(images);
        return nullptr;
    }

    // TinyEXR provides SaveEXRImageToFile (by filename), but to reuse our Unicode-safe FILE*, write to memory then fwrite.
    unsigned char *out_data = nullptr;
    const char *err = nullptr;
    size_t out_size = 0;

    out_size = SaveEXRImageToMemory(&image, &header, &out_data, &err);
    if (!out_size) {
        if (err) {
            PyErr_SetString(PyExc_RuntimeError, err);
            FreeEXRErrorMessage(err);
        } else { PyErr_SetString(PyExc_RuntimeError, "EXR save failed"); }
        fclose(f);
        free(requested_pixel_types);
        free(pixel_types);
        free(chan_info);
        for (int c = 0; c < channels; ++c) free(images[c]);
        free(images);
        return nullptr;
    }

    size_t written = fwrite(out_data, 1, out_size, f);
    int ferr = fflush(f);
    fclose(f);
    free(out_data);

    free(requested_pixel_types);
    free(pixel_types);
    free(chan_info);
    for (int c = 0; c < channels; ++c) free(images[c]);
    free(images);

    if (written != out_size || ferr != 0) {
        PyErr_SetString(PyExc_IOError, "failed to write EXR");
        return nullptr;
    }

    Py_RETURN_NONE;
}

PyObject *py_encode_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "encode_exr(image_data: bytes(float32), width: int, height: int, channels: int) "
                        "takes exactly 4 arguments");
        return nullptr;
    }
    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "image_data must be bytes containing little-endian float32");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2]) || !PyLong_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "width, height, channels must be integers");
        return nullptr;
    }

    const char *data = PyBytes_AsString(args[0]);
    if (!data) return nullptr;
    const Py_ssize_t data_len = PyBytes_Size(args[0]);

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    Py_ssize_t ch = PyLong_AsSsize_t(args[3]);

    if (w <= 0 || h <= 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return nullptr;
    }
    if (!(ch == 1 || ch == 3 || ch == 4)) {
        PyErr_SetString(PyExc_ValueError, "channels must be 1 (Y), 3 (RGB), or 4 (RGBA)");
        return nullptr;
    }

    Py_ssize_t px_count;
    if (!mul_checked_psszt(w, h, &px_count)) {
        PyErr_SetString(PyExc_OverflowError, "width * height overflowed");
        return nullptr;
    }
    Py_ssize_t expected;
    if (!mul3_checked_psszt(px_count, ch, (Py_ssize_t) 4, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels * 4 overflowed");
        return nullptr;
    }
    if (data_len != expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match width*height*channels*4 (%zd)",
                     data_len, expected);
        return nullptr;
    }

    const float *src = (const float *) data;
    const int channels = (int) ch;
    const int width = (int) w;
    const int height = (int) h;

    float **images = (float **) malloc((size_t) channels * sizeof(float *));
    if (!images) {
        PyErr_NoMemory();
        return nullptr;
    }

    size_t plane_bytes = (size_t) px_count * sizeof(float);
    for (int c = 0; c < channels; ++c) {
        images[c] = (float *) malloc(plane_bytes);
        if (!images[c]) {
            for (int j = 0; j < c; ++j) free(images[j]);
            free(images);
            PyErr_NoMemory();
            return nullptr;
        }
    }

    for (Py_ssize_t i = 0; i < px_count; ++i) {
        for (int c = 0; c < channels; ++c) {
            images[c][i] = src[i * channels + c];
        }
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);

    image.num_channels = channels;
    image.images = (unsigned char **) images;

    header.num_channels = channels;
    EXRChannelInfo *chan_info = (EXRChannelInfo *) malloc((size_t) channels * sizeof(EXRChannelInfo));
    int *pixel_types = (int *) malloc((size_t) channels * sizeof(int));
    int *requested_pixel_types = (int *) malloc((size_t) channels * sizeof(int));
    if (!chan_info || !pixel_types || !requested_pixel_types) {
        if (chan_info) free(chan_info);
        if (pixel_types) free(pixel_types);
        if (requested_pixel_types) free(requested_pixel_types);
        for (int c = 0; c < channels; ++c) free(images[c]);
        free(images);
        PyErr_NoMemory();
        return nullptr;
    }

    static const char *RGBA[4] = {"R", "G", "B", "A"};
    for (int c = 0; c < channels; ++c) {
        const char *name = (channels == 1) ? "Y" : RGBA[c];
        size_t len = strlen(name);
        if (len > 255) len = 255;
        memcpy(chan_info[c].name, name, len + 1);
        pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
        requested_pixel_types[c] = TINYEXR_PIXELTYPE_HALF;
    }

    header.channels = chan_info;
    header.pixel_types = pixel_types;
    header.requested_pixel_types = requested_pixel_types;

    image.width = width;
    image.height = height;

    unsigned char *out_data = nullptr;
    size_t out_size = 0;
    const char *err = nullptr;

    out_size = SaveEXRImageToMemory(&image, &header, &out_data, &err);
    // free temp allocations
    free(requested_pixel_types);
    free(pixel_types);
    free(chan_info);
    for (int c = 0; c < channels; ++c) free(images[c]);
    free(images);

    if (out_size == 0) {
        if (err) {
            PyErr_SetString(PyExc_RuntimeError, err);
            FreeEXRErrorMessage(err);
        } else { PyErr_SetString(PyExc_RuntimeError, "EXR encode failed"); }
        return nullptr;
    }

    PyObject *out = PyBytes_FromStringAndSize((const char *) out_data, (Py_ssize_t) out_size);
    free(out_data);
    return out;
}

PyObject *py_decode_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "decode_texture(image_data: bytes, width: int, height: int, format: str) "
                        "takes exactly 4 arguments");
        return nullptr;
    }
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        PyErr_SetString(PyExc_TypeError, "image_data must be bytes");
        return nullptr;
    }
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2])) {
        PyErr_SetString(PyExc_TypeError, "width and height must be integers");
        return nullptr;
    }
    if (!PyUnicode_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "format must be a str");
        return nullptr;
    }

    const uint8_t *data = (uint8_t *) data_view.data();
    const size_t data_len = data_view.size();

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    if (w <= 0 || h <= 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return nullptr;
    }
    Py_ssize_t format_len = 0;
    const char *format_c = PyUnicode_AsUTF8AndSize(args[3], &format_len);
    if (!format_c)
        return nullptr;  // non-UTF8 str; converter set the error
    // Built from (ptr, len) rather than the bare pointer so a name containing an
    // embedded NUL compares unequal instead of silently matching its prefix.
    std::string format(format_c, (size_t) format_len);

    // Every decoder writes straight into the result buffer, so one allocation
    // helper keeps the NULL check from having to be repeated per branch.
    PyObject *result = nullptr;
    auto alloc = [&result](Py_ssize_t size) -> uint8_t * {
        result = PyBytes_FromStringAndSize(nullptr, size);
        return result ? (uint8_t *) PyBytes_AsString(result) : nullptr;
    };
    uint8_t *out;
    if (format == "BC1" || format == "DXT1") {
        if (data_len < BCDEC_BC1_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC1 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC1_BLOCK_SIZE, 4, 4>(data, out, w, h, bcdec_bc1);
    } else if (format == "BC2" || format == "DXT3") {
        if (data_len < BCDEC_BC2_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC2 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC2_BLOCK_SIZE, 4, 4>(data, out, w, h, bcdec_bc2);
    } else if (format == "BC3" || format == "DXT5") {
        if (data_len < BCDEC_BC3_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC3 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC3_BLOCK_SIZE, 4, 4>(data, out, w, h, bcdec_bc3);
    } else if (format == "BC4" || format == "ATI1N") {
        if (data_len < BCDEC_BC4_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC4 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC4_BLOCK_SIZE, 4, 1>(data, out, w, h,
                                               [](void *src, void *dst, int32_t pitch) {
                                                   bcdec_bc4(src, dst, pitch, false);
                                               });
    } else if (format == "BC5" || format == "ATI2N") {
        if (data_len < BCDEC_BC5_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC5 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 2);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC5_BLOCK_SIZE, 4, 2>(data, out, w, h, [](void *src, void *dst, int32_t pitch) {
                                                   bcdec_bc5(src, dst, pitch, false);
                                               });
    } else if (format == "BC6H") {
        if (data_len < BCDEC_BC6H_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC6H compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 2 * 3);
        if (!out) return nullptr;
        convertBC6<6>(data, out, w, h, bcdec_bc6h_half_unsigned);
    } else if (format == "BC7") {
        if (data_len < BCDEC_BC7_COMPRESSED_SIZE(w, h)) {
            PyErr_Format(PyExc_ValueError, "image_data length (%zd) does not match BC7 compressed size for %zd x %zd",
                         data_len, w, h);
            return nullptr;
        }
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        convertBCn<BCDEC_BC7_BLOCK_SIZE, 4, 4>(data, out, w, h, bcdec_bc7);
    } else if (format == "ETC1") {
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        decode_etc1(data, w, h, (uint32_t *) out);
    } else if (format == "ETC2") {
        out = alloc(w * h * 4);
        if (!out) return nullptr;
        decode_etc2(data, w, h, (uint32_t *) out);
    } else {
        PyErr_SetString(PyExc_ValueError,
                        "format must be one of: 'BC1', 'DXT1', 'BC2', 'DXT3', 'BC3', 'DXT5', 'BC4', 'BC5', 'BC6H', "
                        "'BC7', 'ETC1', 'ETC2'");
        return nullptr;
    }
    return result;
}
