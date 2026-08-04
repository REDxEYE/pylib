#ifndef PYLIB_UTILS_H
#define PYLIB_UTILS_H

#include <span>
#include <limits>
#include <string>
#include "Python.h"

#define CPF(fn)  (PyCFunction)(fn)

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

PyObject* create_int_enum(const char* name, const std::span<std::pair<std::string, uint32_t>>& members);
PyObject *create_int_flags(const char *name, const std::span<std::pair<std::string, uint32_t>> &members);

//! Create a submodule of ``parent`` and register it as ``pylib.<name>``.
//!
//! Creates the module, attaches it to the parent under \p name, publishes it in
//! ``sys.modules`` so ``import pylib.<name>`` works, and sets ``__path__`` so it
//! behaves like a package. Returns a borrowed reference owned by \p parent, or
//! nullptr with an exception set.
PyObject *add_submodule(PyObject *parent, const char *name, PyModuleDef *def);

//! Instantiate \p spec and add it to \p module under \p name.
//!
//! On success the module owns the type. Returns 0 on success, -1 with an
//! exception set on failure.
int add_type(PyObject *module, const char *name, PyType_Spec *spec);

//! Build an IntEnum from \p members and add it to \p module under \p name.
//! Returns 0 on success, -1 with an exception set on failure.
int add_int_enum(PyObject *module, const char *name,
                 const std::span<std::pair<std::string, uint32_t>> &members);

//! Build an IntFlag from \p members and add it to \p module under \p name.
//! Returns 0 on success, -1 with an exception set on failure.
int add_int_flags(PyObject *module, const char *name,
                  const std::span<std::pair<std::string, uint32_t>> &members);


class PyROBytesView {
public:
    PyROBytesView() = default;

    explicit PyROBytesView(PyObject* obj);

    PyROBytesView(const PyROBytesView&) = delete;
    PyROBytesView& operator=(const PyROBytesView&) = delete;

    inline PyROBytesView(PyROBytesView&& other) noexcept;

    PyROBytesView& operator=(PyROBytesView&& other) noexcept;

    ~PyROBytesView();

    inline explicit operator bool() const { return owner_ != nullptr; }
    inline const char* data() const { return data_; }
    inline size_t size() const { return size_; }
    inline PyObject* owner() const { return owner_; }

    void reset();

private:
    void move_from(PyROBytesView& other);

    PyObject* owner_ = nullptr;
    const char* data_ = nullptr;
    size_t size_ = 0;
};

#endif //PYLIB_UTILS_H
