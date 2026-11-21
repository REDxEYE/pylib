#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
// Implement stb_image_resize2 here, with internal linkage: VTFLib ships its own
// stb_image_resize v0.96 whose `stbir_resize` has a different signature, and
// without a local implementation calls below were silently bound to that one.
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include "ext/stb_image_resize2.h"

#include "classes/vtf_class.h"
#include "utils/utils.h"
#include "utils/vtf_utils.h"
#include "VTFWrapper.h"

PyDoc_STRVAR(VTF_load_doc,
             "load($self, path, header_only=False, /)\n"
             "--\n"
             "\n"
             "Load a VTF from a file path. If header_only is True, only parse the header.\n"
             "Raises OSError if the file cannot be read and RuntimeError if VTFLib fails to load it.");

PyDoc_STRVAR(VTF_load_bytes_doc,
             "load_bytes($self, data, header_only=False, /)\n"
             "--\n"
             "\n"
             "Load a VTF from a bytes-like object. If header_only is True, only parse the header.\n"
             "Raises RuntimeError if VTFLib fails to parse the data.");

PyDoc_STRVAR(VTF_save_doc,
             "save($self, path, /)\n"
             "--\n"
             "\n"
             "Save the VTF to a file path. Raises OSError if the file cannot be written and\n"
             "RuntimeError if VTFLib fails to serialize the image.");

PyDoc_STRVAR(VTF_to_bytes_doc,
             "to_bytes($self, /)\n"
             "--\n"
             "\n"
             "Serialize the VTF to a bytes object (.vtf file contents). Raises RuntimeError on failure.");

PyDoc_STRVAR(VTF_create_doc,
             "create($self, width, height, frames=1, faces=1, slices=1, format=ImageFormat.RGBA8888, thumbnail=True, mipmaps=True, /)\n"
             "--\n"
             "\n"
             "Create a new, empty VTF image with the given dimensions, layout and format.\n"
             "All integer arguments must be non-negative and fit in 32 bits; width and height must be > 0.\n"
             "Raises RuntimeError if VTFLib rejects the parameters.");

PyDoc_STRVAR(
        VTF_create_from_data_doc,
        "create_from_data($self, data, width, height, frames=1, faces=1, slices=1, "
        "src_image_format=ImageFormat.RGBA8888, dst_image_format=ImageFormat.RGBA8888, "
        "filter_mode=MipFilter.CATROM, flags=TextureFlags.SRGB, generate_mipmaps=True, "
        "generate_thumbnail=True, resize_to_pow2=1, resolution_limit_x=4096, resolution_limit_y=4096)\n"
        "--\n"
        "\n"
        "Create a new VTF image from raw pixel data.\n"
        "\n"
        "Parameters\n"
        "----------\n"
        "data : bytes\n"
        "    Raw pixel data in src_image_format: frames * faces * slices consecutive\n"
        "    width x height images. Extra trailing bytes are ignored.\n"
        "width, height : int\n"
        "    Texture dimensions (> 0, < 2**32).\n"
        "frames, faces, slices : int, optional\n"
        "    Animation frames, cubemap faces, and depth slices (> 0, < 2**32); defaults to 1.\n"
        "src_image_format : ImageFormat or int, optional\n"
        "    Format of the pixels in data; default RGBA8888.\n"
        "dst_image_format : ImageFormat or int, optional\n"
        "    Storage format of the created VTF; default RGBA8888.\n"
        "filter_mode : MipFilter or int, optional\n"
        "    Filter used when generating mipmaps; default CATROM.\n"
        "flags : TextureFlags or int, optional\n"
        "    Only TextureFlags.NORMAL is applied here (it also makes mipmap generation treat alpha as an\n"
        "    independent channel); set other flags with the flags property or set_flag() afterwards.\n"
        "generate_mipmaps : bool, optional\n"
        "    Whether to generate mipmaps; default True.\n"
        "generate_thumbnail : bool, optional\n"
        "    Whether to embed a thumbnail; default True.\n"
        "resize_to_pow2 : int, optional\n"
        "    Power-of-two resize mode: 0=disabled, 1=biggest, 2=smallest, 3=nearest. Default 1.\n"
        "resolution_limit_x, resolution_limit_y : int, optional\n"
        "    Clamp final size (> 0); if either differs from (width, height), clamping is enabled.\n"
        "\n"
        "Raises\n"
        "------\n"
        "TypeError\n"
        "    If data is not bytes.\n"
        "ValueError\n"
        "    If a dimension is not positive, data is too short, or a format, filter_mode or\n"
        "    resize_to_pow2 value is out of range.\n"
        "OverflowError\n"
        "    If a dimension does not fit in 32 bits or the total size is too large.\n"
        "RuntimeError\n"
        "    If the underlying VTFLib call fails.\n"
);

PyDoc_STRVAR(VTF_set_data_doc,
             "set_data($self, frame, face, slice, mip, data, /)\n"
             "--\n"
             "\n"
             "Set the pixel data of one slice of the specified level. 'data' must be bytes in the\n"
             "storage format and exactly the size of that level (ValueError otherwise).\n"
             "Raises IndexError if frame/face/slice/mip is out of range.");

PyDoc_STRVAR(VTF_get_data_doc,
             "get_data($self, frame, face, slice, mip, /)\n"
             "--\n"
             "\n"
             "Return the pixel data of one slice of the specified level as bytes, in the storage format.\n"
             "Raises IndexError if frame/face/slice/mip is out of range.");

PyDoc_STRVAR(VTF_set_flag_doc,
             "set_flag($self, flag, state, /)\n"
             "--\n"
             "\n"
             "Enable or disable a VTF flag (TextureFlags or int).");

PyDoc_STRVAR(VTF_get_flag_doc,
             "get_flag($self, flag, /)\n"
             "--\n"
             "\n"
             "Return True if the given flag (TextureFlags or int) is set.");

PyDoc_STRVAR(VTF_get_version_doc,
             "get_version($self, /)\n"
             "--\n"
             "\n"
             "Return the VTF version as a (major, minor) tuple.");

PyDoc_STRVAR(VTF_set_version_doc,
             "set_version($self, major, minor, /)\n"
             "--\n"
             "\n"
             "Set the version of the VTF file. Raises RuntimeError if VTFLib rejects it.");

PyDoc_STRVAR(VTF_generate_mipmaps_doc,
             "generate_mipmaps($self, filter=MipFilter.BOX, /)\n"
             "--\n"
             "\n"
             "Regenerate every existing mip level from level 0 using the given MipFilter.\n"
             "Raises ValueError for an invalid filter and NotImplementedError for volume textures.");

PyDoc_STRVAR(VTF_set_reflectivity_doc,
             "set_reflectivity($self, x, y, z, /)\n"
             "--\n"
             "\n"
             "Set the reflectivity vector (RGB floats).");

PyDoc_STRVAR(VTF_compute_reflectivity_doc,
             "compute_reflectivity($self, /)\n"
             "--\n"
             "\n"
             "Compute reflectivity from image data. Raises RuntimeError on failure.");

PyDoc_STRVAR(VTF_width_prop_doc, "Image width (int); 0 when no image is loaded.");
PyDoc_STRVAR(VTF_height_prop_doc, "Image height (int); 0 when no image is loaded.");
PyDoc_STRVAR(VTF_format_prop_doc, "Storage format (ImageFormat value, int), or None when no image is loaded.");
PyDoc_STRVAR(VTF_frame_count_prop_doc, "Number of frames (int).");
PyDoc_STRVAR(VTF_face_count_prop_doc, "Number of faces (int).");
PyDoc_STRVAR(VTF_mipmap_count_prop_doc, "Number of mip levels (int).");
PyDoc_STRVAR(VTF_flags_prop_doc, "VTF flags bitmask (int). Read/write.");
PyDoc_STRVAR(VTF_bump_scale_prop_doc, "Bumpmap scale (float). Read/write.");

#ifdef _WIN32
// Converts a str path to a heap-allocated wide string (free with PyMem_Free).
// Returns nullptr with an exception set on failure.
static wchar_t *as_wide_path(PyObject *path) {
    return PyUnicode_AsWideCharString(path, nullptr);
}
#endif


void set_vtf_error(const VTFLib::Diagnostics::CError &error) {
    if (const char *msg = error.Get(); msg && *msg) {
        PyErr_SetString(PyExc_RuntimeError, msg);
    } else {
        PyErr_SetString(PyExc_RuntimeError, "VTFLib error");
    }
}

// Converts a Python int to a VTFLib `vlUInt`. Returns false with an exception
// set (TypeError / OverflowError) when `o` isn't an int or doesn't fit.
static bool as_u32(PyObject *o, uint32_t &out) {
    const unsigned long v = PyLong_AsUnsignedLong(o);
    if (v == (unsigned long) -1 && PyErr_Occurred()) return false;
    if (v > UINT32_MAX) {
        PyErr_SetString(PyExc_OverflowError, "value does not fit in a 32-bit unsigned integer");
        return false;
    }
    out = (uint32_t) v;
    return true;
}

// Python truthiness of an optional bool argument; -1 with an exception set on failure.
static int as_bool(PyObject *const *args, Py_ssize_t nargs, Py_ssize_t index, int def) {
    return nargs > index ? PyObject_IsTrue(args[index]) : def;
}

PyObject *VTF_load(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "load(path: str, header_only: bool = False)");
        return nullptr;
    }
    if (!PyUnicode_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "path must be str");
        return nullptr;
    }
    const int header_only = as_bool(args, nargs, 1, 0);
    if (header_only < 0) return nullptr;

    VTFLib::Diagnostics::CError error;
#ifdef _WIN32
    // VTFLib opens paths with narrow fopen, which can't express non-ASCII
    // names on Windows; read the file ourselves and load from memory.
    wchar_t *wpath = as_wide_path(args[0]);
    if (!wpath) return nullptr;
    std::vector<uint8_t> contents;
    bool read_ok = false;
    bool too_big = false;
    try {
        if (FILE *f = _wfopen(wpath, L"rb")) {
            uint8_t chunk[65536];
            size_t n;
            while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
                if (contents.size() + n > UINT32_MAX) {
                    too_big = true;
                    break;
                }
                contents.insert(contents.end(), chunk, chunk + n);
            }
            read_ok = !too_big && !ferror(f);
            fclose(f);
        }
    } catch (const std::bad_alloc &) {
        PyMem_Free(wpath);
        return PyErr_NoMemory();
    }
    if (too_big) {
        PyMem_Free(wpath);
        PyErr_SetString(PyExc_OverflowError, "VTF file is larger than 4 GiB");
        return nullptr;
    }
    if (!read_ok) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, args[0]);
        PyMem_Free(wpath);
        return nullptr;
    }
    PyMem_Free(wpath);
    if (!self->file->Load(contents.data(), (uint32_t) contents.size(), error, header_only ? vlTrue : vlFalse)) {
        set_vtf_error(error);
        return nullptr;
    }
#else
    PyObject *path_bytes = nullptr;
    if (!PyUnicode_FSConverter(args[0], &path_bytes)) return nullptr;
    const char *cpath = PyBytes_AsString(path_bytes);
    if (!cpath) {
        Py_DECREF(path_bytes);
        return nullptr;
    }
    if (!self->file->Load(cpath, error, header_only ? vlTrue : vlFalse)) {
        Py_DECREF(path_bytes);
        set_vtf_error(error);
        return nullptr;
    }
    Py_DECREF(path_bytes);
#endif
    Py_RETURN_NONE;
}

PyObject *VTF_load_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "load_bytes(data: bytes, header_only: bool = False)");
        return nullptr;
    }
    PyROBytesView data_view(args[0]);
    if (!data_view) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    const int header_only = as_bool(args, nargs, 1, 0);
    if (header_only < 0) return nullptr;

    VTFLib::Diagnostics::CError error;
    if (!self->file->Load(data_view.data(), data_view.size(), error, header_only ? vlTrue : vlFalse)) {
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject *VTF_save(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1 || !PyUnicode_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "save(path: str)");
        return nullptr;
    }
    VTFLib::Diagnostics::CError error;
#ifdef _WIN32
    // Serialize to memory, then write via _wfopen so non-ASCII paths work.
    uint32_t size = self->file->GetSize(error);
    if (size == 0) {
        set_vtf_error(error);
        return nullptr;
    }
    std::vector<uint8_t> buffer;
    try {
        buffer.resize(size);
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
    uint32_t written = 0;
    if (!self->file->Save(buffer.data(), (uint32_t) buffer.size(), written, error)) {
        set_vtf_error(error);
        return nullptr;
    }
    wchar_t *wpath = as_wide_path(args[0]);
    if (!wpath) return nullptr;
    bool write_ok = false;
    if (FILE *f = _wfopen(wpath, L"wb")) {
        write_ok = fwrite(buffer.data(), 1, written, f) == written;
        if (fclose(f) != 0) write_ok = false;
    }
    PyMem_Free(wpath);
    if (!write_ok) {
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, args[0]);
        return nullptr;
    }
#else
    PyObject *path_bytes = nullptr;
    if (!PyUnicode_FSConverter(args[0], &path_bytes)) return nullptr;
    const char *cpath = PyBytes_AsString(path_bytes);
    if (!cpath) {
        Py_DECREF(path_bytes);
        return nullptr;
    }
    if (!self->file->Save(cpath, error)) {
        Py_DECREF(path_bytes);
        set_vtf_error(error);
        return nullptr;
    }
    Py_DECREF(path_bytes);
#endif
    Py_RETURN_NONE;
}

PyObject *VTF_to_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    (void) args;
    (void) nargs;
    VTFLib::Diagnostics::CError error;
    uint32_t guess = self->file->GetSize(error);
    if (guess == 0) guess = 16 * 1024;

    // A std::vector, not a `thread_local` raw pointer: the previous version
    // initialized the buffer only on the first call per thread, so a later, larger
    // texture wrote past the original allocation -- and the buffer was deleted on
    // the way out, leaving the next call writing into freed memory.
    std::vector<uint8_t> buffer;
    try {
        buffer.resize(guess);
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
    uint32_t written = 0;
    if (!self->file->Save(buffer.data(), buffer.size(), written, error)) {
        // Buffer was too small: retry once at the size VTFLib reported.
        if (written <= buffer.size()) {
            set_vtf_error(error);
            return nullptr;
        }
        try {
            buffer.assign(written, 0);
        } catch (const std::bad_alloc &) {
            return PyErr_NoMemory();
        }
        uint32_t written_retry = 0;
        VTFLib::Diagnostics::CError retry_error;
        if (!self->file->Save(buffer.data(), buffer.size(), written_retry, retry_error)) {
            set_vtf_error(retry_error);
            return nullptr;
        }
        written = written_retry;
    }
    return PyBytes_FromStringAndSize((const char *) buffer.data(), (Py_ssize_t) written);
}

PyObject *VTF_create(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 2 || nargs > 8) {
        PyErr_SetString(PyExc_TypeError,
                        "create(width:int, height:int, frames=1, faces=1, slices=1, format=IMAGE_FORMAT_RGBA8888, thumbnail=True, mipmaps=True)");
        return nullptr;
    }
    uint32_t width = 0, height = 0, frames = 1, faces = 1, slices = 1;
    uint32_t fmt = IMAGE_FORMAT_RGBA8888;
    if (!as_u32(args[0], width) || !as_u32(args[1], height)) return nullptr;
    if (width == 0 || height == 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return nullptr;
    }
    if (nargs > 2 && !as_u32(args[2], frames)) return nullptr;
    if (nargs > 3 && !as_u32(args[3], faces)) return nullptr;
    if (nargs > 4 && !as_u32(args[4], slices)) return nullptr;
    if (nargs > 5 && !as_u32(args[5], fmt)) return nullptr;
    const int thumbnail = as_bool(args, nargs, 6, 1);
    if (thumbnail < 0) return nullptr;
    const int mipmaps = as_bool(args, nargs, 7, 1);
    if (mipmaps < 0) return nullptr;
    VTFLib::Diagnostics::CError error;
    if (!self->file->Create(width, height, error, frames, faces, slices,
                            (VTFImageFormat) fmt, thumbnail ? vlTrue : vlFalse, mipmaps ? vlTrue : vlFalse, vlFalse)) {
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject *VTF_set_data(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 5) {
        PyErr_SetString(PyExc_TypeError, "set_data(frame:int, face:int, slice:int, mip:int, data: bytes)");
        return nullptr;
    }
    uint32_t frame = 0, face = 0, slice = 0, mip = 0;
    if (!as_u32(args[0], frame) || !as_u32(args[1], face) || !as_u32(args[2], slice) || !as_u32(args[3], mip))
        return nullptr;
    if (!PyBytes_Check(args[4])) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    const char *buf = PyBytes_AsString(args[4]);
    Py_ssize_t blen = PyBytes_Size(args[4]);

    if (PyErr_Occurred())
        return nullptr;
    if (!self->file->IsLoaded() || frame >= self->file->GetFrameCount() || face >= self->file->GetFaceCount() ||
        slice >= self->file->GetDepth() || mip >= self->file->GetMipmapCount()) {
        PyErr_SetString(PyExc_IndexError, "frame/face/slice/mip out of range");
        return nullptr;
    }
    // Size of one slice of this level. ComputeMipmapSize applies the mip shift
    // itself, so it takes the base dimensions; this is exactly what SetData copies.
    const uint32_t need = VTFLib::CVTFFile::ComputeMipmapSize(self->file->GetWidth(), self->file->GetHeight(), 1,
                                                              mip, self->file->GetFormat());
    if ((Py_ssize_t) need != blen) {
        PyErr_Format(PyExc_ValueError, "data length %zd does not match required %u for this level", blen, need);
        return nullptr;
    }
    self->file->SetData(frame, face, slice, mip, (uint8_t *) buf);
    Py_RETURN_NONE;
}

PyObject *VTF_get_data(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError, "get_data(frame:int, face:int, slice:int, mip:int) -> bytes");
        return nullptr;
    }
    uint32_t frame = 0, face = 0, slice = 0, mip = 0;
    if (!as_u32(args[0], frame) || !as_u32(args[1], face) || !as_u32(args[2], slice) || !as_u32(args[3], mip))
        return nullptr;
    if (!self->file->IsLoaded() || frame >= self->file->GetFrameCount() || face >= self->file->GetFaceCount() ||
        slice >= self->file->GetDepth() || mip >= self->file->GetMipmapCount()) {
        PyErr_SetString(PyExc_IndexError, "frame/face/slice/mip out of range");
        return nullptr;
    }

    uint8_t *p = self->file->GetData(frame, face, slice, mip);
    if (!p) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get image data");
        return nullptr;
    }

    // One slice of the requested level (base dimensions; the mip shift is
    // applied inside ComputeMipmapSize).
    const uint32_t size = VTFLib::CVTFFile::ComputeMipmapSize(self->file->GetWidth(), self->file->GetHeight(), 1,
                                                              mip, self->file->GetFormat());
    return PyBytes_FromStringAndSize(reinterpret_cast<const char *>(p), size);
}

PyObject *VTF_set_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "set_flag(flag:int, state:bool)");
        return nullptr;
    }
    uint32_t flag = 0;
    if (!as_u32(args[0], flag)) return nullptr;
    const int state = PyObject_IsTrue(args[1]);
    if (state < 0) return nullptr;
    self->file->SetFlag(static_cast<VTFImageFlag>(flag), state ? vlTrue : vlFalse);
    Py_RETURN_NONE;
}

PyObject *VTF_get_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "get_flag(flag:int) -> bool");
        return nullptr;
    }
    uint32_t flag = 0;
    if (!as_u32(args[0], flag)) return nullptr;
    const vlBool b = self->file->GetFlag(static_cast<VTFImageFlag>(flag));
    if (b)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

PyObject *VTF_get_version(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    return Py_BuildValue("(kk)", (unsigned long) self->file->GetMajorVersion(),
                         (unsigned long) self->file->GetMinorVersion());
}

PyObject *VTF_set_version(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "set_version(major:int, minor:int)");
        return nullptr;
    }

    uint32_t major = 0, minor = 0;
    if (!as_u32(args[0], major) || !as_u32(args[1], minor)) return nullptr;
    if (VTFLib::Diagnostics::CError error; !self->file->SetVersion(major, minor, error)) {
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}

#undef max
#undef min

PyObject *VTF_generate_mipmaps(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs > 1) {
        PyErr_SetString(PyExc_TypeError, "generate_mipmaps(filter: MipFilter = MipFilter.BOX)");
        return nullptr;
    }
    uint32_t mf_value = MIPMAP_FILTER_BOX;
    if (nargs > 0 && !as_u32(args[0], mf_value)) return nullptr;
    if (mf_value >= MIPMAP_FILTER_COUNT) {
        PyErr_Format(PyExc_ValueError, "invalid mipmap filter %u", mf_value);
        return nullptr;
    }
    const auto mf = (VTFMipmapFilter) mf_value;

    stbir_filter filter = STBIR_FILTER_BOX;
    switch (mf) {
        case MIPMAP_FILTER_POINT:
            filter = STBIR_FILTER_POINT_SAMPLE;
            break;
        case MIPMAP_FILTER_BOX:
        case MIPMAP_FILTER_COUNT:
            filter = STBIR_FILTER_BOX;
            break;
        case MIPMAP_FILTER_TRIANGLE:
            filter = STBIR_FILTER_TRIANGLE;
            break;
        case MIPMAP_FILTER_QUADRATIC:
        case MIPMAP_FILTER_CUBIC:
            filter = STBIR_FILTER_CUBICBSPLINE;
            break;
        case MIPMAP_FILTER_CATROM:
            filter = STBIR_FILTER_CATMULLROM;
            break;
        case MIPMAP_FILTER_MITCHELL:
            filter = STBIR_FILTER_MITCHELL;
            break;
        case MIPMAP_FILTER_GAUSSIAN:
        case MIPMAP_FILTER_SINC:
        case MIPMAP_FILTER_BESSEL:
        case MIPMAP_FILTER_HANNING:
        case MIPMAP_FILTER_HAMMING:
        case MIPMAP_FILTER_BLACKMAN:
        case MIPMAP_FILTER_KAISER:
            filter = STBIR_FILTER_CUBICBSPLINE;
            break;
    }

    auto original_format = self->file->GetFormat();
    auto face_count = self->file->GetFaceCount();
    auto frame_count = self->file->GetFrameCount();
    auto slice_count = self->file->GetDepth();
    if (slice_count != 1) {
        PyErr_SetString(PyExc_NotImplementedError, "Mipmap generation for 3D textures is not implemented");
        return nullptr;
    }

    uint32_t width = self->file->GetWidth();
    uint32_t height = self->file->GetHeight();
    // Only levels that exist in the file can be written; asking VTFLib for a
    // level past the end clamps to the last one and overwrites it.
    uint32_t mip_count = self->file->GetMipmapCount();
    bool is_float = is_float_storage(original_format);
    auto intermediate_format = is_float
                                   ? VTFImageFormat::IMAGE_FORMAT_RGBA32323232F
                                   : VTFImageFormat::IMAGE_FORMAT_RGBA8888;
    const uint32_t pixel_bytes = is_float ? 16u : 4u;
    // A normal map's alpha is an independent mask (e.g. phong), not coverage: don't weight RGB by it.
    const bool independent_alpha = self->file->GetFlag(TEXTUREFLAGS_NORMAL);

    // VTFLib has no identity conversion, so same-format "conversion" is a copy.
    auto convert = [&](uint8_t *src, uint8_t *dst, uint32_t w, uint32_t h, VTFImageFormat from,
                       VTFImageFormat to, VTFLib::Diagnostics::CError &err) -> bool {
        if (from == to) {
            memcpy(dst, src, VTFLib::CVTFFile::ComputeImageSize(w, h, 1, from));
            return true;
        }
        return VTFLib::CVTFFile::Convert(src, dst, w, h, from, to, err);
    };

    VTFLib::Diagnostics::CError error;
    try {
    for (uint32_t face = 0; face < face_count; ++face) {
        for (uint32_t frame = 0; frame < frame_count; ++frame) {
            auto orig_data = self->file->GetData(frame, face, 0, 0);
            std::vector<uint8_t> rgba_buffer(
                VTFLib::CVTFFile::ComputeImageSize(width, height, 1, intermediate_format));
            if (!convert(orig_data, rgba_buffer.data(), width, height, original_format, intermediate_format, error)) {
                set_vtf_error(error);
                return nullptr;
            }

            // Each level is resized from the previous one (not from level 0), so the
            // work shrinks geometrically. The previous level stays at intermediate precision.
            std::unique_ptr<uint8_t, decltype(&free)> prev_buffer(nullptr, &free);
            const uint8_t *prev = rgba_buffer.data();
            uint32_t prev_width = width;
            uint32_t prev_height = height;
            for (uint32_t mip = 1; mip < mip_count; ++mip) {
                uint32_t mip_width = 0;
                uint32_t mip_height = 0;
                uint32_t mip_depth = 0;
                VTFLib::CVTFFile::ComputeMipmapDimensions(width, height, 1, mip, mip_width, mip_height, mip_depth);
                // stbir_resize mallocs its own output; owned here so it's freed on every path.
                std::unique_ptr<uint8_t, decltype(&free)> resized_buffer((uint8_t *) stbir_resize(
                    prev,
                    static_cast<int>(prev_width), static_cast<int>(prev_height),
                    static_cast<int>(prev_width * pixel_bytes),
                    nullptr,
                    static_cast<int>(mip_width), static_cast<int>(mip_height),
                    static_cast<int>(mip_width * pixel_bytes),
                    independent_alpha ? STBIR_RGBA_PM : STBIR_RGBA,
                    is_float ? STBIR_TYPE_FLOAT : STBIR_TYPE_UINT8,
                    STBIR_EDGE_CLAMP,
                    filter), &free);
                if (!resized_buffer) {
                    PyErr_SetString(PyExc_RuntimeError, "Failed to resize image data for mipmap generation");
                    return nullptr;
                }
                const auto mip_size = VTFLib::CVTFFile::ComputeMipmapSize(width, height, 1, mip, original_format);
                std::vector<uint8_t> mip_buffer(mip_size);
                VTFLib::Diagnostics::CError mip_error;
                if (!convert(resized_buffer.get(), mip_buffer.data(), mip_width, mip_height, intermediate_format,
                             original_format, mip_error)) {
                    set_vtf_error(mip_error);
                    return nullptr;
                }

                self->file->SetData(frame, face, 0, mip, mip_buffer.data());

                prev_buffer = std::move(resized_buffer);
                prev = prev_buffer.get();
                prev_width = mip_width;
                prev_height = mip_height;
            }
        }
    }
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
    Py_RETURN_NONE;
}

PyObject *VTF_set_reflectivity(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError, "set_reflectivity(x:float,y:float,z:float)");
        return nullptr;
    }
    const double x = PyFloat_AsDouble(args[0]);
    if (x == -1.0 && PyErr_Occurred()) return nullptr;
    const double y = PyFloat_AsDouble(args[1]);
    if (y == -1.0 && PyErr_Occurred()) return nullptr;
    const double z = PyFloat_AsDouble(args[2]);
    if (z == -1.0 && PyErr_Occurred()) return nullptr;
    self->file->SetReflectivity((float) x, (float) y, (float) z);
    Py_RETURN_NONE;
}

PyObject *VTF_compute_reflectivity(VTFObject *self, PyObject *const *a, Py_ssize_t n) {
    (void) a;
    (void) n;
    VTFLib::Diagnostics::CError error;
    if (!self->file->ComputeReflectivity(error)) {
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject *VTF_width_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetWidth());
}

PyObject *VTF_height_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetHeight());
}

PyObject *VTF_format_get(PyObject *self, void *) {
    const VTFImageFormat format = ((VTFObject *) self)->file->GetFormat();
    if (format == IMAGE_FORMAT_NONE)
        Py_RETURN_NONE;
    return PyLong_FromLong((long) format);
}

PyObject *VTF_frame_count_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetFrameCount());
}

PyObject *VTF_face_count_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetFaceCount());
}

PyObject *VTF_mipmap_count_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetMipmapCount());
}

/* flags: read/write mask */
PyObject *VTF_flags_get(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetFlags());
}

int VTF_flags_set(PyObject *self, PyObject *value, void *) {
    if (!value) return 0; /* deletion not supported */
    unsigned long f = PyLong_AsUnsignedLong(value);
    if (PyErr_Occurred()) return -1;
    ((VTFObject *) self)->file->SetFlags((uint32_t) f);
    return 0;
}

/* bump_scale: float read/write */
PyObject *VTF_bump_scale_get(PyObject *self, void *) {
    return PyFloat_FromDouble(((VTFObject *) self)->file->GetBumpmapScale());
}

int VTF_bump_scale_set(PyObject *self, PyObject *value, void *) {
    if (!value) return 0;
    double d = PyFloat_AsDouble(value);
    if (PyErr_Occurred()) return -1;
    ((VTFObject *) self)->file->SetBumpmapScale((float) d);
    return 0;
}

PyObject *VTF_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    (void) args;
    (void) kwargs;
    VTFObject *self = (VTFObject *) PyType_GenericNew(type, nullptr, nullptr);
    if (!self) return nullptr;
    try {
        self->file = new VTFLib::CVTFFile();
    } catch (const std::bad_alloc &) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }
    return (PyObject *) self;
}

void VTF_dealloc(VTFObject *self) {
    delete self->file;
    PyTypeObject *tp = Py_TYPE((PyObject *) self);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))((PyObject *) self);
    Py_DECREF(tp);
}

PyObject *VTF_create_from_data(VTFObject *self, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {
        "data", "width", "height",
        "frames", "faces", "slices",
        "src_image_format", "dst_image_format", "filter_mode", "flags",
        "generate_mipmaps", "generate_thumbnail",
        "resize_to_pow2", "resolution_limit_x", "resolution_limit_y",
        nullptr
    };

    PyObject *data_buf = nullptr;

    Py_ssize_t width = 0, height = 0;
    Py_ssize_t frames = 1, faces = 1, slices = 1;

    // Parsed as `unsigned long` and cast afterwards: passing the address of a
    // 4-byte enum to a `k`/`n` conversion writes 8 bytes and corrupts whatever
    // follows it on the stack.
    unsigned long src_image_format = IMAGE_FORMAT_RGBA8888;
    unsigned long dst_image_format = IMAGE_FORMAT_RGBA8888;
    unsigned long filter_mode = MIPMAP_FILTER_CATROM;
    unsigned long flags = TEXTUREFLAGS_SRGB;

    int generate_mipmaps = 1;
    int generate_thumbnail = 1;
    // Py_ssize_t, not int: an `n` conversion writes 8 bytes, so an `int` here
    // overwrote the neighbouring resolution limits.
    Py_ssize_t resize_to_pow2 = 1;
    Py_ssize_t resolution_limit_x = 4096;
    Py_ssize_t resolution_limit_y = 4096;

    if (!PyArg_ParseTupleAndKeywords(
        args, kwargs,
        "Onn|" // data, width, height
        "nnn" // frames, faces, slices
        "kkkk" // src_image_format, dst_image_format, filter_mode, flags
        "pp" // generate_mipmaps, generate_thumbnail
        "nnn", // resize_to_pow2, resolution_limit_x, resolution_limit_y
        const_cast<char **>(kwlist),
        &data_buf, &width, &height,
        &frames, &faces, &slices,
        &src_image_format, &dst_image_format, &filter_mode, &flags,
        &generate_mipmaps, &generate_thumbnail,
        &resize_to_pow2, &resolution_limit_x, &resolution_limit_y)) {
        return nullptr;
    }
    if (!PyBytes_Check(data_buf)) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    // Validate everything before any size is computed: VTFLib takes `vlUInt`
    // dimensions and indexes format tables by the enum value.
    if (width <= 0 || height <= 0)
        return PyErr_Format(PyExc_ValueError, "width and height must be > 0");
    if (frames <= 0 || faces <= 0 || slices <= 0)
        return PyErr_Format(PyExc_ValueError, "frames, faces and slices must be > 0");
    if ((uint64_t) width > UINT32_MAX || (uint64_t) height > UINT32_MAX || (uint64_t) frames > UINT32_MAX ||
        (uint64_t) faces > UINT32_MAX || (uint64_t) slices > UINT32_MAX)
        return PyErr_Format(PyExc_OverflowError, "width, height, frames, faces and slices must fit in 32 bits");
    if (resolution_limit_x <= 0 || resolution_limit_y <= 0 || (uint64_t) resolution_limit_x > UINT32_MAX ||
        (uint64_t) resolution_limit_y > UINT32_MAX)
        return PyErr_Format(PyExc_ValueError, "resolution limits must be in [1, 2**32)");
    if (src_image_format >= IMAGE_FORMAT_COUNT || dst_image_format >= IMAGE_FORMAT_COUNT)
        return PyErr_Format(PyExc_ValueError, "invalid image format");
    if (filter_mode >= MIPMAP_FILTER_COUNT)
        return PyErr_Format(PyExc_ValueError, "invalid filter_mode %lu", filter_mode);
    if (resize_to_pow2 < 0 || resize_to_pow2 > 3)
        return PyErr_Format(PyExc_ValueError, "resize_to_pow2 must be 0 (off), 1 (biggest), 2 (smallest) or 3 (nearest)");

    const auto w = (uint32_t) width, h = (uint32_t) height;
    VTFLib::Diagnostics::CError error;
    SVTFCreateOptions options;
    vlImageCreateDefaultCreateStructure(&options);
    options.imageFormat = (tagVTFImageFormat) dst_image_format;
    options.thumbnail = generate_thumbnail;
    options.mipmaps = generate_mipmaps;
    switch (resize_to_pow2) {
        case 0:
            options.resize = false;
            break;
        case 1:
            options.resize = true;
            options.resizeMethod = RESIZE_BIGGEST_POWER2;
            break;
        case 2:
            options.resize = true;
            options.resizeMethod = RESIZE_SMALLEST_POWER2;
            break;
        default:
            options.resize = true;
            options.resizeMethod = RESIZE_NEAREST_POWER2;
            break;
    }
    if (resolution_limit_x != width || resolution_limit_y != height) {
        options.resizeClamp = true;
        options.resizeClampWidth = (uint32_t) resolution_limit_x;
        options.resizeClampHeight = (uint32_t) resolution_limit_y;
    }

    options.mipmapFilter = (tagVTFMipmapFilter) filter_mode;
    // The other flags stay unapplied (callers set them afterwards); NORMAL must be known while mips are built.
    options.flags = (uint32_t) flags & TEXTUREFLAGS_NORMAL;
    auto data = PyROBytesView(data_buf);
    if (!data) return nullptr;

    // `Create` is documented as `lpImageDataRGBA8888` and converts to
    // `options.ImageFormat` itself, so the input must be
    // RGBA8888 -- converting straight to `dst_image_format` here (as the previous
    // version did) both mis-sized the buffer and handed Create data in a format it
    // would then reinterpret, crashing on any non-RGBA8888 destination.
    //
    // It also takes an array of `frames * faces * slices` image pointers, one per
    // stored image; the previous version passed the address of a single buffer, so
    // any call with more than one frame/face/slice read past it.
    // All factors are < 2**32; mul_checked_psszt rejects products past PY_SSIZE_T_MAX.
    Py_ssize_t image_count = 0, src_total = 0, rgba_total = 0;
    // ComputeImageSize returns a 32-bit size; 16 bytes is the widest pixel format.
    if ((uint64_t) w * h > UINT32_MAX / 16)
        return PyErr_Format(PyExc_OverflowError, "image dimensions are too large");
    const Py_ssize_t src_image_size = VTFLib::CVTFFile::ComputeImageSize(
        w, h, 1, (tagVTFImageFormat) src_image_format);
    const Py_ssize_t rgba_image_size = VTFLib::CVTFFile::ComputeImageSize(w, h, 1, IMAGE_FORMAT_RGBA8888);
    if (!mul3_checked_psszt((Py_ssize_t) frames, (Py_ssize_t) faces, (Py_ssize_t) slices, &image_count) ||
        !mul_checked_psszt(src_image_size, image_count, &src_total) ||
        !mul_checked_psszt(rgba_image_size, image_count, &rgba_total) ||
        image_count > (Py_ssize_t) UINT32_MAX)
        return PyErr_Format(PyExc_OverflowError, "total image size is too large");
    if ((Py_ssize_t) data.size() < src_total) {
        return PyErr_Format(PyExc_ValueError,
                            "data length (%zd) is too small for %zd image(s) of %zd bytes",
                            (Py_ssize_t) data.size(), image_count, src_image_size);
    }

    std::vector<uint8_t> rgba_data;
    std::vector<uint8_t *> image_pointers;
    try {
        rgba_data.resize(rgba_total);
        image_pointers.resize(image_count);
    } catch (const std::bad_alloc &) {
        return PyErr_NoMemory();
    }
    for (Py_ssize_t image = 0; image < image_count; ++image) {
        auto *dst = rgba_data.data() + rgba_image_size * image;
        image_pointers[image] = dst;
        if (!VTFLib::CVTFFile::Convert((uint8_t *) data.data() + src_image_size * image, dst,
                                       w, h, (tagVTFImageFormat) src_image_format,
                                       IMAGE_FORMAT_RGBA8888, error)) {
            set_vtf_error(error);
            return nullptr;
        }
    }
    if (!self->file->Create(w, h, (uint32_t) frames, (uint32_t) faces, (uint32_t) slices, image_pointers.data(),
                            options, error)) {
        set_vtf_error(error);
        return nullptr;
    }
    if (!self->file->ComputeReflectivity(error)) {
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}

PyObject *vtf_repr(VTFObject *self) {
    if (!self->file->IsLoaded())
        return PyUnicode_FromString("<VTFFile: empty>");
    return PyUnicode_FromFormat("<VTFFile: %ux%u, format=%d, frames=%u, faces=%u, slices=%u>",
                                self->file->GetWidth(),
                                self->file->GetHeight(),
                                (int) self->file->GetFormat(),
                                self->file->GetFrameCount(),
                                self->file->GetFaceCount(),
                                self->file->GetDepth());
}

static PyGetSetDef vtf_class_getset[] = {
        {"width",        getter(VTF_width_get),        nullptr,                    VTF_width_prop_doc,        nullptr},
        {"height",       getter(VTF_height_get),       nullptr,                    VTF_height_prop_doc,       nullptr},
        {"format",       getter(VTF_format_get),       nullptr,                    VTF_format_prop_doc,       nullptr},
        {"frame_count",  getter(VTF_frame_count_get),  nullptr,                    VTF_frame_count_prop_doc,  nullptr},
        {"face_count",   getter(VTF_face_count_get),   nullptr,                    VTF_face_count_prop_doc,   nullptr},
        {"mipmap_count", getter(VTF_mipmap_count_get), nullptr,                    VTF_mipmap_count_prop_doc, nullptr},
        {"flags",        getter(VTF_flags_get),        setter(VTF_flags_set),      VTF_flags_prop_doc,        nullptr},
        {"bump_scale",   getter(VTF_bump_scale_get),   setter(VTF_bump_scale_set), VTF_bump_scale_prop_doc,   nullptr},
        {nullptr,        nullptr,                      nullptr,                    nullptr,                   nullptr}
};

static PyMethodDef vtf_class_methods[] = {
        {"load",                 CPF(VTF_load),                 METH_FASTCALL, VTF_load_doc},
        {"load_bytes",           CPF(VTF_load_bytes),           METH_FASTCALL, VTF_load_bytes_doc},
        {"save",                 CPF(VTF_save),                 METH_FASTCALL, VTF_save_doc},
        {"to_bytes",             CPF(VTF_to_bytes),             METH_FASTCALL, VTF_to_bytes_doc},
        {"create",               CPF(VTF_create),               METH_FASTCALL, VTF_create_doc},
        {"create_from_data",     CPF(VTF_create_from_data),     METH_VARARGS | METH_KEYWORDS, VTF_create_from_data_doc},
        {"set_data",             CPF(VTF_set_data),             METH_FASTCALL, VTF_set_data_doc},
        {"get_data",             CPF(VTF_get_data),             METH_FASTCALL, VTF_get_data_doc},
        {"set_flag",             CPF(VTF_set_flag),             METH_FASTCALL, VTF_set_flag_doc},
        {"get_flag",             CPF(VTF_get_flag),             METH_FASTCALL, VTF_get_flag_doc},
        {"set_version",          CPF(VTF_set_version),          METH_FASTCALL, VTF_set_version_doc},
        {"get_version",          CPF(VTF_get_version),          METH_FASTCALL, VTF_get_version_doc},
        {"generate_mipmaps",     CPF(VTF_generate_mipmaps),     METH_FASTCALL, VTF_generate_mipmaps_doc},
        {"set_reflectivity",     CPF(VTF_set_reflectivity),     METH_FASTCALL, VTF_set_reflectivity_doc},
        {"compute_reflectivity", CPF(VTF_compute_reflectivity), METH_FASTCALL, VTF_compute_reflectivity_doc},
        {nullptr,                nullptr,                       0,             nullptr}
};

static PyType_Slot vtf_class_slots[] = {
        {Py_tp_new,     (void *) VTF_new},
        {Py_tp_dealloc, (void *) VTF_dealloc},
        {Py_tp_methods, (void *) vtf_class_methods},
        {Py_tp_getset,  (void *) vtf_class_getset},
        {Py_tp_repr,    (void *) vtf_repr},
        {0,             nullptr}
};

PyType_Spec vtf_class_spec = {
        "pylib.vtf.VTFFile",
        sizeof(VTFObject),
        0,
        Py_TPFLAGS_DEFAULT,
        vtf_class_slots
};
