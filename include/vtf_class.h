#ifndef PYLIB_VTF_CLASS_H
#define PYLIB_VTF_CLASS_H

#include <Python.h>
#include "utils.h"
#include <VTFFile.h>

void set_vtf_error(VTFLib::Diagnostics::CError &error);

struct VTFObject {
    PyObject_HEAD
    VTFLib::CVTFFile *file;
};

PyDoc_STRVAR(VTF_load_doc,
             "load($self, /, path, header_only=False)\n"
             "--\n"
             "\n"
             "Load a VTF from a file path. If header_only is True, only parse the header.");

PyDoc_STRVAR(VTF_load_bytes_doc,
             "load_bytes($self, /, data, header_only=False)\n"
             "--\n"
             "\n"
             "Load a VTF from a bytes object. If header_only is True, only parse the header.");

PyDoc_STRVAR(VTF_save_doc,
             "save($self, /, path)\n"
             "--\n"
             "\n"
             "Save the VTF to a file path.");

PyDoc_STRVAR(VTF_to_bytes_doc,
             "to_bytes($self, /)\n"
             "--\n"
             "\n"
             "Serialize the VTF to a bytes object (.vtf file contents).");

PyDoc_STRVAR(VTF_create_doc,
             "create($self, /, width, height, frames=1, faces=1, slices=1,\n"
             "       format=IMAGE_FORMAT_RGBA8888, thumbnail=True, mipmaps=True)\n"
             "--\n"
             "\n"
             "Create a new VTF image with the given dimensions, layout and format.");

PyDoc_STRVAR(VTF_set_data_doc,
             "set_data($self, /, frame, face, slice, mip, data)\n"
             "--\n"
             "\n"
             "Set pixel data for the specified level. 'data' must match the storage\n"
             "format and size for that level.");

PyDoc_STRVAR(VTF_get_data_doc,
             "get_data($self, /, frame, face, slice, mip)\n"
             "--\n"
             "\n"
             "Return pixel data for the specified level as bytes.");

PyDoc_STRVAR(VTF_set_flag_doc,
             "set_flag($self, /, flag, state)\n"
             "--\n"
             "\n"
             "Enable or disable a VTF flag.");

PyDoc_STRVAR(VTF_get_flag_doc,
             "get_flag($self, /, flag)\n"
             "--\n"
             "\n"
             "Return True if the given flag is set.");

PyDoc_STRVAR(VTF_generate_mipmaps_doc,
             "generate_mipmaps($self, /, mipmap_filter=MIPMAP_FILTER_BOX,\n"
             "                 sharpen_filter=SHARPEN_FILTER_NONE)\n"
             "--\n"
             "\n"
             "Generate mipmaps using the selected filters.");

PyDoc_STRVAR(VTF_set_reflectivity_doc,
             "set_reflectivity($self, /, x, y, z)\n"
             "--\n"
             "\n"
             "Set the reflectivity vector (RGB).");

PyDoc_STRVAR(VTF_compute_reflectivity_doc,
             "compute_reflectivity($self, /)\n"
             "--\n"
             "\n"
             "Compute reflectivity from image data.");

PyDoc_STRVAR(VTF_width_prop_doc, "Image width (int).");
PyDoc_STRVAR(VTF_height_prop_doc, "Image height (int).");
PyDoc_STRVAR(VTF_format_prop_doc, "VTF format enum (int).");
PyDoc_STRVAR(VTF_frame_count_prop_doc, "Number of frames (int).");
PyDoc_STRVAR(VTF_face_count_prop_doc, "Number of faces (int).");
PyDoc_STRVAR(VTF_mipmap_count_prop_doc, "Number of mip levels (int).");
PyDoc_STRVAR(VTF_flags_prop_doc, "VTF flags bitmask (int). Read/write.");
PyDoc_STRVAR(VTF_bump_scale_prop_doc, "Bumpmap scale (float). Read/write.");


PyObject *VTF_width_get(PyObject *self, void *);

PyObject *VTF_height_get(PyObject *self, void *);

PyObject *VTF_format_get(PyObject *self, void *);

PyObject *VTF_frame_count_get(PyObject *self, void *);

PyObject *VTF_face_count_get(PyObject *self, void *);

PyObject *VTF_mipmap_count_get(PyObject *self, void *);

PyObject *VTF_flags_get(PyObject *self, void *);

PyObject *VTF_bump_scale_get(PyObject *self, void *);

int VTF_flags_set(PyObject *self, PyObject *value, void *);

int VTF_bump_scale_set(PyObject *self, PyObject *value, void *);

static PyGetSetDef vtf_class_getset[] = {
        {"width",        getter(VTF_width_get),        nullptr},
        {"height",       getter(VTF_height_get),       nullptr,                    VTF_height_prop_doc,       nullptr},
        {"format",       getter(VTF_format_get),       nullptr,                    VTF_format_prop_doc,       nullptr},
        {"frame_count",  getter(VTF_frame_count_get),  nullptr,                    VTF_frame_count_prop_doc,  nullptr},
        {"face_count",   getter(VTF_face_count_get),   nullptr,                    VTF_face_count_prop_doc,   nullptr},
        {"mipmap_count", getter(VTF_mipmap_count_get), nullptr,                    VTF_mipmap_count_prop_doc, nullptr},
        {"flags",        getter(VTF_flags_get),        setter(VTF_flags_set),      VTF_flags_prop_doc,        nullptr},
        {"bump_scale",   getter(VTF_bump_scale_get),   setter(VTF_bump_scale_set), VTF_bump_scale_prop_doc,   nullptr},
        /* {"reflectivity", (getter)VTF_reflectivity_get, (setter)VTF_reflectivity_set, "RGB reflectivity", nullptr}, */
        {nullptr,        nullptr,                      nullptr,                    nullptr,                   nullptr}
};

PyObject *VTF_load(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_load_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_save(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_to_bytes(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_create(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_set_data(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_get_data(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_set_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_get_flag(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_generate_mipmaps(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_set_reflectivity(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

PyObject *VTF_compute_reflectivity(VTFObject *self, PyObject *const *args, Py_ssize_t nargs);

static PyMethodDef vtf_class_methods[] = {
        {"load",                 CPF(VTF_load),                 METH_FASTCALL, VTF_load_doc},
        {"load_bytes",           CPF(VTF_load_bytes),           METH_FASTCALL, VTF_load_bytes_doc},
        {"save",                 CPF(VTF_save),                 METH_FASTCALL, VTF_save_doc},
        {"to_bytes",             CPF(VTF_to_bytes),             METH_FASTCALL, VTF_to_bytes_doc},
        {"create",               CPF(VTF_create),               METH_FASTCALL, VTF_create_doc},
        {"set_data",             CPF(VTF_set_data),             METH_FASTCALL, VTF_set_data_doc},
        {"get_data",             CPF(VTF_get_data),             METH_FASTCALL, VTF_get_data_doc},
        {"set_flag",             CPF(VTF_set_flag),             METH_FASTCALL, VTF_set_flag_doc},
        {"get_flag",             CPF(VTF_get_flag),             METH_FASTCALL, VTF_get_flag_doc},
        {"generate_mipmaps",     CPF(VTF_generate_mipmaps),     METH_FASTCALL, VTF_generate_mipmaps_doc},
        {"set_reflectivity",     CPF(VTF_set_reflectivity),     METH_FASTCALL, VTF_set_reflectivity_doc},
        {"compute_reflectivity", CPF(VTF_compute_reflectivity), METH_FASTCALL, VTF_compute_reflectivity_doc},
        {nullptr, nullptr, 0,                                                  nullptr}
};

PyObject *VTF_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);

void VTF_dealloc(VTFObject *self);

static PyType_Slot vtf_class_slots[] = {
        {Py_tp_new,     (void *) VTF_new},
        {Py_tp_dealloc, (void *) VTF_dealloc},
        {Py_tp_methods, (void *) vtf_class_methods},
        {Py_tp_getset,  (void *) vtf_class_getset},
        {0,             0}
};

static PyType_Spec vtf_class_spec = {
        "pylib.vtf.VTFFile",
        sizeof(VTFObject),
        0,
        Py_TPFLAGS_DEFAULT,
        vtf_class_slots
};


#endif //PYLIB_VTF_CLASS_H
