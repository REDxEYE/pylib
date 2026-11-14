#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "modules/vtf_module.h"
#include "classes/vtf_class.h"
#include "utils/utils.h"
#include "utils/vtf_utils.h"
#include "VTFLib.h"
#include "VTFFormat.h"

PyDoc_STRVAR(py_load_vtf_texture_fn_doc,
             "load_vtf_texture($module, input_data, frame=0, face=0, mip=0, /)\n"
             "--\n"
             "\n"
             "Load VTF texture from input data, converted to RGBA8888 (or\n"
             "RGBA32323232F for float formats).\n"
             "\n"
             "Parameters\n"
             "----------\n"
             "input_data : bytes\n"
             "    Raw .vtf file contents.\n"
             "frame : int, optional\n"
             "    Animation frame to decode; defaults to 0. Use ``VTFFile.frame_count``\n"
             "    to enumerate the frames of an animated texture.\n"
             "face : int, optional\n"
             "    Cubemap face to decode; defaults to 0.\n"
             "mip : int, optional\n"
             "    Mipmap level to decode; defaults to 0 (full resolution).\n"
             "\n"
             "Returns\n"
             "-------\n"
             "tuple\n"
             "    ``(pixel_data, width, height, is_float)``, where width/height are\n"
             "    those of the requested mip level.\n"
             "\n"
             "Raises\n"
             "------\n"
             "TypeError\n"
             "    If input_data is not bytes or an index is not an int.\n"
             "ValueError\n"
             "    If input_data is empty or an index is negative.\n"
             "IndexError\n"
             "    If frame, face or mip is out of range for this texture.\n"
             "OverflowError\n"
             "    If the decoded level would exceed 4 GiB.\n"
             "RuntimeError\n"
             "    If VTFLib fails to parse or convert the texture.\n"
);

PyDoc_STRVAR(py_load_vtf_texture_frames_fn_doc,
             "load_vtf_texture_frames($module, input_data, face=0, mip=0, /)\n"
             "--\n"
             "\n"
             "Load every animation frame of a VTF texture in one pass.\n"
             "\n"
             "Equivalent to calling ``load_vtf_texture`` once per frame, but parses\n"
             "the file a single time instead of re-parsing it for each frame.\n"
             "\n"
             "Parameters\n"
             "----------\n"
             "input_data : bytes\n"
             "    Raw .vtf file contents.\n"
             "face : int, optional\n"
             "    Cubemap face to decode; defaults to 0.\n"
             "mip : int, optional\n"
             "    Mipmap level to decode; defaults to 0 (full resolution).\n"
             "\n"
             "Returns\n"
             "-------\n"
             "tuple\n"
             "    ``(frames, width, height, is_float)`` where ``frames`` is a list of\n"
             "    bytes objects, one per animation frame, each already converted to\n"
             "    RGBA8888 (or RGBA32323232F for float formats).\n"
             "\n"
             "Raises\n"
             "------\n"
             "TypeError, ValueError, OverflowError, RuntimeError\n"
             "    As for ``load_vtf_texture``.\n"
             "IndexError\n"
             "    If face or mip is out of range for this texture.\n"
);

PyDoc_STRVAR(mod_version_doc,
             "version($module, /)\n"
             "--\n"
             "\n"
             "Return the VTFLib version string, or None if unavailable.");

// Byte size of one decoded mip level (all depth slices) in `output_format`,
// computed in 64 bits. Returns -1 with OverflowError set if it exceeds what
// VTFLib (32-bit sizes) or a bytes object can hold.
static Py_ssize_t decoded_mip_size(uint32_t mip_width, uint32_t mip_height, uint32_t mip_depth,
                                   VTFImageFormat output_format) {
    const uint64_t bpp = output_format == IMAGE_FORMAT_RGBA32323232F ? 16 : 4;
    const uint64_t size = (uint64_t) mip_width * mip_height * mip_depth * bpp;
    if (size > UINT32_MAX || size > (uint64_t) PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "decoded texture level is too large");
        return -1;
    }
    return (Py_ssize_t) size;
}

static PyObject *mod_get_version(PyObject *, PyObject *const *, Py_ssize_t) {
    const char *s = vlGetVersionString();
    if (!s)
        Py_RETURN_NONE;
    return PyUnicode_FromString(s);
}

// Converts every depth slice of one mip level into `dst` as consecutive
// slice-sized chunks, so volume textures leave no part of the buffer unwritten.
static bool convert_mip_slices(VTFLib::CVTFFile &vtf_file, uint32_t frame, uint32_t face, uint32_t mip,
                               uint32_t mip_width, uint32_t mip_height, uint32_t mip_depth,
                               VTFImageFormat output_format, uint8_t *dst, VTFLib::Diagnostics::CError &error) {
    const uint32_t slice_size = VTFLib::CVTFFile::ComputeImageSize(mip_width, mip_height, 1, output_format);
    for (uint32_t slice = 0; slice < mip_depth; ++slice) {
        error.Clear();
        if (!VTFLib::CVTFFile::Convert(vtf_file.GetData(frame, face, slice, mip), dst + (size_t) slice_size * slice,
                                       mip_width, mip_height, vtf_file.GetFormat(), output_format, error))
            return false;
    }
    return true;
}

static PyObject *py_load_vtf_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 4) {
        PyErr_SetString(PyExc_TypeError,
                        "load_vtf_texture(input_data: bytes, frame: int = 0, face: int = 0, mip: int = 0) "
                        "takes 1 to 4 arguments");
        return nullptr;
    }
    const PyROBytesView data(args[0]);
    if (!data) {
        PyErr_SetString(PyExc_TypeError, "input_data must be bytes");
        return nullptr;
    }
    if (data.size() == 0) {
        PyErr_SetString(PyExc_ValueError, "input_data must not be empty");
        return nullptr;
    }

    long frame = 0, face = 0, mip = 0;
    long *const indices[3] = {&frame, &face, &mip};
    static const char *index_names[3] = {"frame", "face", "mip"};
    for (Py_ssize_t i = 1; i < nargs; ++i) {
        if (!PyLong_Check(args[i])) {
            PyErr_Format(PyExc_TypeError, "%s must be an int", index_names[i - 1]);
            return nullptr;
        }
        *indices[i - 1] = PyLong_AsLong(args[i]);
        if (*indices[i - 1] == -1 && PyErr_Occurred()) return nullptr;
        if (*indices[i - 1] < 0) {
            PyErr_Format(PyExc_ValueError, "%s must not be negative", index_names[i - 1]);
            return nullptr;
        }
    }

    VTFLib::Diagnostics::CError error;
    auto vtf_file = VTFLib::CVTFFile();

    if (!vtf_file.Load(data.data(), data.size(), error, false)) {
        set_vtf_error(error);
        return nullptr;
    }

    // Animated textures store one image per frame, cubemaps one per face, and
    // every level of the mip chain is addressable. Bounds-check here so an
    // out-of-range index raises instead of handing VTFLib a bogus pointer.
    if (frame >= vtf_file.GetFrameCount()) {
        PyErr_Format(PyExc_IndexError, "frame %ld out of range, texture has %u frame(s)",
                     frame, vtf_file.GetFrameCount());
        return nullptr;
    }
    if (face >= vtf_file.GetFaceCount()) {
        PyErr_Format(PyExc_IndexError, "face %ld out of range, texture has %u face(s)",
                     face, vtf_file.GetFaceCount());
        return nullptr;
    }
    if (mip >= vtf_file.GetMipmapCount()) {
        PyErr_Format(PyExc_IndexError, "mip %ld out of range, texture has %u mipmap(s)",
                     mip, vtf_file.GetMipmapCount());
        return nullptr;
    }

    const bool is_float = is_float_storage(vtf_file.GetFormat());
    const auto output_format = is_float ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;

    // Mip levels are half-sized per step, so report the dimensions of the level
    // that was actually requested rather than those of the base image.
    uint32_t mip_width = 0, mip_height = 0, mip_depth = 0;
    VTFLib::CVTFFile::ComputeMipmapDimensions(vtf_file.GetWidth(), vtf_file.GetHeight(), vtf_file.GetDepth(),
                                              mip, mip_width, mip_height, mip_depth);

    const Py_ssize_t buffer_size = decoded_mip_size(mip_width, mip_height, mip_depth, output_format);
    if (buffer_size < 0) return nullptr;
    const auto buffer = PyBytes_FromStringAndSize(nullptr, buffer_size);
    if (!buffer) return nullptr;
    if (!convert_mip_slices(vtf_file, (uint32_t) frame, (uint32_t) face, (uint32_t) mip, mip_width, mip_height,
                            mip_depth, output_format, reinterpret_cast<uint8_t *>(PyBytes_AsString(buffer)),
                            error)) {
        set_vtf_error(error);
        Py_DECREF(buffer);
        return nullptr;
    }
    // Deliberately still a 4-tuple: existing callers unpack exactly four values,
    // and the frame count is already reachable via ``VTFFile.frame_count``.
    return Py_BuildValue("(NkkO)", buffer, (unsigned long) mip_width, (unsigned long) mip_height,
                         is_float ? Py_True : Py_False);
}

static PyObject *py_load_vtf_texture_frames(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 3) {
        PyErr_SetString(PyExc_TypeError,
                        "load_vtf_texture_frames(input_data: bytes, face: int = 0, mip: int = 0) "
                        "takes 1 to 3 arguments");
        return nullptr;
    }
    const PyROBytesView data(args[0]);
    if (!data) {
        PyErr_SetString(PyExc_TypeError, "input_data must be bytes");
        return nullptr;
    }
    if (data.size() == 0) {
        PyErr_SetString(PyExc_ValueError, "input_data must not be empty");
        return nullptr;
    }

    long face = 0, mip = 0;
    long *const indices[2] = {&face, &mip};
    static const char *index_names[2] = {"face", "mip"};
    for (Py_ssize_t i = 1; i < nargs; ++i) {
        if (!PyLong_Check(args[i])) {
            PyErr_Format(PyExc_TypeError, "%s must be an int", index_names[i - 1]);
            return nullptr;
        }
        *indices[i - 1] = PyLong_AsLong(args[i]);
        if (*indices[i - 1] == -1 && PyErr_Occurred()) return nullptr;
        if (*indices[i - 1] < 0) {
            PyErr_Format(PyExc_ValueError, "%s must not be negative", index_names[i - 1]);
            return nullptr;
        }
    }

    VTFLib::Diagnostics::CError error;
    auto vtf_file = VTFLib::CVTFFile();

    if (!vtf_file.Load(data.data(), data.size(), error, false)) {
        set_vtf_error(error);
        return nullptr;
    }
    if (face >= vtf_file.GetFaceCount()) {
        PyErr_Format(PyExc_IndexError, "face %ld out of range, texture has %u face(s)",
                     face, vtf_file.GetFaceCount());
        return nullptr;
    }
    if (mip >= vtf_file.GetMipmapCount()) {
        PyErr_Format(PyExc_IndexError, "mip %ld out of range, texture has %u mipmap(s)",
                     mip, vtf_file.GetMipmapCount());
        return nullptr;
    }

    const bool is_float = is_float_storage(vtf_file.GetFormat());
    const auto output_format = is_float ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;

    uint32_t mip_width = 0, mip_height = 0, mip_depth = 0;
    VTFLib::CVTFFile::ComputeMipmapDimensions(vtf_file.GetWidth(), vtf_file.GetHeight(), vtf_file.GetDepth(),
                                              mip, mip_width, mip_height, mip_depth);
    const Py_ssize_t buffer_size = decoded_mip_size(mip_width, mip_height, mip_depth, output_format);
    if (buffer_size < 0) return nullptr;

    const uint32_t frame_count = vtf_file.GetFrameCount();
    PyObject *frames = PyList_New(frame_count);
    if (!frames) return nullptr;

    for (uint32_t frame = 0; frame < frame_count; ++frame) {
        const auto buffer = PyBytes_FromStringAndSize(nullptr, buffer_size);
        if (!buffer) {
            Py_DECREF(frames);
            return nullptr;
        }
        if (!convert_mip_slices(vtf_file, frame, (uint32_t) face, (uint32_t) mip, mip_width, mip_height,
                                mip_depth, output_format, reinterpret_cast<uint8_t *>(PyBytes_AsString(buffer)),
                                error)) {
            set_vtf_error(error);
            Py_DECREF(buffer);
            Py_DECREF(frames);
            return nullptr;
        }
        // Steals the reference to buffer, even on failure.
        if (PyList_SetItem(frames, frame, buffer) < 0) {
            Py_DECREF(frames);
            return nullptr;
        }
    }

    return Py_BuildValue("(NkkO)", frames, (unsigned long) mip_width, (unsigned long) mip_height,
                         is_float ? Py_True : Py_False);
}

static PyMethodDef vtf_methods[] = {
        {"load_vtf_texture",        CPF(py_load_vtf_texture),        METH_FASTCALL, py_load_vtf_texture_fn_doc},
        {"load_vtf_texture_frames", CPF(py_load_vtf_texture_frames), METH_FASTCALL, py_load_vtf_texture_frames_fn_doc},
        {"version",                 CPF(mod_get_version),            METH_FASTCALL, mod_version_doc},
        {nullptr,                   nullptr,                         0,             nullptr}
};

static PyModuleDef vtf_module_def = {
        PyModuleDef_HEAD_INIT,
        "pylib.vtf",
        "SourceIO vtf module",
        -1,
        vtf_methods,
};

PyObject *VTFModule_Init(PyObject *parent_module) {
    PyObject *module = add_submodule(parent_module, "vtf", &vtf_module_def);
    if (!module)
        return nullptr;

    if (add_type(module, "VTFFile", &vtf_class_spec) < 0)
        return nullptr;

    std::vector<std::pair<std::string, uint32_t>> image_format_members = {
            {"RGBA8888",          IMAGE_FORMAT_RGBA8888},
            {"ABGR8888",          IMAGE_FORMAT_ABGR8888},
            {"RGB888",            IMAGE_FORMAT_RGB888},
            {"BGR888",            IMAGE_FORMAT_BGR888},
            {"RGB565",            IMAGE_FORMAT_RGB565},
            {"I8",                IMAGE_FORMAT_I8},
            {"IA88",              IMAGE_FORMAT_IA88},
            {"P8",                IMAGE_FORMAT_P8},
            {"A8",                IMAGE_FORMAT_A8},
            {"RGB888_BLUESCREEN", IMAGE_FORMAT_RGB888_BLUESCREEN},
            {"BGR888_BLUESCREEN", IMAGE_FORMAT_BGR888_BLUESCREEN},
            {"ARGB8888",          IMAGE_FORMAT_ARGB8888},
            {"BGRA8888",          IMAGE_FORMAT_BGRA8888},
            {"DXT1",              IMAGE_FORMAT_DXT1},
            {"DXT3",              IMAGE_FORMAT_DXT3},
            {"DXT5",              IMAGE_FORMAT_DXT5},
            {"BGRX8888",          IMAGE_FORMAT_BGRX8888},
            {"BGR565",            IMAGE_FORMAT_BGR565},
            {"BGRX5551",          IMAGE_FORMAT_BGRX5551},
            {"BGRA4444",          IMAGE_FORMAT_BGRA4444},
            {"DXT1_ONEBITALPHA",  IMAGE_FORMAT_DXT1_ONEBITALPHA},
            {"BGRA5551",          IMAGE_FORMAT_BGRA5551},
            {"UV88",              IMAGE_FORMAT_UV88},
            {"UVWQ8888",          IMAGE_FORMAT_UVWQ8888},
            {"RGBA16161616F",     IMAGE_FORMAT_RGBA16161616F},
            {"RGBA16161616",      IMAGE_FORMAT_RGBA16161616},
            {"UVLX8888",          IMAGE_FORMAT_UVLX8888},
            {"R32F",              IMAGE_FORMAT_R32F},
            {"RGB323232F",        IMAGE_FORMAT_RGB323232F},
            {"RGBA32323232F",     IMAGE_FORMAT_RGBA32323232F},
            {"NV_DST16",          IMAGE_FORMAT_NV_DST16},
            {"NV_DST24",          IMAGE_FORMAT_NV_DST24},
            {"NV_INTZ",           IMAGE_FORMAT_NV_INTZ},
            {"NV_RAWZ",           IMAGE_FORMAT_NV_RAWZ},
            {"ATI_DST16",         IMAGE_FORMAT_ATI_DST16},
            {"ATI_DST24",         IMAGE_FORMAT_ATI_DST24},
            {"NV_NULL",           IMAGE_FORMAT_NV_NULL},
            {"ATI2N",             IMAGE_FORMAT_ATI2N},
            {"ATI1N",             IMAGE_FORMAT_ATI1N}
    };
    if (add_int_enum(module, "ImageFormat", image_format_members) < 0)
        return nullptr;

    std::vector<std::pair<std::string, uint32_t>> mip_filter_members = {
            {"POINT",     MIPMAP_FILTER_POINT},
            {"BOX",       MIPMAP_FILTER_BOX},
            {"TRIANGLE",  MIPMAP_FILTER_TRIANGLE},
            {"QUADRATIC", MIPMAP_FILTER_QUADRATIC},
            {"CUBIC",     MIPMAP_FILTER_CUBIC},
            {"CATROM",    MIPMAP_FILTER_CATROM},
            {"MITCHELL",  MIPMAP_FILTER_MITCHELL},
            {"GAUSSIAN",  MIPMAP_FILTER_GAUSSIAN},
            {"SINC",      MIPMAP_FILTER_SINC},
            {"BESSEL",    MIPMAP_FILTER_BESSEL},
            {"HANNING",   MIPMAP_FILTER_HANNING},
            {"HAMMING",   MIPMAP_FILTER_HAMMING},
            {"BLACKMAN",  MIPMAP_FILTER_BLACKMAN},
            {"KAISER",    MIPMAP_FILTER_KAISER}
    };
    if (add_int_enum(module, "MipFilter", mip_filter_members) < 0)
        return nullptr;

    std::vector<std::pair<std::string, uint32_t>> texture_flags{
            {"POINTSAMPLE",       TEXTUREFLAGS_POINTSAMPLE},
            {"TRILINEAR",         TEXTUREFLAGS_TRILINEAR},
            {"CLAMPS",            TEXTUREFLAGS_CLAMPS},
            {"CLAMPT",            TEXTUREFLAGS_CLAMPT},
            {"ANISOTROPIC",       TEXTUREFLAGS_ANISOTROPIC},
            {"HINT_DXT5",         TEXTUREFLAGS_HINT_DXT5},
            {"SRGB",              TEXTUREFLAGS_SRGB},
            {"NORMAL",            TEXTUREFLAGS_NORMAL},
            {"NOMIP",             TEXTUREFLAGS_NOMIP},
            {"NOLOD",             TEXTUREFLAGS_NOLOD},
            {"MINMIP",            TEXTUREFLAGS_MINMIP},
            {"PROCEDURAL",        TEXTUREFLAGS_PROCEDURAL},
            {"ONEBITALPHA",       TEXTUREFLAGS_ONEBITALPHA},
            {"EIGHTBITALPHA",     TEXTUREFLAGS_EIGHTBITALPHA},
            {"ENVMAP",            TEXTUREFLAGS_ENVMAP},
            {"RENDERTARGET",      TEXTUREFLAGS_RENDERTARGET},
            {"DEPTHRENDERTARGET", TEXTUREFLAGS_DEPTHRENDERTARGET},
            {"NODEBUGOVERRIDE",   TEXTUREFLAGS_NODEBUGOVERRIDE},
            {"SINGLECOPY",        TEXTUREFLAGS_SINGLECOPY},
            {"NODEPTHBUFFER",     TEXTUREFLAGS_NODEPTHBUFFER},
            {"CLAMPU",            TEXTUREFLAGS_CLAMPU},
            {"VERTEXTEXTURE",     TEXTUREFLAGS_VERTEXTEXTURE},
            {"SSBUMP",            TEXTUREFLAGS_SSBUMP},
            {"BORDER",            TEXTUREFLAGS_BORDER},
    };
    if (add_int_flags(module, "TextureFlags", texture_flags) < 0)
        return nullptr;

    return module;
}
