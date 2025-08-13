#include "modules/vtf_module.h"
#include "vtf_class.h"
#include "vtf_utils.h"
#include "VTFLib.h"
#include "VTFFormat.h"

PyObject *mod_get_version(PyObject *, PyObject *const *, Py_ssize_t) {
    const vlChar *s = vlGetVersionString();
    if (!s) Py_RETURN_NONE;
    return PyUnicode_FromString(s);
}

PyObject *py_load_vtf_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs) {
    if (nargs != 1) {
        PyErr_SetString(PyExc_TypeError, "load_vtf_texture(input_data: bytes) takes exactly 1 argument");
        return nullptr;
    }
    if (!PyBytes_Check(args[0])) {
        PyErr_SetString(PyExc_TypeError, "input_data must be bytes");
        return nullptr;
    }
    const char *buf = PyBytes_AsString(args[0]);
    const size_t buf_size = PyBytes_Size(args[0]);

    if (!buf || buf_size == 0) {
        PyErr_SetString(PyExc_ValueError, "input_data must not be empty");
        return nullptr;
    }
    VTFLib::Diagnostics::CError error;
    auto vtf_file = VTFLib::CVTFFile();

    if (!vtf_file.Load(buf, buf_size, error, false)) {
        set_vtf_error(error);
        return nullptr;
    }
    bool is_float = is_float_storage(vtf_file.GetFormat());

    auto output_format = is_float ? VTFImageFormat::IMAGE_FORMAT_RGBA32323232F : VTFImageFormat::IMAGE_FORMAT_RGBA8888;

    auto buffer_size = VTFLib::CVTFFile::ComputeMipmapSize(vtf_file.GetWidth(),
                                                           vtf_file.GetHeight(),
                                                           vtf_file.GetDepth(),
                                                           0,
                                                           output_format);
    auto buffer = PyBytes_FromStringAndSize(nullptr, buffer_size);
    if (!buffer) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to allocate output buffer");
        return nullptr;
    }
    VTFLib::Diagnostics::CError error2;
    if (!VTFLib::CVTFFile::Convert(vtf_file.GetData(0, 0, 0, 0),
                                   (uint8_t *) PyBytes_AsString(buffer), vtf_file.GetWidth(),
                                   vtf_file.GetHeight(), vtf_file.GetFormat(), output_format, error)) {
        set_vtf_error(error2);
        Py_DECREF(buffer);
        return nullptr;
    }
    PyObject *res_tuple = PyTuple_New(4);
    if (!res_tuple) {
        Py_DECREF(buffer);
        return nullptr;
    }
    PyTuple_SetItem(res_tuple, 0, buffer);
    PyTuple_SetItem(res_tuple, 1, PyLong_FromUnsignedLong(vtf_file.GetWidth()));
    PyTuple_SetItem(res_tuple, 2, PyLong_FromUnsignedLong(vtf_file.GetHeight()));
    PyTuple_SetItem(res_tuple, 3, PyBool_FromLong(is_float));

    return res_tuple;
}
