#include "classes/vpk_glob_iterator.h"

PyTypeObject *GlobIterType = nullptr;

// Matches one `[...]` class starting at pat[pi] == '['. Returns false if the
// class is unterminated (the '[' is then a literal). On success `end` is the
// index past the closing ']' and `matched` tells whether `c` is in the class.
static bool match_class(std::string_view pat, size_t pi, unsigned char c, bool &matched, size_t &end) {
    size_t j = pi + 1;
    if (j < pat.size() && pat[j] == '!')
        ++j;
    if (j < pat.size() && pat[j] == ']')
        ++j; // leading ']' is literal
    while (j < pat.size() && pat[j] != ']')
        ++j;
    if (j >= pat.size())
        return false;
    size_t k = pi + 1;
    bool negate = false;
    if (pat[k] == '!') {
        negate = true;
        ++k;
    }
    bool hit = false;
    while (k < j) {
        const auto lo = (unsigned char) pat[k];
        if (k + 2 < j && pat[k + 1] == '-') {
            const auto hi = (unsigned char) pat[k + 2];
            if (lo <= c && c <= hi)
                hit = true;
            k += 3;
        } else {
            if (lo == c)
                hit = true;
            ++k;
        }
    }
    matched = hit != negate;
    end = j + 1;
    return true;
}

// Iterative star-backtracking matcher (linear in the common case, O(n*m)
// worst case; never exponential). `*` also matches '/' as in Python fnmatch.
bool vpk_fnmatch(std::string_view name, std::string_view pat) {
    size_t n = 0, p = 0;
    size_t star = std::string_view::npos, mark = 0;
    while (n < name.size()) {
        bool advanced = false;
        if (p < pat.size()) {
            const char pc = pat[p];
            if (pc == '*') {
                star = ++p;
                mark = n;
                continue;
            }
            if (pc == '?') {
                ++p;
                ++n;
                continue;
            }
            if (pc == '[') {
                bool m = false;
                size_t end = 0;
                if (match_class(pat, p, (unsigned char) name[n], m, end)) {
                    if (m) {
                        p = end;
                        ++n;
                        advanced = true;
                    }
                } else if (name[n] == '[') {
                    ++p;
                    ++n;
                    advanced = true;
                }
            } else if (pc == name[n]) {
                ++p;
                ++n;
                advanced = true;
            }
        }
        if (advanced)
            continue;
        if (star == std::string_view::npos)
            return false;
        p = star;
        n = ++mark;
    }
    while (p < pat.size() && pat[p] == '*')
        ++p;
    return p == pat.size();
}

static PyObject *GlobIter_iter(PyObject *self) {
    Py_INCREF(self);
    return self;
}

static void GlobIter_dealloc(VPKGlobIter *it) {
    delete it->chunk;
    Py_XDECREF((PyObject *) it->owner);
    Py_XDECREF(it->pattern_obj);
    PyTypeObject *tp = Py_TYPE((PyObject *) it);
    ((freefunc) PyType_GetSlot(tp, Py_tp_free))((PyObject *) it);
    Py_DECREF(tp);
}

static PyObject *GlobIter_iternext(VPKGlobIter *it) {
    if (!it->owner || !it->pattern_obj)
        return nullptr; // exhausted / never initialised: StopIteration
    char *pat_c = nullptr;
    Py_ssize_t pat_len = 0;
    if (PyBytes_AsStringAndSize(it->pattern_obj, &pat_c, &pat_len) < 0)
        return nullptr;
    const std::string_view pattern(pat_c, (size_t) pat_len);
    const auto &vec = *(it->owner->m_entries);
    const Py_ssize_t n = (Py_ssize_t) vec.size();

    while (it->index < n) {
        const VPKEntry &entry = vec[(size_t) it->index++];
        if (!vpk_fnmatch(entry.name, pattern))
            continue;

        PyObject *entry_data = get_entry_data(it->owner, entry, &it->chunk, &it->chunk_id);
        if (!entry_data)
            return nullptr; // propagate error

        PyObject *name_py = PyUnicode_FromStringAndSize(entry.name.data(), (Py_ssize_t) entry.name.size());
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
        // Both steal on success; on failure they drop the reference themselves, so
        // the tuple is all that is left to release.
        if (PyTuple_SetItem(tup, 0, name_py) < 0 || PyTuple_SetItem(tup, 1, entry_data) < 0) {
            Py_DECREF(tup);
            return nullptr;
        }
        return tup;
    }

    // Exhausted: drop the cached chunk stream early.
    delete it->chunk;
    it->chunk = nullptr;
    it->chunk_id = -1;
    return nullptr;
}

static PyType_Slot GlobIter_slots[] = {
        {Py_tp_dealloc,  (void *) GlobIter_dealloc},
        {Py_tp_iter,     (void *) GlobIter_iter},
        {Py_tp_iternext, (void *) GlobIter_iternext},
        {0,              nullptr}
};

PyType_Spec GlobIter_spec = {
        "pylib._VPKGlobIterator",
        sizeof(VPKGlobIter),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        GlobIter_slots
};
