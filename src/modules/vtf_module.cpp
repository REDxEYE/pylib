#include "modules/vtf_module.h"
#include "classes/vtf_class.h"
#include "utils/vtf_utils.h"
#include "VTFLib.h"
#include "VTFFormat.h"

PyObject *mod_get_version(PyObject *, PyObject *const *, Py_ssize_t) {
    const vlChar *s = vlGetVersionString();
    if (!s) Py_RETURN_NONE;
    return PyUnicode_FromString(s);
}

PyObject *py_load_vtf_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 4) {
        PyErr_SetString(PyExc_TypeError,
                        "load_vtf_texture(input_data: bytes, frame: int = 0, face: int = 0, mip: int = 0) "
                        "takes 1 to 4 arguments");
        return nullptr;
    }
    PyROBytesView data(args[0]);
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
    if ((vlUInt) frame >= vtf_file.GetFrameCount()) {
        PyErr_Format(PyExc_IndexError, "frame %ld out of range, texture has %u frame(s)",
                     frame, vtf_file.GetFrameCount());
        return nullptr;
    }
    if ((vlUInt) face >= vtf_file.GetFaceCount()) {
        PyErr_Format(PyExc_IndexError, "face %ld out of range, texture has %u face(s)",
                     face, vtf_file.GetFaceCount());
        return nullptr;
    }
    if ((vlUInt) mip >= vtf_file.GetMipmapCount()) {
        PyErr_Format(PyExc_IndexError, "mip %ld out of range, texture has %u mipmap(s)",
                     mip, vtf_file.GetMipmapCount());
        return nullptr;
    }

    bool is_float = is_float_storage(vtf_file.GetFormat());

    auto output_format = is_float ? VTFImageFormat::IMAGE_FORMAT_RGBA32323232F : VTFImageFormat::IMAGE_FORMAT_RGBA8888;

    // Mip levels are half-sized per step, so report the dimensions of the level
    // that was actually requested rather than those of the base image.
    vlUInt mip_width = 0, mip_height = 0, mip_depth = 0;
    VTFLib::CVTFFile::ComputeMipmapDimensions(vtf_file.GetWidth(), vtf_file.GetHeight(), vtf_file.GetDepth(),
                                              (vlUInt) mip, mip_width, mip_height, mip_depth);

    auto buffer_size = VTFLib::CVTFFile::ComputeMipmapSize(vtf_file.GetWidth(),
                                                           vtf_file.GetHeight(),
                                                           vtf_file.GetDepth(),
                                                           (vlUInt) mip,
                                                           output_format);
    auto buffer = PyBytes_FromStringAndSize(nullptr, buffer_size);
    if (!buffer) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to allocate output buffer");
        return nullptr;
    }
    VTFLib::Diagnostics::CError error2;
    if (!VTFLib::CVTFFile::Convert(vtf_file.GetData((vlUInt) frame, (vlUInt) face, 0, (vlUInt) mip),
                                   (uint8_t *) PyBytes_AsString(buffer), mip_width,
                                   mip_height, vtf_file.GetFormat(), output_format, error2)) {
        set_vtf_error(error2);
        Py_DECREF(buffer);
        return nullptr;
    }
    // Deliberately still a 4-tuple: existing callers unpack exactly four values,
    // and the frame count is already reachable via ``VTFFile.frame_count``.
    PyObject *res_tuple = PyTuple_New(4);
    if (!res_tuple) {
        Py_DECREF(buffer);
        return nullptr;
    }
    PyTuple_SetItem(res_tuple, 0, buffer);
    PyTuple_SetItem(res_tuple, 1, PyLong_FromUnsignedLong(mip_width));
    PyTuple_SetItem(res_tuple, 2, PyLong_FromUnsignedLong(mip_height));
    PyTuple_SetItem(res_tuple, 3, PyBool_FromLong(is_float));

    return res_tuple;
}

PyObject *py_load_vtf_texture_frames(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs < 1 || nargs > 3) {
        PyErr_SetString(PyExc_TypeError,
                        "load_vtf_texture_frames(input_data: bytes, face: int = 0, mip: int = 0) "
                        "takes 1 to 3 arguments");
        return nullptr;
    }
    PyROBytesView data(args[0]);
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
    if ((vlUInt) face >= vtf_file.GetFaceCount()) {
        PyErr_Format(PyExc_IndexError, "face %ld out of range, texture has %u face(s)",
                     face, vtf_file.GetFaceCount());
        return nullptr;
    }
    if ((vlUInt) mip >= vtf_file.GetMipmapCount()) {
        PyErr_Format(PyExc_IndexError, "mip %ld out of range, texture has %u mipmap(s)",
                     mip, vtf_file.GetMipmapCount());
        return nullptr;
    }

    bool is_float = is_float_storage(vtf_file.GetFormat());
    auto output_format = is_float ? VTFImageFormat::IMAGE_FORMAT_RGBA32323232F : VTFImageFormat::IMAGE_FORMAT_RGBA8888;

    vlUInt mip_width = 0, mip_height = 0, mip_depth = 0;
    VTFLib::CVTFFile::ComputeMipmapDimensions(vtf_file.GetWidth(), vtf_file.GetHeight(), vtf_file.GetDepth(),
                                              (vlUInt) mip, mip_width, mip_height, mip_depth);
    auto buffer_size = VTFLib::CVTFFile::ComputeMipmapSize(vtf_file.GetWidth(),
                                                           vtf_file.GetHeight(),
                                                           vtf_file.GetDepth(),
                                                           (vlUInt) mip,
                                                           output_format);

    vlUInt frame_count = vtf_file.GetFrameCount();
    PyObject *frames = PyList_New(frame_count);
    if (!frames) return nullptr;

    for (vlUInt frame = 0; frame < frame_count; ++frame) {
        auto buffer = PyBytes_FromStringAndSize(nullptr, buffer_size);
        if (!buffer) {
            PyErr_SetString(PyExc_RuntimeError, "Failed to allocate output buffer");
            Py_DECREF(frames);
            return nullptr;
        }
        VTFLib::Diagnostics::CError convert_error;
        if (!VTFLib::CVTFFile::Convert(vtf_file.GetData(frame, (vlUInt) face, 0, (vlUInt) mip),
                                       (uint8_t *) PyBytes_AsString(buffer), mip_width,
                                       mip_height, vtf_file.GetFormat(), output_format, convert_error)) {
            set_vtf_error(convert_error);
            Py_DECREF(buffer);
            Py_DECREF(frames);
            return nullptr;
        }
        // Steals the reference to buffer.
        if (PyList_SetItem(frames, frame, buffer) < 0) {
            Py_DECREF(buffer);
            Py_DECREF(frames);
            return nullptr;
        }
    }

    PyObject *res_tuple = PyTuple_New(4);
    if (!res_tuple) {
        Py_DECREF(frames);
        return nullptr;
    }
    PyTuple_SetItem(res_tuple, 0, frames);
    PyTuple_SetItem(res_tuple, 1, PyLong_FromUnsignedLong(mip_width));
    PyTuple_SetItem(res_tuple, 2, PyLong_FromUnsignedLong(mip_height));
    PyTuple_SetItem(res_tuple, 3, PyBool_FromLong(is_float));

    return res_tuple;
}
