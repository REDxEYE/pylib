#ifndef PYLIB_VPK_GLOB_ITERATOR_H
#define PYLIB_VPK_GLOB_ITERATOR_H

#include <Python.h>
#include "vpk_class.h"

typedef struct {
    PyObject_HEAD
    VPKFile *owner;         // strong ref to keep entries alive
    Py_ssize_t index;       // current scan position
    PyObject *pattern_obj;  // keep unicode alive (backs UTF-8 ptr)
    const char *pattern_c;  // cached UTF-8 pointer (valid while pattern_obj lives)
} VPKGlobIter;

PyObject *GlobIter_iter(PyObject *self);

void GlobIter_dealloc(VPKGlobIter *it);

 PyObject *GlobIter_iternext(VPKGlobIter *it);

/* Build the iterator type via spec (limited API) */

static PyType_Slot GlobIter_slots[] = {
        {Py_tp_dealloc,  (void *) GlobIter_dealloc},
        {Py_tp_iter,     (void *) GlobIter_iter},
        {Py_tp_iternext, (void *) GlobIter_iternext},
        {0,              0}
};
static PyType_Spec GlobIter_spec = {
        "pylib.vpk._GlobIter",          // fully-qualified name (adjust to your package)
        sizeof(VPKGlobIter),
        0,
        Py_TPFLAGS_DEFAULT,
        GlobIter_slots
};
static PyObject *GlobIter_TypeObj = NULL;

static int EnsureGlobIterType(void) {
    if (GlobIter_TypeObj) return 0;
    GlobIter_TypeObj = PyType_FromSpec(&GlobIter_spec);
    return GlobIter_TypeObj ? 0 : -1;
}

#endif //PYLIB_VPK_GLOB_ITERATOR_H
