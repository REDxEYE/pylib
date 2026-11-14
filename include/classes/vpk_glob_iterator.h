#ifndef PYLIB_VPK_GLOB_ITERATOR_H
#define PYLIB_VPK_GLOB_ITERATOR_H

#include <Python.h>
#include <string_view>
#include "vpk_class.h"

struct VPKGlobIter {
    PyObject_HEAD
    VPKFile *owner;          // strong ref to keep entries alive
    Py_ssize_t index;        // current scan position
    PyObject *pattern_obj;   // lowercased pattern as bytes
    std::ifstream *chunk;    // cached open chunk archive (may be null)
    int32_t chunk_id;        // archive index of `chunk`
};

extern PyType_Spec GlobIter_spec;
extern PyTypeObject *GlobIterType; // created once by VPK_InitTypes

// Python fnmatch.fnmatchcase semantics (byte-wise).
bool vpk_fnmatch(std::string_view name, std::string_view pattern);

#endif //PYLIB_VPK_GLOB_ITERATOR_H
