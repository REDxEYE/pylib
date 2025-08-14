#include "classes/vpk_glob_iterator.h"

#if defined(_MSC_VER)
#include <shlwapi.h>
#else

#include <fnmatch.h>

#endif


PyObject *GlobIter_iter(PyObject *self) {
    Py_INCREF(self);
    return self;
}

void GlobIter_dealloc(VPKGlobIter *it) {
    Py_XDECREF(it->owner);
    Py_XDECREF(it->pattern_obj);
    PyObject_Del(it);  // limited-API safe
}

PyObject *GlobIter_iternext(VPKGlobIter *it) {
    // scan until match or end
    const auto &vec = *(it->owner->m_entries);
    const Py_ssize_t n = (Py_ssize_t) vec.size();

    while (it->index < n) {
        const VPKEntry &entry = vec[(size_t) it->index++];
        const char *name_c = entry.name.c_str();
        bool match = false;
#if defined(_MSC_VER)
        match = PathMatchSpecA(name_c, it->pattern_c) != 0;
#else
        match = (fnmatch(it->pattern_c, name_c, 0) == 0);
#endif
        if (!match)
            continue;

        PyObject *entry_data = get_entry_data(it->owner, entry);
        if (!entry_data)
            return nullptr; // propagate error

        PyObject *name_py = PyUnicode_FromString(name_c);
        if (!name_py) {
            Py_DECREF(entry_data);
            return nullptr;
        }

        PyObject *tup = PyTuple_New(2);
        if (!tup) {
            Py_DECREF(name_py);
            Py_DECREF(entry_data);
            return nullptr;
        }
        PyTuple_SetItem(tup, 0, name_py);     // steals
        PyTuple_SetItem(tup, 1, entry_data);  // steals
        return tup; // one item per next()
    }

    // End of iteration: return nullptr with no exception
    return nullptr;
}