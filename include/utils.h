#ifndef PYLIB_UTILS_H
#define PYLIB_UTILS_H

#include <span>
#include <limits>
#include "Python.h"

#define CPF(fn)  _PyCFunction_CAST(fn)

PyObject *type_error(const char *arg, const char *expected, PyObject *got);


inline int mul_checked_psszt(Py_ssize_t a, Py_ssize_t b, Py_ssize_t* out) {
    if (a < 0 || b < 0) return 0;
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > PY_SSIZE_T_MAX / b) return 0;
    *out = a * b;
    return 1;
}

inline int mul3_checked_psszt(Py_ssize_t a, Py_ssize_t b, Py_ssize_t c, Py_ssize_t* out) {
    Py_ssize_t t;
    if (!mul_checked_psszt(a, b, &t)) return 0;
    return mul_checked_psszt(t, c, out);
}

static std::span<uint8_t>
slice(const std::span<uint8_t> &data, uint32_t start, size_t len = -1) {
    return data.subspan(start, len);
}

#endif //PYLIB_UTILS_H
