#ifndef PYLIB_VTF_MODULE_H
#define PYLIB_VTF_MODULE_H

#include <vector>
#include <span>
#include <string>
#include <Python.h>
#include "utils/utils.h"
#include "classes/vtf_class.h"
#include "VTFFile.h"


PyObject *py_load_vtf_texture(PyObject *self, PyObject *const *args, Py_ssize_t nargs);


PyDoc_STRVAR(py_load_vtf_texture_fn_doc,
             "load_vtf_texture($module, /, input_data)\n"
             "--\n"
             "\n"
             "Load VTF texture from input data.\n"
);

PyDoc_STRVAR(mod_version_doc,
             "version($module, /)\n"
             "--\n"
             "\n"
             "Return VTFLib version string.");


PyObject *mod_get_version(PyObject *, PyObject *const *, Py_ssize_t);

static PyMethodDef vtf_methods[] = {
        {"load_vtf_texture", CPF(py_load_vtf_texture), METH_FASTCALL, py_load_vtf_texture_fn_doc},
        {"version",          CPF(mod_get_version),     METH_FASTCALL, mod_version_doc},
        {nullptr, nullptr, 0,                                         nullptr}
};


static struct PyModuleDef vtf_module_def = {
        PyModuleDef_HEAD_INIT,
        "pylib.vtf",
        "SourceIO vtf module",
        -1,
        vtf_methods,
};

static PyObject *VTFModule_Init(PyObject *parent_module) {
    PyObject *module = PyModule_Create(&vtf_module_def);
    if (!module) {
        Py_DECREF(module);
        return nullptr;
    }

    if (PyModule_AddObject(parent_module, "vtf", module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_SetItemString(modules, "pylib.vtf", module) < 0) {
        Py_DECREF(module);
        return nullptr;
    }

    PyObject *t = PyType_FromSpec(&vtf_class_spec);
    if (!t) return nullptr;
    if (PyModule_AddObject(module, "VTFFile", t) < 0) {
        Py_DECREF(t);
        return nullptr;
    }

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
    PyObject *my_enum;
    my_enum = create_int_enum("ImageFormat", image_format_members);
    if (!my_enum) return nullptr;

    if (PyModule_AddObject(module, "ImageFormat", my_enum) < 0) {
        Py_DECREF(my_enum);
        return nullptr;
    }

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

    my_enum = create_int_enum("MipFilter", mip_filter_members);
    if (PyModule_AddObject(module, "MipFilter", my_enum) < 0) {
        Py_DECREF(my_enum);
        return nullptr;
    }
    std::vector<std::pair<std::string, uint32_t>> constants{
            {"NONE",           SHARPEN_FILTER_NONE},
            {"NEGATIVE",       SHARPEN_FILTER_NEGATIVE},
            {"LIGHTER",        SHARPEN_FILTER_LIGHTER},
            {"DARKER",         SHARPEN_FILTER_DARKER},
            {"CONTRASTMORE",   SHARPEN_FILTER_CONTRASTMORE},
            {"CONTRASTLESS",   SHARPEN_FILTER_CONTRASTLESS},
            {"SMOOTHEN",       SHARPEN_FILTER_SMOOTHEN},
            {"SHARPENSOFT",    SHARPEN_FILTER_SHARPENSOFT},
            {"SHARPENMEDIUM",  SHARPEN_FILTER_SHARPENMEDIUM},
            {"SHARPENSTRONG",  SHARPEN_FILTER_SHARPENSTRONG},
            {"FINDEDGES",      SHARPEN_FILTER_FINDEDGES},
            {"CONTOUR",        SHARPEN_FILTER_CONTOUR},
            {"EDGEDETECT",     SHARPEN_FILTER_EDGEDETECT},
            {"EDGEDETECTSOFT", SHARPEN_FILTER_EDGEDETECTSOFT},
            {"EMBOSS",         SHARPEN_FILTER_EMBOSS},
            {"MEANREMOVAL",    SHARPEN_FILTER_MEANREMOVAL},
            {"UNSHARP",        SHARPEN_FILTER_UNSHARP},
            {"XSHARPEN",       SHARPEN_FILTER_XSHARPEN},
            {"WARPSHARP",      SHARPEN_FILTER_WARPSHARP},
    };

    my_enum = create_int_enum("SharpenFilter", constants);
    if (PyModule_AddObject(module, "SharpenFilter", my_enum) < 0) {
        Py_DECREF(my_enum);
        return nullptr;
    }
    std::vector<std::pair<std::string, uint32_t>> flags{
            {"POINTSAMPLE",                              TEXTUREFLAGS_POINTSAMPLE},
            {"TRILINEAR",                                TEXTUREFLAGS_TRILINEAR},
            {"CLAMPS",                                   TEXTUREFLAGS_CLAMPS},
            {"CLAMPT",                                   TEXTUREFLAGS_CLAMPT},
            {"ANISOTROPIC",                              TEXTUREFLAGS_ANISOTROPIC},
            {"HINT_DXT5",                                TEXTUREFLAGS_HINT_DXT5},
            {"SRGB",                                     TEXTUREFLAGS_SRGB},
            {"NORMAL",                                   TEXTUREFLAGS_NORMAL},
            {"NOMIP",                                    TEXTUREFLAGS_NOMIP},
            {"NOLOD",                                    TEXTUREFLAGS_NOLOD},
            {"MINMIP",                                   TEXTUREFLAGS_MINMIP},
            {"PROCEDURAL",                               TEXTUREFLAGS_PROCEDURAL},
            {"ONEBITALPHA",                              TEXTUREFLAGS_ONEBITALPHA},
            {"EIGHTBITALPHA",                            TEXTUREFLAGS_EIGHTBITALPHA},
            {"ENVMAP",                                   TEXTUREFLAGS_ENVMAP},
            {"RENDERTARGET",                             TEXTUREFLAGS_RENDERTARGET},
            {"DEPTHRENDERTARGET",                        TEXTUREFLAGS_DEPTHRENDERTARGET},
            {"NODEBUGOVERRIDE",                          TEXTUREFLAGS_NODEBUGOVERRIDE},
            {"SINGLECOPY",                               TEXTUREFLAGS_SINGLECOPY},
            {"NODEPTHBUFFER",                            TEXTUREFLAGS_NODEPTHBUFFER},
            {"CLAMPU",                                   TEXTUREFLAGS_CLAMPU},
            {"VERTEXTEXTURE",                            TEXTUREFLAGS_VERTEXTEXTURE},
            {"SSBUMP",                                   TEXTUREFLAGS_SSBUMP},
            {"BORDER",                                   TEXTUREFLAGS_BORDER},
    };

    my_enum = create_int_flags("TextureFlags", flags);
    if (PyModule_AddObject(module, "TextureFlags", my_enum) < 0) {
        Py_DECREF(my_enum);
        return nullptr;
    }

    // Set __path__ attribute for the submodule
    PyObject *path_list = Py_BuildValue("[s]", "pylib/vtf");
    if (path_list) {
        if (PyModule_AddObject(module, "__path__", path_list) < 0) {
            Py_DECREF(path_list);
            Py_DECREF(module);
            return nullptr;
        }
    }
    return module;
}

#endif //PYLIB_VTF_MODULE_H
