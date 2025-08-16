#include <cmath>
#include "ext/stb_image_resize2.h"

#include "classes/vtf_class.h"
#include "utils/vtf_utils.h"
#include "VTFWrapper.h"

using namespace VTFLib;

inline void set_vtf_error(VTFLib::Diagnostics::CError &error) {
    const vlChar *msg = error.Get();
    if (msg && *msg) {
        PyErr_SetString(PyExc_RuntimeError, msg);
    } else {
        PyErr_SetString(PyExc_RuntimeError, "VTFLib error");
    }
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
    int header_only = 0;
    if (nargs == 2) header_only = PyObject_IsTrue(args[1]);

    PyObject *path_bytes = nullptr;
    if (!PyUnicode_FSConverter(args[0], &path_bytes)) return nullptr;
    const char *cpath = PyBytes_AsString(path_bytes);

    if (!cpath) {
        Py_DECREF(path_bytes);
        return nullptr;
    }

    VTFLib::Diagnostics::CError error;

    if (!self->file->Load(cpath, error, header_only ? vlTrue : vlFalse)) {
        Py_DECREF(path_bytes);
        set_vtf_error(error);
        return nullptr;
    }
    Py_DECREF(path_bytes);
    Py_RETURN_NONE;
}

PyObject *VTF_load_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 2) {
        PyErr_SetString(PyExc_TypeError, "load_bytes(data: bytes, header_only: bool = False)");
        return nullptr;
    }
    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    int header_only = 0;
    if (nargs == 2) header_only = PyObject_IsTrue(args[1]);

    const void *buf = PyBytes_AsString(args[0]);
    vlSize sz = (vlSize) PyBytes_Size(args[0]);
    VTFLib::Diagnostics::CError error;
    if (!self->file->Load(buf, sz, error, header_only ? vlTrue : vlFalse)) {
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
    PyObject *path_bytes = nullptr;
    if (!PyUnicode_FSConverter(args[0], &path_bytes)) return nullptr;
    const char *cpath = PyBytes_AsString(path_bytes);
    if (!cpath) {
        Py_DECREF(path_bytes);
        return nullptr;
    }
    VTFLib::Diagnostics::CError error;
    if (!self->file->Save(cpath, error)) {
        Py_DECREF(path_bytes);
        set_vtf_error(error);
        return nullptr;
    }
    Py_DECREF(path_bytes);
    Py_RETURN_NONE;
}

PyObject *VTF_to_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    (void) args;
    (void) nargs;
    vlUInt guess = self->file->GetSize();
    if (guess == 0) guess = 16 * 1024;
    thread_local uint8_t *tmp_buf = new uint8_t[guess];
    vlSize written = 0;
    VTFLib::Diagnostics::CError error;
    if (!self->file->Save(tmp_buf, (vlSize) guess, written, error)) {
        /* If buffer was too small, try again with the reported size */
        if (written > (vlSize) guess) {
            thread_local uint8_t *tmp_buf2 = new uint8_t[written];
            vlSize written2 = 0;
            VTFLib::Diagnostics::CError error2;
            if (!self->file->Save(tmp_buf2, written, written2, error2)) {
                delete[] tmp_buf2;
                set_vtf_error(error2);
                return nullptr;
            }
            PyObject *out2 = PyBytes_FromStringAndSize((const char *) (tmp_buf2), (Py_ssize_t) written2);
            if (!out2) return nullptr;
            delete[] tmp_buf2;
            return out2;
        }
        set_vtf_error(error);
        return nullptr;
    }

    PyObject *out = PyBytes_FromStringAndSize((const char *) tmp_buf, (Py_ssize_t) written);
    if (!out) return nullptr;
    delete[] tmp_buf;
    return out;
}

PyObject *VTF_create(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 2 || nargs > 8) {
        PyErr_SetString(PyExc_TypeError,
                        "create(width:int, height:int, frames=1, faces=1, slices=1, format=IMAGE_FORMAT_RGBA8888, thumbnail=True, mipmaps=True)");
        return nullptr;
    }
    Py_ssize_t width = PyLong_AsSsize_t(args[0]);
    Py_ssize_t height = PyLong_AsSsize_t(args[1]);
    if (width <= 0 || height <= 0) {
        PyErr_SetString(PyExc_ValueError, "width and height must be > 0");
        return nullptr;
    }
    Py_ssize_t frames = (nargs > 2) ? PyLong_AsSsize_t(args[2]) : 1;
    Py_ssize_t faces = (nargs > 3) ? PyLong_AsSsize_t(args[3]) : 1;
    Py_ssize_t slices = (nargs > 4) ? PyLong_AsSsize_t(args[4]) : 1;
    vlUInt fmt = (vlUInt) ((nargs > 5) ? PyLong_AsUnsignedLong(args[5]) : (vlUInt) IMAGE_FORMAT_RGBA8888);
    int thumbnail = (nargs > 6) ? PyObject_IsTrue(args[6]) : 1;
    int mipmaps = (nargs > 7) ? PyObject_IsTrue(args[7]) : 1;
    VTFLib::Diagnostics::CError error;
    if (!self->file->Create((vlUInt) width, (vlUInt) height, error, (vlUInt) frames, (vlUInt) faces, (vlUInt) slices,
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
    vlUInt frame = (vlUInt) PyLong_AsUnsignedLong(args[0]);
    vlUInt face = (vlUInt) PyLong_AsUnsignedLong(args[1]);
    vlUInt slice = (vlUInt) PyLong_AsUnsignedLong(args[2]);
    vlUInt mip = (vlUInt) PyLong_AsUnsignedLong(args[3]);
    if (!PyBytes_Check(args[4])) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    const char *buf = PyBytes_AsString(args[4]);
    Py_ssize_t blen = PyBytes_Size(args[4]);

    vlUInt w = 0, h = 0, d = 0;
    CVTFFile::ComputeMipmapDimensions(self->file->GetWidth(), self->file->GetHeight(), self->file->GetDepth(),
                                      mip, w, h, d);
    vlUInt need = CVTFFile::ComputeMipmapSize(w, h, d, mip, self->file->GetFormat());
    if ((Py_ssize_t) need != blen) {
        PyErr_Format(PyExc_ValueError, "data length %zd does not match required %u for this level", blen, need);
        return nullptr;
    }
    self->file->SetData(frame, face, slice, mip, (vlByte *) buf);
    Py_RETURN_NONE;
}

PyObject *VTF_get_data(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 4) {
        PyErr_SetString(PyExc_TypeError, "get_data(frame:int, face:int, slice:int, mip:int) -> bytes");
        return nullptr;
    }
    vlUInt frame = (vlUInt) PyLong_AsUnsignedLong(args[0]);
    vlUInt face = (vlUInt) PyLong_AsUnsignedLong(args[1]);
    vlUInt slice = (vlUInt) PyLong_AsUnsignedLong(args[2]);
    vlUInt mip = (vlUInt) PyLong_AsUnsignedLong(args[3]);

    vlByte *p = self->file->GetData(frame, face, slice, mip);
    if (!p) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get image data");
        return nullptr;
    }

    vlUInt w = 0, h = 0, d = 0;
    CVTFFile::ComputeMipmapDimensions(self->file->GetWidth(), self->file->GetHeight(), self->file->GetDepth(),
                                      mip, w, h, d);
    vlUInt size = CVTFFile::ComputeMipmapSize(w, h, d, mip, self->file->GetFormat());
    return PyBytes_FromStringAndSize((const char *) p, (Py_ssize_t) size);
}

PyObject *VTF_set_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 2) {
        PyErr_SetString(PyExc_TypeError, "set_flag(flag:int, state:bool)");
        return nullptr;
    }
    vlUInt flag = (vlUInt) PyLong_AsUnsignedLong(args[0]);
    int state = PyObject_IsTrue(args[1]);
    self->file->SetFlag((VTFImageFlag) flag, state ? vlTrue : vlFalse);
    Py_RETURN_NONE;
}

PyObject *VTF_get_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "get_flag(flag:int) -> bool");
        return nullptr;
    }
    vlUInt flag = (vlUInt) PyLong_AsUnsignedLong(args[0]);
    vlBool b = self->file->GetFlag((VTFImageFlag) flag);
    if (b) Py_RETURN_TRUE; else Py_RETURN_FALSE;
}

PyObject *VTF_flags(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    (void) args;
    (void) nargs;
    return PyLong_FromUnsignedLong(self->file->GetFlags());
}

PyObject *VTF_set_flags(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "set_flags(flags:int)");
        return nullptr;
    }
    vlUInt f = (vlUInt) PyLong_AsUnsignedLong(args[0]);
    self->file->SetFlags(f);
    Py_RETURN_NONE;
}

#undef max
#undef min

PyObject *VTF_generate_mipmaps(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    VTFMipmapFilter mf = MIPMAP_FILTER_BOX;
    VTFSharpenFilter sf = SHARPEN_FILTER_NONE;
    if (nargs > 0) mf = (VTFMipmapFilter) PyLong_AsUnsignedLong(args[0]);
    if (nargs > 1) sf = (VTFSharpenFilter) PyLong_AsUnsignedLong(args[1]);

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

    vlUInt width = self->file->GetWidth();
    vlUInt height = self->file->GetHeight();
    uint32_t mip_count = VTFLib::CVTFFile::ComputeMipmapCount(width, height, slice_count);
    bool is_float = is_float_storage(original_format);
    auto intermediate_format = is_float ? VTFImageFormat::IMAGE_FORMAT_RGBA32323232F
                                        : VTFImageFormat::IMAGE_FORMAT_RGBA8888;

    VTFLib::Diagnostics::CError error;
    for (int face = 0; face < face_count; ++face) {
        for (int frame = 0; frame < frame_count; ++frame) {
            auto orig_data = self->file->GetData(frame, face, 0, 0);
            auto rgba_buffer = new uint8_t[VTFLib::CVTFFile::ComputeImageSize(width, height, 1, intermediate_format)];
            if (!VTFLib::CVTFFile::Convert(orig_data, rgba_buffer, width, height, original_format, intermediate_format,
                                           error)) {
                set_vtf_error(error);
                return nullptr;
            }

            for (int mip = 1; mip < mip_count; ++mip) {
                uint32_t mip_width = 0;
                uint32_t mip_height = 0;
                uint32_t mip_depth = 0;
                VTFLib::CVTFFile::ComputeMipmapDimensions(width, height, 1, mip, mip_width, mip_height, mip_depth);
                auto resized_buffer = (uint8_t *) stbir_resize(
                        rgba_buffer,
                        (int) width, (int) height, (int) (width * 4),
                        nullptr,
                        (int) mip_width, (int) mip_height,
                        (int) (mip_width * 4),
                        STBIR_RGBA,
                        is_float ? STBIR_TYPE_FLOAT : STBIR_TYPE_UINT8,
                        STBIR_EDGE_CLAMP,
                        filter);
                if (!resized_buffer) {
                    free(resized_buffer);
                    PyErr_SetString(PyExc_RuntimeError, "Failed to resize image data for mipmap generation");
                    return nullptr;
                }
                auto mip_size = VTFLib::CVTFFile::ComputeMipmapSize(width, height, 0, mip, original_format);
                auto mip_buffer = new uint8_t[mip_size];
                VTFLib::Diagnostics::CError mip_error;
                if (!VTFLib::CVTFFile::Convert(resized_buffer, mip_buffer, mip_width, mip_height,
                                               intermediate_format, original_format, mip_error)) {
                    free(resized_buffer);
                    free(mip_buffer);
                    set_vtf_error(mip_error);
                    return nullptr;
                }

                self->file->SetData(frame, face, 0, mip, mip_buffer);
                free(resized_buffer);
                free(mip_buffer);
            }
        }
    }
    Py_RETURN_NONE;
}

PyObject *VTF_set_reflectivity(VTFObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 3) {
        PyErr_SetString(PyExc_TypeError, "set_reflectivity(x:float,y:float,z:float)");
        return nullptr;
    }
    float x = (float) PyFloat_AsDouble(args[0]);
    float y = (float) PyFloat_AsDouble(args[1]);
    float z = (float) PyFloat_AsDouble(args[2]);
    self->file->SetReflectivity(x, y, z);
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
    return PyLong_FromUnsignedLong(((VTFObject *) self)->file->GetFormat());
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
    ((VTFObject *) self)->file->SetFlags((vlUInt) f);
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
    self->file = new CVTFFile();
    if (!self->file) {
        Py_DECREF(self);
        PyErr_NoMemory();
        return nullptr;
    }
    return (PyObject *) self;
}

void VTF_dealloc(VTFObject *self) {
    delete self->file;
    freefunc(PyType_GetSlot(Py_TYPE(self), Py_tp_free))(self);
}

PyObject *VTF_create_from_data(VTFObject *self, PyObject *args, PyObject *kwargs) {
    static const char *kwlist[] = {
            "data", "width", "height",
            "frames", "faces", "slices",
            "image_format", "filter_mode", "flags",
            "generate_mipmaps", "generate_thumbnail",
            "resize_to_pow2", "resolution_limit_x", "resolution_limit_y",
            nullptr
    };

    PyObject *data_buf = nullptr;
    Py_ssize_t data_len = 0;

    Py_ssize_t width = 0, height = 0;
    Py_ssize_t frames = 1, faces = 1, slices = 1;

    tagVTFImageFormat image_format = IMAGE_FORMAT_RGBA8888;
    tagVTFMipmapFilter filter_mode = MIPMAP_FILTER_CATROM;
    tagVTFImageFlag flags = TEXTUREFLAGS_SRGB;

    int generate_mipmaps = 1;
    int generate_thumbnail = 1;
    int resize_to_pow2 = 1;
    Py_ssize_t resolution_limit_x = 4096;
    Py_ssize_t resolution_limit_y = 4096;

    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs,
            "Onn|"
            "nnn"
            "kkk"
            "pp"
            "nnn",
            kwlist,
            &data_buf, &width, &height,
            &frames, &faces, &slices,
            &image_format, &filter_mode, &flags,
            &generate_mipmaps, &generate_thumbnail,
            &resize_to_pow2, &resolution_limit_x, &resolution_limit_y)) {
        return nullptr;
    }
    if (!PyBytes_Check(data_buf)) {
        PyErr_SetString(PyExc_TypeError, "data must be bytes");
        return nullptr;
    }
    VTFLib::Diagnostics::CError error;
    SVTFCreateOptions options;
    vlImageCreateDefaultCreateStructure(&options);
    options.ImageFormat = image_format;
    options.bThumbnail = generate_thumbnail;
    options.bMipmaps = generate_mipmaps;
    switch (resize_to_pow2) {
        case 0:
            options.bResize = false;
            break;
        case 1:
            options.bResize = true;
            options.ResizeMethod = RESIZE_BIGGEST_POWER2;
            break;
        case 2:
            options.bResize = true;
            options.ResizeMethod = RESIZE_SMALLEST_POWER2;
            break;
        case 3:
            options.bResize = true;
            options.ResizeMethod = RESIZE_NEAREST_POWER2;
            break;
    }
    if (resolution_limit_x != width || resolution_limit_y != height) {
        options.bResizeClamp = true;
        options.uiResizeClampWidth = resolution_limit_x;
        options.uiResizeClampHeight = resolution_limit_y;
    }

    options.MipmapFilter = filter_mode;
    char *data = PyBytes_AsString(data_buf);
    if (!self->file->Create(width, height, frames, faces, slices, (vlByte **) &data, options, error)) {
        set_vtf_error(error);
        return nullptr;
    }
    if(!vlImageComputeReflectivity(self->file, &error)){
        set_vtf_error(error);
        return nullptr;
    }
    Py_RETURN_NONE;
}
