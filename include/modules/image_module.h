#ifndef PYLIB_IMAGE_MODULE_H
#define PYLIB_IMAGE_MODULE_H

#include <Python.h>
#include "utils/utils.h"


PyObject *py_save_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_encode_png(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_save_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_encode_exr(PyObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *py_decode_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs);


PyDoc_STRVAR(py_save_png_fn_doc,
             "save_png($module, /, image_data, width, height, channels, file_path)\n"
             "--\n"
             "\n"
             "Save image data as PNG file.\n"
);

PyDoc_STRVAR(py_encode_png_fn_doc,
             "encode_png($module, /, image_data, width, height, channels)\n"
             "--\n"
             "\n"
             "Encode image data to PNG format and return as bytes.\n"
);

PyDoc_STRVAR(py_save_exr_fn_doc,
             "save_exr($module, /, image_data, width, height, channels, file_path)\n"
             "--\n"
             "\n"
             "Save image data as EXR file.\n"
);

PyDoc_STRVAR(py_encode_exr_fn_doc,
             "encode_exr($module, /, image_data, width, height, channels)\n"
             "--\n"
             "\n"
             "Encode image data to EXR format and return as bytes.\n"
);

PyDoc_STRVAR(py_decode_texture_fn_doc,
             "decode_texture($module, /, image_data, width, height, format)\n"
             "--\n"
             "\n"
             "Decode texture data block compressed format.\n"
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
        "image",
        "SourceIO image module",
        -1,
        image_methods
};

static PyObject *ImageModule_Init(PyObject *parent_module) {
    return add_submodule(parent_module, "image", &image_module_def);
}

#endif //PYLIB_IMAGE_MODULE_H