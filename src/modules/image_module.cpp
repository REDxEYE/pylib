#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "modules/image_module.h"
#include "utils/utils.h"
#include "tinyexr.h"
#include "ext/stb_image_write.h"
#include "ext/bcdec.h"
#include "ext/etc.h"
#include "utils/bcdec_helper.h"


// Reads width, height and channels (args[1..3]) as Py_ssize_t. Range checks are
// left to the caller, which compares the full-width values before narrowing.
static bool parse_whc(PyObject *const *args, Py_ssize_t *w, Py_ssize_t *h, Py_ssize_t *ch) {
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2]) || !PyLong_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "width, height, channels must be integers");
        return false;
    }
    Py_ssize_t *outs[3] = {w, h, ch};
    for (int i = 0; i < 3; i++) {
        *outs[i] = PyLong_AsSsize_t(args[i + 1]);
        if (*outs[i] == -1 && PyErr_Occurred()) return false;  // OverflowError propagates
    }
    return true;
}

// ---------------------------------------------------------------------------
// Output files: opened the same way they are removed again on a failed write.

struct OutFile {
    FILE *f = nullptr;
    PyObject *path_obj = nullptr;  // borrowed, for error messages
#ifdef _WIN32
    wchar_t *wpath = nullptr;
#else
    PyObject *path_bytes = nullptr;
#endif
};

static void out_release_path(OutFile *of) {
#ifdef _WIN32
    PyMem_Free(of->wpath);
    of->wpath = nullptr;
#else
    Py_CLEAR(of->path_bytes);
#endif
}

static bool out_open(PyObject *path_obj, OutFile *of) {
    if (!PyUnicode_Check(path_obj)) {
        PyErr_SetString(PyExc_TypeError, "file_path must be a str");
        return false;
    }
    of->path_obj = path_obj;
#ifdef _WIN32
    of->wpath = PyUnicode_AsWideCharString(path_obj, nullptr);
    if (!of->wpath) return false;
    of->f = _wfopen(of->wpath, L"wb");
#else
    if (!PyUnicode_FSConverter(path_obj, &of->path_bytes)) return false;
    of->f = fopen(PyBytes_AsString(of->path_bytes), "wb");
#endif
    if (!of->f) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path_obj);
        out_release_path(of);
        return false;
    }
    return true;
}

static void out_remove(OutFile *of) {
#ifdef _WIN32
    _wremove(of->wpath);
#else
    remove(PyBytes_AsString(of->path_bytes));
#endif
}

// Closes the file. If the write or the close failed, removes the partial file and
// raises OSError (errno from the first failure). Returns true on success.
static bool out_close(OutFile *of, bool write_ok, int write_errno) {
    bool ok = write_ok;
    int err = write_errno;
    if (fclose(of->f) != 0 && ok) {
        ok = false;
        err = errno;
    }
    of->f = nullptr;
    if (!ok) {
        out_remove(of);
        errno = err ? err : EIO;
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, of->path_obj);
    }
    out_release_path(of);
    return ok;
}

// Closes and removes the file after a non-I/O failure; the caller raises.
static void out_discard(OutFile *of) {
    fclose(of->f);
    of->f = nullptr;
    out_remove(of);
    out_release_path(of);
}

struct FileSink {
    FILE *f;
    bool failed;
    int err;
};

// stb callback to FILE*; records the first short write.
static void stb_write_to_FILE(void *context, void *data, int size) {
    FileSink *s = (FileSink *) context;
    if (s->failed || size <= 0) return;
    if (fwrite(data, 1, (size_t) size, s->f) != (size_t) size) {
        s->failed = true;
        s->err = errno;
    }
}

// ---------------------------------------------------------------------------
// Memory buffer for encode_png.

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

// ---------------------------------------------------------------------------
// PNG

struct PngArgs {
    PyROBytesView view;
    int w, h, ch, row_stride;
};

// Validates (image_data, width, height, channels). On failure returns false with
// an exception set.
static bool png_parse_args(PyObject *const *args, PngArgs *out) {
    out->view = PyROBytesView(args[0]);
    if (!out->view) return false;  // PyBytes_FromObject's TypeError
    Py_ssize_t w, h, ch;
    if (!parse_whc(args, &w, &h, &ch)) return false;
    if (w <= 0 || h <= 0 || ch < 1 || ch > 4) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0; channels must be 1,2,3, or 4");
        return false;
    }

    Py_ssize_t row_stride, expected;
    if (!mul_checked_psszt(w, ch, &row_stride) || !mul_checked_psszt(row_stride, h, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels overflowed");
        return false;
    }
    // stb_image_write does its size math in int; its filtered buffer is
    // (w*ch + 1) * h bytes, so that product must stay below INT_MAX.
    Py_ssize_t filtered;
    if (w > INT_MAX || h > INT_MAX || row_stride >= PY_SSIZE_T_MAX ||
        !mul_checked_psszt(row_stride + 1, h, &filtered) || filtered >= INT_MAX) {
        PyErr_SetString(PyExc_OverflowError, "image too large for PNG encoder (int size limits)");
        return false;
    }
    if (out->view.size() != (size_t) expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zu) does not match width*height*channels (%zd)",
                     out->view.size(), expected);
        return false;
    }
    out->w = (int) w;
    out->h = (int) h;
    out->ch = (int) ch;
    out->row_stride = (int) row_stride;
    return true;
}

static PyObject *py_save_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 5) {
        PyErr_SetString(PyExc_TypeError,
                        "save_png(image_data, width, height, channels, file_path) takes exactly 5 arguments");
        return nullptr;
    }
    PngArgs a;
    if (!png_parse_args(args, &a)) return nullptr;

    OutFile of;
    if (!out_open(args[4], &of)) return nullptr;

    FileSink sink{of.f, false, 0};
    int ok;
    Py_BEGIN_ALLOW_THREADS
        ok = stbi_write_png_to_func(stb_write_to_FILE, &sink, a.w, a.h, a.ch, a.view.data(), a.row_stride);
        if (!sink.failed && fflush(of.f) != 0) {
            sink.failed = true;
            sink.err = errno;
        }
    Py_END_ALLOW_THREADS

    if (!ok && !sink.failed) {
        out_discard(&of);
        PyErr_SetString(PyExc_RuntimeError, "PNG encoding failed");
        return nullptr;
    }
    if (!out_close(&of, !sink.failed, sink.err)) return nullptr;
    Py_RETURN_NONE;
}

static PyObject *py_encode_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "encode_png(image_data, width, height, channels) takes exactly 4 arguments");
        return nullptr;
    }
    PngArgs a;
    if (!png_parse_args(args, &a)) return nullptr;

    MemBuf buf;
    membuf_init(&buf);
    int ok;
    Py_BEGIN_ALLOW_THREADS
        ok = stbi_write_png_to_func(stb_write_to_membuf, &buf, a.w, a.h, a.ch, a.view.data(), a.row_stride);
    Py_END_ALLOW_THREADS

    if (!ok || buf.failed) {
        const bool oom = buf.failed;
        membuf_free(&buf);
        if (oom) return PyErr_NoMemory();
        PyErr_SetString(PyExc_RuntimeError, "PNG encoding failed");
        return nullptr;
    }

    PyObject *out = PyBytes_FromStringAndSize((const char *) buf.data, (Py_ssize_t) buf.size);
    membuf_free(&buf);
    return out;
}

// ---------------------------------------------------------------------------
// EXR

// Shared by save_exr/encode_exr: validates (image_data, width, height, channels)
// and encodes to an in-memory EXR (free() the result). On failure returns false
// with an exception set.
static bool exr_encode_args(PyObject *const *args, unsigned char **out_data, size_t *out_size,
                            const char *fail_msg) {
    PyROBytesView view(args[0]);
    if (!view) return false;  // PyBytes_FromObject's TypeError
    Py_ssize_t w, h, ch;
    if (!parse_whc(args, &w, &h, &ch)) return false;

    if (w <= 0 || h <= 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return false;
    }
    if (w > INT_MAX || h > INT_MAX) {
        PyErr_SetString(PyExc_OverflowError, "width and height must be <= 2147483647");
        return false;
    }
    if (!(ch == 1 || ch == 3 || ch == 4)) {
        PyErr_SetString(PyExc_ValueError, "channels must be 1 (Y), 3 (RGB), or 4 (RGBA)");
        return false;
    }

    Py_ssize_t px_count;
    if (!mul_checked_psszt(w, h, &px_count)) {
        PyErr_SetString(PyExc_OverflowError, "width * height overflowed");
        return false;
    }
    Py_ssize_t expected;
    if (!mul3_checked_psszt(px_count, ch, (Py_ssize_t) 4, &expected)) {
        PyErr_SetString(PyExc_OverflowError, "width * height * channels * 4 overflowed");
        return false;
    }
    if (view.size() != (size_t) expected) {
        PyErr_Format(PyExc_ValueError, "image_data length (%zu) does not match width*height*channels*4 (%zd)",
                     view.size(), expected);
        return false;
    }

    // PyROBytesView data is a bytes object's buffer, so float alignment holds.
    const float *src = (const float *) view.data();
    const int channels = (int) ch;

    // OpenEXR requires channels sorted by name, and TinyEXR pairs images[k] with
    // channels[k]; so plane k holds interleaved component src_index[k].
    static const char *const NAMES_1[1] = {"Y"};
    static const char *const NAMES_3[3] = {"B", "G", "R"};
    static const char *const NAMES_4[4] = {"A", "B", "G", "R"};
    static const int SRC_1[1] = {0};
    static const int SRC_3[3] = {2, 1, 0};
    static const int SRC_4[4] = {3, 2, 1, 0};
    const char *const *names = channels == 1 ? NAMES_1 : channels == 3 ? NAMES_3 : NAMES_4;
    const int *src_index = channels == 1 ? SRC_1 : channels == 3 ? SRC_3 : SRC_4;

    float *images[4] = {nullptr, nullptr, nullptr, nullptr};
    EXRChannelInfo chan_info[4];
    int pixel_types[4];
    int requested_pixel_types[4];

    size_t plane_bytes = (size_t) px_count * sizeof(float);
    for (int c = 0; c < channels; ++c) {
        images[c] = (float *) malloc(plane_bytes);
        if (!images[c]) {
            for (int j = 0; j < c; ++j) free(images[j]);
            PyErr_NoMemory();
            return false;
        }
    }

    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage image;
    InitEXRImage(&image);

    image.num_channels = channels;
    image.images = (unsigned char **) images; // TinyEXR uses unsigned char*
    image.width = (int) w;
    image.height = (int) h;

    memset(chan_info, 0, sizeof(chan_info));
    for (int c = 0; c < channels; ++c) {
        memcpy(chan_info[c].name, names[c], strlen(names[c]) + 1);
        pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;        // input type
        requested_pixel_types[c] = TINYEXR_PIXELTYPE_HALF; // store as HALF to shrink file size
    }
    header.num_channels = channels;
    header.channels = chan_info;
    header.pixel_types = pixel_types;
    header.requested_pixel_types = requested_pixel_types;

    const char *err = nullptr;
    *out_data = nullptr;
    Py_BEGIN_ALLOW_THREADS
        // Deinterleave interleaved float32 into planar channels as TinyEXR expects.
        for (int c = 0; c < channels; ++c) {
            float *plane = images[c];
            const int s = src_index[c];
            for (Py_ssize_t i = 0; i < px_count; ++i) plane[i] = src[i * channels + s];
        }
        *out_size = SaveEXRImageToMemory(&image, &header, out_data, &err);
    Py_END_ALLOW_THREADS
    for (int c = 0; c < channels; ++c) free(images[c]);

    if (*out_size == 0) {
        if (err) {
            PyErr_SetString(PyExc_RuntimeError, err);
            FreeEXRErrorMessage(err);
        } else { PyErr_SetString(PyExc_RuntimeError, fail_msg); }
        return false;
    }
    return true;
}

static PyObject *py_save_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 5) {
        PyErr_SetString(PyExc_TypeError,
                        "save_exr(image_data, width, height, channels, file_path) takes exactly 5 arguments");
        return nullptr;
    }

    // Encode to memory, then write through our Unicode-safe FILE*.
    unsigned char *out_data = nullptr;
    size_t out_size = 0;
    if (!exr_encode_args(args, &out_data, &out_size, "EXR save failed")) return nullptr;

    OutFile of;
    if (!out_open(args[4], &of)) {
        free(out_data);
        return nullptr;
    }

    bool write_ok;
    int write_errno = 0;
    Py_BEGIN_ALLOW_THREADS
        write_ok = fwrite(out_data, 1, out_size, of.f) == out_size && fflush(of.f) == 0;
        if (!write_ok) write_errno = errno;
    Py_END_ALLOW_THREADS
    free(out_data);

    if (!out_close(&of, write_ok, write_errno)) return nullptr;
    Py_RETURN_NONE;
}

static PyObject *py_encode_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "encode_exr(image_data, width, height, channels) takes exactly 4 arguments");
        return nullptr;
    }

    unsigned char *out_data = nullptr;
    size_t out_size = 0;
    if (!exr_encode_args(args, &out_data, &out_size, "EXR encode failed")) return nullptr;

    PyObject *out = PyBytes_FromStringAndSize((const char *) out_data, (Py_ssize_t) out_size);
    free(out_data);
    return out;
}

// ---------------------------------------------------------------------------
// Texture decoding

using TextureDecodeFn = void (*)(const uint8_t *src, uint8_t *out, uint32_t w, uint32_t h);

static void bc4_unsigned(const void *src, void *dst, int pitch) { bcdec_bc4(src, dst, pitch, false); }
static void bc4_signed(const void *src, void *dst, int pitch) { bcdec_bc4(src, dst, pitch, true); }
static void bc5_unsigned(const void *src, void *dst, int pitch) { bcdec_bc5(src, dst, pitch, false); }
static void bc5_signed(const void *src, void *dst, int pitch) { bcdec_bc5(src, dst, pitch, true); }

template<size_t BlockBytes, size_t PixelBytes, auto BlockFn>
static void decode_bcn(const uint8_t *src, uint8_t *out, uint32_t w, uint32_t h) {
    convertBCn<BlockBytes, 4, PixelBytes>(src, out, w, h, BlockFn);
}

template<auto BlockFn>
static void decode_bc6(const uint8_t *src, uint8_t *out, uint32_t w, uint32_t h) {
    convertBC6<6>(src, out, w, h, BlockFn);
}

// The bytes object's buffer satisfies uint32_t alignment.
template<void (*EtcFn)(const uint8_t *, uint32_t, uint32_t, uint32_t *)>
static void decode_etc(const uint8_t *src, uint8_t *out, uint32_t w, uint32_t h) {
    EtcFn(src, w, h, (uint32_t *) out);
}

struct TextureFormat {
    const char *name;
    const char *alias;  // nullptr if none
    Py_ssize_t block_bytes;  // per 4x4 block
    Py_ssize_t pixel_bytes;  // in the output
    TextureDecodeFn decode;
};

static const TextureFormat TEXTURE_FORMATS[] = {
        {"BC1",       "DXT1",        BCDEC_BC1_BLOCK_SIZE,  4, decode_bcn<BCDEC_BC1_BLOCK_SIZE, 4, bcdec_bc1>},
        {"BC2",       "DXT3",        BCDEC_BC2_BLOCK_SIZE,  4, decode_bcn<BCDEC_BC2_BLOCK_SIZE, 4, bcdec_bc2>},
        {"BC3",       "DXT5",        BCDEC_BC3_BLOCK_SIZE,  4, decode_bcn<BCDEC_BC3_BLOCK_SIZE, 4, bcdec_bc3>},
        {"BC4",       "ATI1N",       BCDEC_BC4_BLOCK_SIZE,  1, decode_bcn<BCDEC_BC4_BLOCK_SIZE, 1, bc4_unsigned>},
        {"BC4S",      "ATI1N_SNORM", BCDEC_BC4_BLOCK_SIZE,  1, decode_bcn<BCDEC_BC4_BLOCK_SIZE, 1, bc4_signed>},
        {"BC5",       "ATI2N",       BCDEC_BC5_BLOCK_SIZE,  2, decode_bcn<BCDEC_BC5_BLOCK_SIZE, 2, bc5_unsigned>},
        {"BC5S",      "ATI2N_SNORM", BCDEC_BC5_BLOCK_SIZE,  2, decode_bcn<BCDEC_BC5_BLOCK_SIZE, 2, bc5_signed>},
        {"BC6H",      "BC6H_UF16",   BCDEC_BC6H_BLOCK_SIZE, 6, decode_bc6<bcdec_bc6h_half_unsigned>},
        {"BC6H_SF16", nullptr,       BCDEC_BC6H_BLOCK_SIZE, 6, decode_bc6<bcdec_bc6h_half_signed>},
        {"BC7",       nullptr,       BCDEC_BC7_BLOCK_SIZE,  4, decode_bcn<BCDEC_BC7_BLOCK_SIZE, 4, bcdec_bc7>},
        {"ETC1",      nullptr,       8,                     4, decode_etc<decode_etc1>},
        {"ETC2",      nullptr,       8,                     4, decode_etc<decode_etc2>},
        {"ETC2_A1",   "ETC2_RGB_A1", 8,                     4, decode_etc<decode_etc2a1>},
        {"ETC2_EAC",  "ETC2_RGBA",   16,                    4, decode_etc<decode_etc2a8>},
        {"EAC_R11",   nullptr,       8,                     4, decode_etc<decode_eacr>},
        {"EAC_R11S",  nullptr,       8,                     4, decode_etc<decode_eacr_signed>},
        {"EAC_RG11",  nullptr,       16,                    4, decode_etc<decode_eacrg>},
        {"EAC_RG11S", nullptr,       16,                    4, decode_etc<decode_eacrg_signed>},
};

static PyObject *py_decode_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError,
                        "decode_texture(image_data, width, height, format) takes exactly 4 arguments");
        return nullptr;
    }
    PyROBytesView data_view(args[0]);
    if (!data_view) return nullptr;  // PyBytes_FromObject's TypeError
    if (!PyLong_Check(args[1]) || !PyLong_Check(args[2])) {
        PyErr_SetString(PyExc_TypeError, "width and height must be integers");
        return nullptr;
    }
    if (!PyUnicode_Check(args[3])) {
        PyErr_SetString(PyExc_TypeError, "format must be a str");
        return nullptr;
    }

    Py_ssize_t w = PyLong_AsSsize_t(args[1]);
    if (w == -1 && PyErr_Occurred()) return nullptr;
    Py_ssize_t h = PyLong_AsSsize_t(args[2]);
    if (h == -1 && PyErr_Occurred()) return nullptr;
    // bcdec takes the row pitch as an int: width * 4 bytes (BC6H: width * 3 halfs)
    // must fit.
    if (w <= 0 || h <= 0 || w > INT_MAX / 4 || h > INT_MAX) {
        PyErr_SetString(PyExc_ValueError, "width must be in 1..536870911 and height in 1..2147483647");
        return nullptr;
    }
    Py_ssize_t format_len = 0;
    const char *format_c = PyUnicode_AsUTF8AndSize(args[3], &format_len);
    if (!format_c)
        return nullptr;  // non-UTF8 str; converter set the error
    // Compared with the length so a name containing an embedded NUL compares
    // unequal instead of silently matching its prefix.
    auto matches = [&](const char *name) {
        return name && strlen(name) == (size_t) format_len && memcmp(name, format_c, (size_t) format_len) == 0;
    };
    const TextureFormat *fmt = nullptr;
    for (const TextureFormat &f: TEXTURE_FORMATS) {
        if (matches(f.name) || matches(f.alias)) {
            fmt = &f;
            break;
        }
    }
    if (!fmt) {
        PyErr_SetString(PyExc_ValueError,
                        "format must be one of: 'BC1', 'DXT1', 'BC2', 'DXT3', 'BC3', 'DXT5', 'BC4', 'ATI1N', "
                        "'BC4S', 'ATI1N_SNORM', 'BC5', 'ATI2N', 'BC5S', 'ATI2N_SNORM', 'BC6H', 'BC6H_UF16', "
                        "'BC6H_SF16', 'BC7', 'ETC1', 'ETC2', 'ETC2_A1', 'ETC2_RGB_A1', 'ETC2_EAC', 'ETC2_RGBA', "
                        "'EAC_R11', 'EAC_R11S', 'EAC_RG11', 'EAC_RG11S'");
        return nullptr;
    }

    // Block formats cover ceil(w/4) x ceil(h/4) blocks; partially covered edge
    // blocks (any mip smaller than 4 px) still occupy a full block in the input.
    Py_ssize_t blocks, need, out_size;
    if (!mul_checked_psszt((w + 3) / 4, (h + 3) / 4, &blocks) ||
        !mul_checked_psszt(blocks, fmt->block_bytes, &need) ||
        !mul3_checked_psszt(w, h, fmt->pixel_bytes, &out_size)) {
        PyErr_SetString(PyExc_OverflowError, "texture dimensions too large");
        return nullptr;
    }
    if (data_view.size() < (size_t) need) {
        PyErr_Format(PyExc_ValueError,
                     "image_data length (%zu) is smaller than %s compressed size (%zd) for %zd x %zd",
                     data_view.size(), fmt->name, need, w, h);
        return nullptr;
    }

    PyObject *result = PyBytes_FromStringAndSize(nullptr, out_size);
    if (!result) return nullptr;
    uint8_t *out = (uint8_t *) PyBytes_AsString(result);
    if (!out) {
        Py_DECREF(result);
        return nullptr;
    }
    const uint8_t *data = (const uint8_t *) data_view.data();
    Py_BEGIN_ALLOW_THREADS
        fmt->decode(data, out, (uint32_t) w, (uint32_t) h);
    Py_END_ALLOW_THREADS
    return result;
}

// ---------------------------------------------------------------------------
// Module definition

PyDoc_STRVAR(py_save_png_fn_doc,
             "save_png($module, image_data, width, height, channels, file_path, /)\n"
             "--\n"
             "\n"
             "Encode 8-bit image data as PNG and write it to file_path.\n"
             "\n"
             "image_data is any bytes-like object of width*height*channels bytes (rows\n"
             "tightly packed, top to bottom); channels is 1 (Y), 2 (YA), 3 (RGB) or 4 (RGBA).\n"
             "file_path must be a str. Returns None.\n"
             "Raises ValueError on bad dimensions/length, OverflowError if the image exceeds\n"
             "the encoder's int limits, OSError if the file cannot be written (a partially\n"
             "written file is removed), RuntimeError if encoding fails.\n"
);

PyDoc_STRVAR(py_encode_png_fn_doc,
             "encode_png($module, image_data, width, height, channels, /)\n"
             "--\n"
             "\n"
             "Encode 8-bit image data as PNG and return the file contents as bytes.\n"
             "\n"
             "image_data is any bytes-like object of width*height*channels bytes (rows\n"
             "tightly packed, top to bottom); channels is 1 (Y), 2 (YA), 3 (RGB) or 4 (RGBA).\n"
             "Raises ValueError on bad dimensions/length, OverflowError if the image exceeds\n"
             "the encoder's int limits, MemoryError/RuntimeError if encoding fails.\n"
);

PyDoc_STRVAR(py_save_exr_fn_doc,
             "save_exr($module, image_data, width, height, channels, file_path, /)\n"
             "--\n"
             "\n"
             "Encode float image data as EXR (stored as half floats) and write it to file_path.\n"
             "\n"
             "image_data is any bytes-like object holding width*height*channels native-endian\n"
             "float32 values, interleaved; channels is 1 (Y), 3 (RGB) or 4 (RGBA).\n"
             "file_path must be a str. Returns None.\n"
             "Raises ValueError on bad dimensions/length, OverflowError on oversized\n"
             "dimensions, OSError if the file cannot be written (a partially written file\n"
             "is removed), RuntimeError if encoding fails.\n"
);

PyDoc_STRVAR(py_encode_exr_fn_doc,
             "encode_exr($module, image_data, width, height, channels, /)\n"
             "--\n"
             "\n"
             "Encode float image data as EXR (stored as half floats) and return it as bytes.\n"
             "\n"
             "image_data is any bytes-like object holding width*height*channels native-endian\n"
             "float32 values, interleaved; channels is 1 (Y), 3 (RGB) or 4 (RGBA).\n"
             "Raises ValueError on bad dimensions/length, OverflowError on oversized\n"
             "dimensions, RuntimeError if encoding fails.\n"
);

PyDoc_STRVAR(py_decode_texture_fn_doc,
             "decode_texture($module, image_data, width, height, format, /)\n"
             "--\n"
             "\n"
             "Decode block-compressed texture data and return the pixels as bytes.\n"
             "\n"
             "image_data is any bytes-like object holding at least ceil(width/4)*ceil(height/4)\n"
             "4x4 blocks (extra trailing bytes are ignored). width must be in 1..536870911,\n"
             "height in 1..2147483647.\n"
             "\n"
             "Formats (block size) and output layout (tightly packed rows, width*height pixels):\n"
             "  'BC1'/'DXT1' (8), 'BC2'/'DXT3' (16), 'BC3'/'DXT5' (16), 'BC7' (16),\n"
             "  'ETC1' (8), 'ETC2' (8, RGB, A = 255): RGBA8 (uint8 x4).\n"
             "  'ETC2_A1'/'ETC2_RGB_A1' (8): RGBA8 with 1-bit alpha (A is 0 or 255).\n"
             "  'ETC2_EAC'/'ETC2_RGBA' (16): RGBA8 with 8-bit alpha.\n"
             "  'EAC_R11' (8): RGBA8 with the value in R, G = B = 0, A = 255 (11-bit value\n"
             "      reduced to 8 bits).\n"
             "  'EAC_RG11' (16): RGBA8 with the values in R and G, B = 0, A = 255.\n"
             "  'EAC_R11S' (8), 'EAC_RG11S' (16): as EAC_R11/EAC_RG11 for signed data, stored\n"
             "      biased as unsigned bytes (0 ~ -1.0, 127 ~ 0.0, 254 ~ +1.0).\n"
             "  'BC4'/'ATI1N' (8): R8 unsigned (uint8 x1).\n"
             "  'BC4S'/'ATI1N_SNORM' (8): R8 signed (int8 two's-complement x1, range -127..127).\n"
             "  'BC5'/'ATI2N' (16): RG8 unsigned (uint8 x2).\n"
             "  'BC5S'/'ATI2N_SNORM' (16): RG8 signed (int8 two's-complement x2, range -127..127).\n"
             "  'BC6H'/'BC6H_UF16' (16): RGB unsigned half floats (float16 x3).\n"
             "  'BC6H_SF16' (16): RGB signed half floats (float16 x3).\n"
             "Raises ValueError for an unknown format, bad dimensions or too little data.\n"
);

static PyMethodDef image_methods[] = {
        {"save_png",       CPF(py_save_png),       METH_FASTCALL, py_save_png_fn_doc},
        {"encode_png",     CPF(py_encode_png),     METH_FASTCALL, py_encode_png_fn_doc},
        {"save_exr",       CPF(py_save_exr),       METH_FASTCALL, py_save_exr_fn_doc},
        {"encode_exr",     CPF(py_encode_exr),     METH_FASTCALL, py_encode_exr_fn_doc},
        {"decode_texture", CPF(py_decode_texture), METH_FASTCALL, py_decode_texture_fn_doc},
        {nullptr, nullptr, 0,                                     nullptr}
};

static struct PyModuleDef image_module_def = {
        PyModuleDef_HEAD_INIT,
        "pylib.image",
        "Image encoding (PNG, EXR) and block-compressed texture decoding.",
        -1,
        image_methods
};

PyObject *ImageModule_Init(PyObject *parent_module) {
    return add_submodule(parent_module, "image", &image_module_def);
}
