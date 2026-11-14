#ifndef PYLIB_VTF_CLASS_H
#define PYLIB_VTF_CLASS_H

#include <Python.h>
#include "VTFFile.h"
#include "Error.h"

void set_vtf_error(const VTFLib::Diagnostics::CError &error);

struct VTFObject {
    PyObject_HEAD
    VTFLib::CVTFFile *file;
};

extern PyType_Spec vtf_class_spec;

#endif //PYLIB_VTF_CLASS_H
