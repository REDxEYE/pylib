#ifndef PYLIB_VPK_CLASS_H
#define PYLIB_VPK_CLASS_H

#include <Python.h>
#include <string>
#include <fstream>
#include <vector>
#include "utils.h"

struct VPKEntry{
    std::string name;
    uint32_t offset;
    uint32_t size;
    int32_t archive_id;
    std::vector<uint8_t> preload{};
};

struct VPKFile {
    PyObject_HEAD
    std::string m_path;
    std::ifstream* m_stream;
    std::vector<VPKEntry> *m_entries;
    size_t m_embedded_data_start;
    std::string m_chunked_base;
};
PyObject* VPKFile_find_file(VPKFile *self, PyObject *const *args, Py_ssize_t nargs);
PyObject* VPKFile_glob(VPKFile *self, PyObject *const *args, Py_ssize_t nargs);

PyDoc_STRVAR(VPKFile_find_file_doc,
             "find_file($self, /, name)\n"
             "--\n"
             "\n"
             "Find a file in the VPK by its name. Returns a tuple (offset, size, archive_id) if found, or None if not found.");

PyDoc_STRVAR(VPKFile_glob_doc,
             "glob($self, /, pattern)\n"
             "--\n"
             "\n"
             "Find files in the VPK matching a glob pattern.\n");

static PyMethodDef VPKFile_class_methods[] = {
        {"find_file", CPF(VPKFile_find_file), METH_FASTCALL, VPKFile_find_file_doc},
        {"glob", CPF(VPKFile_glob), METH_FASTCALL, VPKFile_glob_doc},
        {nullptr, nullptr, 0, nullptr}
};

PyObject *VPKFile_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);

int VPKFile_init(VPKFile *self, PyObject *args, PyObject *kwds);

void VPKFile_dealloc(VPKFile *self);

static PyType_Slot VPKFile_class_slots[] = {
        {Py_tp_new,     (void *) VPKFile_new},
        {Py_tp_init,    (void *) VPKFile_init},
        {Py_tp_dealloc, (void *) VPKFile_dealloc},
        {Py_tp_methods, (void *) VPKFile_class_methods},
        {0,             nullptr}
};

static PyType_Spec VPKFile_class_spec = {
        "pylib.VPKFile",
        sizeof(VPKFile),
        0,
        Py_TPFLAGS_DEFAULT,
        VPKFile_class_slots
};

PyObject *get_entry_data(VPKFile *self, const VPKEntry &entry);

#endif //PYLIB_VPK_CLASS_H
