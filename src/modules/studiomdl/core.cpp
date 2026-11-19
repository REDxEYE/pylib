//
// pylib.studiomdl core: errors, enums, converters, arrays, argument parsing, views and options.
//

#include "modules/studiomdl/bind.h"

#include <cstdarg>
#include <cstdio>
#include <memory>
#include <unordered_map>

namespace pysmdl {

ErrorTypes errors;

void raise(PyObject *type, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    PyErr_SetString(type, buffer);
    throw PyError{};
}

// ---------------------------------------------------------------------------------------------------------------------
// Errors

static PyObject *new_error(PyObject *module, const char *name, const char *doc, std::initializer_list<PyObject *> bases) {
    PyObject *tuple = PyTuple_New((Py_ssize_t) bases.size());
    if (!tuple)
        return nullptr;
    Py_ssize_t i = 0;
    for (PyObject *base: bases) {
        Py_INCREF(base);
        PyTuple_SetItem(tuple, i++, base);
    }
    std::string qualified = std::string("pylib.studiomdl.") + name;
    PyObject *type = PyErr_NewExceptionWithDoc(qualified.c_str(), doc, tuple, nullptr);
    Py_DECREF(tuple);
    if (!type)
        return nullptr;
    if (PyModule_AddObjectRef(module, name, type) < 0) {
        Py_DECREF(type);
        return nullptr;
    }
    return type;  // the process keeps this reference
}

int init_errors(PyObject *module) {
    if (errors.base)
        return 0;
    ErrorTypes e;
    e.base = new_error(module, "StudioMdlError",
                       "Base of every studiomdl error.\n\n"
                       "Attributes: status (int, the smdl_status), object (str, the object being compiled when a "
                       "compile failed, else empty), messages (list of Message, compile messages).",
                       {PyExc_Exception});
    if (!e.base)
        return -1;
    struct Def {
        PyObject **slot;
        const char *name;
        const char *doc;
        std::initializer_list<PyObject *> bases;
    };
    const Def defs[] = {
        {&e.invalid_argument, "InvalidArgumentError", "Bad value, enum value or array length.", {e.base, PyExc_ValueError}},
        {&e.out_of_range, "OutOfRangeError", "An id or index not valid for this model or geometry.", {e.base, PyExc_IndexError}},
        {&e.duplicate_name, "DuplicateNameError", "A name already used by another object of the same kind.", {e.base, PyExc_ValueError}},
        {&e.not_found, "NotFoundError", "A name lookup found nothing.", {e.base, PyExc_KeyError}},
        {&e.limit, "LimitError", "Over a studiomdl / MDL format limit.", {e.base, PyExc_ValueError}},
        {&e.syntax, "ExpressionSyntaxError", "A flex rule expression does not parse.", {e.base, PyExc_ValueError}},
        {&e.compile, "CompileError", "The compile failed; object and messages describe it.", {e.base}},
        {&e.io, "StudioMdlIOError", "A file could not be written.", {e.base, PyExc_OSError}},
        {&e.internal, "InternalError", "Unexpected failure inside studiomdl.", {e.base, PyExc_RuntimeError}},
    };
    for (const Def &def: defs) {
        *def.slot = new_error(module, def.name, def.doc, def.bases);
        if (!*def.slot)
            return -1;
    }
    e.qc = new_error(module, "QcError", "A QC compile failed. Extra attributes: file (str) and line (int) of the error, "
                                        "empty / 0 when unknown.", {e.compile});
    if (!e.qc)
        return -1;
    errors = e;
    return 0;
}

static PyObject *error_type(smdl_status status) {
    switch (status) {
        case SMDL_ERROR_INVALID_ARGUMENT: return errors.invalid_argument;
        case SMDL_ERROR_OUT_OF_RANGE: return errors.out_of_range;
        case SMDL_ERROR_DUPLICATE_NAME: return errors.duplicate_name;
        case SMDL_ERROR_NOT_FOUND: return errors.not_found;
        case SMDL_ERROR_LIMIT: return errors.limit;
        case SMDL_ERROR_SYNTAX: return errors.syntax;
        case SMDL_ERROR_COMPILE: return errors.compile;
        case SMDL_ERROR_IO: return errors.io;
        default: return errors.internal;
    }
}

static PyObject *utf8(std::string_view text) {
    return PyUnicode_DecodeUTF8(text.data(), (Py_ssize_t) text.size(), "replace");
}

static bool set_attr(PyObject *object, const char *name, PyObject *value) {
    if (!value)
        return false;
    const bool ok = PyObject_SetAttrString(object, name, value) == 0;
    Py_DECREF(value);
    return ok;
}

void set_smdl_error(smdl_status status, std::string_view message, std::string_view object, PyObject *messages,
                    std::string_view file, int line, bool qc) {
    PyObject *type = qc ? errors.qc : error_type(status);
    Ref text(utf8(message));
    if (!text)
        return;
    Ref exc(PyObject_CallFunctionObjArgs(type, text.get(), nullptr));
    if (!exc)
        return;
    if (!set_attr(exc.get(), "status", PyLong_FromLong(status)) ||
        !set_attr(exc.get(), "object", utf8(object)))
        return;
    if (messages) {
        Py_INCREF(messages);
        if (!set_attr(exc.get(), "messages", messages))
            return;
    } else if (!set_attr(exc.get(), "messages", PyList_New(0))) {
        return;
    }
    if (type == errors.qc &&
        (!set_attr(exc.get(), "file", utf8(file)) || !set_attr(exc.get(), "line", PyLong_FromLong(line))))
        return;
    PyErr_SetObject(type, exc.get());
}

void translate_current_exception(PyObject *messages) {
    try {
        throw;
    } catch (const PyError &) {
    } catch (const smdl::QcError &e) {
        set_smdl_error(e.status(), e.what(), e.object(), messages, e.file(), e.line(), true);
    } catch (const smdl::Error &e) {
        set_smdl_error(e.status(), e.what(), e.object(), messages);
    } catch (const std::bad_alloc &) {
        PyErr_NoMemory();
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "unknown C++ exception in studiomdl");
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Enums

int add_enum(PyObject *module, PyObject **slot, const char *name, bool flags,
             std::initializer_list<std::pair<const char *, uint32_t>> members) {
    Ref enum_module(PyImport_ImportModule("enum"));
    if (!enum_module)
        return -1;
    Ref base(PyObject_GetAttrString(enum_module.get(), flags ? "IntFlag" : "IntEnum"));
    if (!base)
        return -1;
    Ref items(PyList_New(0));
    if (!items)
        return -1;
    for (const auto &[member, value]: members) {
        Ref pair(Py_BuildValue("(sk)", member, (unsigned long) value));
        if (!pair || PyList_Append(items.get(), pair.get()) < 0)
            return -1;
    }
    Ref kwargs(Py_BuildValue("{ss}", "module", "pylib.studiomdl"));
    Ref args(Py_BuildValue("(sO)", name, items.get()));
    if (!kwargs || !args)
        return -1;
    PyObject *type = PyObject_Call(base.get(), args.get(), kwargs.get());
    if (!type)
        return -1;
    if (PyModule_AddObjectRef(module, name, type) < 0) {
        Py_DECREF(type);
        return -1;
    }
    Py_XDECREF(*slot);
    *slot = type;
    return 0;
}

PyObject *enum_value(PyObject *type, uint32_t value) {
    PyObject *number = PyLong_FromUnsignedLong(value);
    if (!number || !type)
        return number;
    PyObject *member = PyObject_CallFunctionObjArgs(type, number, nullptr);
    if (!member) {
        if (!PyErr_ExceptionMatches(PyExc_ValueError)) {
            Py_DECREF(number);
            return nullptr;
        }
        PyErr_Clear();
        return number;
    }
    Py_DECREF(number);
    return member;
}

// ---------------------------------------------------------------------------------------------------------------------
// Converters

static const char *name_of_type(PyTypeObject *type, std::string &buffer) {
    PyObject *name = PyType_GetName(type);
    if (!name) {
        PyErr_Clear();
        return "?";
    }
    const char *text = PyUnicode_AsUTF8AndSize(name, nullptr);
    buffer = text ? text : "?";
    if (!text)
        PyErr_Clear();
    Py_DECREF(name);
    return buffer.c_str();
}

const char *type_name(PyObject *object) {
    thread_local std::string buffer;
    return name_of_type(Py_TYPE(object), buffer);
}

const char *type_name_of(PyTypeObject *type) {
    thread_local std::string buffer;
    return name_of_type(type, buffer);
}

[[noreturn]] static void wrong_type(PyObject *object, const char *name, const char *expected) {
    if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_TypeError))
        throw PyError{};
    PyErr_Clear();
    raise(PyExc_TypeError, "%s must be %s, not %.100s", name, expected, type_name(object));
}

bool Conv<bool>::from(PyObject *object, const char *name) {
    const int truth = PyObject_IsTrue(object);
    if (truth < 0)
        wrong_type(object, name, "a bool");
    return truth != 0;
}

static long long to_integer(PyObject *object, const char *name) {
    if (PyFloat_Check(object) || PyUnicode_Check(object) || PyBytes_Check(object))
        wrong_type(object, name, "an int");
    Ref index(PyNumber_Index(object));
    if (!index)
        wrong_type(object, name, "an int");
    int overflow = 0;
    const long long value = PyLong_AsLongLongAndOverflow(index.get(), &overflow);
    if (overflow)
        raise(PyExc_OverflowError, "%s is out of range", name);
    if (value == -1 && PyErr_Occurred())
        throw PyError{};
    return value;
}

int Conv<int>::from(PyObject *object, const char *name) {
    const long long value = to_integer(object, name);
    if (value < INT32_MIN || value > INT32_MAX)
        raise(PyExc_OverflowError, "%s is out of range", name);
    return (int) value;
}

uint32_t Conv<uint32_t>::from(PyObject *object, const char *name) {
    const long long value = to_integer(object, name);
    if (value < 0 || value > (long long) UINT32_MAX)
        raise(PyExc_OverflowError, "%s must be in 0..%u", name, UINT32_MAX);
    return (uint32_t) value;
}

float Conv<float>::from(PyObject *object, const char *name) {
    if (PyUnicode_Check(object) || PyBytes_Check(object))
        wrong_type(object, name, "a number");
    const double value = PyFloat_AsDouble(object);
    if (value == -1.0 && PyErr_Occurred())
        wrong_type(object, name, "a number");
    return (float) value;
}

std::string Conv<std::string>::from(PyObject *object, const char *name) {
    if (!PyUnicode_Check(object))
        wrong_type(object, name, "a str");
    Py_ssize_t size = 0;
    const char *text = PyUnicode_AsUTF8AndSize(object, &size);
    if (!text)
        throw PyError{};
    return {text, (size_t) size};
}

std::vector<float> float_sequence(PyObject *object, size_t count, const char *name) {
    if (PyUnicode_Check(object) || PyBytes_Check(object) || !PySequence_Check(object))
        raise(PyExc_TypeError, "%s must be a sequence of %zu numbers, not %.100s", name, count, type_name(object));
    const Py_ssize_t size = PySequence_Size(object);
    if (size < 0)
        throw PyError{};
    if ((size_t) size != count)
        raise(PyExc_ValueError, "%s must have %zu values, not %zd", name, count, size);
    std::vector<float> values(count);
    for (size_t i = 0; i < count; ++i) {
        Ref item = Ref::check(PySequence_GetItem(object, (Py_ssize_t) i));
        const double value = PyFloat_AsDouble(item.get());
        if (value == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            raise(PyExc_TypeError, "%s must contain numbers, not %.100s", name, type_name(item.get()));
        }
        values[i] = (float) value;
    }
    return values;
}

std::string fs_path(PyObject *object, const char *name, bool allow_none) {
    if (object == Py_None) {
        if (allow_none)
            return {};
        raise(PyExc_TypeError, "%s must be a path, not None", name);
    }
    Ref path(PyOS_FSPath(object));
    if (!path)
        wrong_type(object, name, "a str or os.PathLike");
    if (PyBytes_Check(path.get())) {
        char *data = nullptr;
        Py_ssize_t size = 0;
        if (PyBytes_AsStringAndSize(path.get(), &data, &size) < 0)
            throw PyError{};
        return {data, (size_t) size};
    }
    return Conv<std::string>::from(path.get(), name);
}

std::vector<std::string> string_list(PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        raise(PyExc_TypeError, "%s must be a sequence of str, not a single str", name);
    Ref iterator(PyObject_GetIter(object));
    if (!iterator)
        wrong_type(object, name, "an iterable of str");
    std::vector<std::string> result;
    while (PyObject *item = PyIter_Next(iterator.get())) {
        Ref owned(item);
        result.push_back(Conv<std::string>::from(item, name));
    }
    if (PyErr_Occurred())
        throw PyError{};
    return result;
}

// ---------------------------------------------------------------------------------------------------------------------
// Arrays

template<class T>
Array<T>::Array(PyObject *object, size_t columns, const char *name) : m_Columns(columns) {
    if (!from_buffer(object, name))
        from_iterable(object, name);
}

template<class T>
bool Array<T>::from_buffer(PyObject *object, const char *name) {
    if (!PyObject_CheckBuffer(object))
        return false;
    if (PyObject_GetBuffer(object, &m_View, PyBUF_RECORDS_RO) < 0) {
        PyErr_Clear();
        return false;
    }
    m_HasView = true;
    const char *format = m_View.format ? m_View.format : "B";
    if (*format == '@' || *format == '=' || *format == '<')
        ++format;
    bool type_ok;
    if constexpr (std::is_same_v<T, float>)
        type_ok = std::strcmp(format, "f") == 0;
    else
        type_ok = m_View.itemsize == 4 && format[1] == '\0' && std::strchr("IiLl", format[0]) != nullptr;
    if (!type_ok || m_View.itemsize != (Py_ssize_t) sizeof(T)) {
        if (m_View.ndim == 0 || (format[0] != '\0' && format[1] == '\0' && std::strchr("bBhHiIlLqQnNfde", format[0]))) {
            // A numeric buffer of another type (numpy's default int64/float64, ...): copy through the iterable path,
            // which converts and range checks every value
            PyBuffer_Release(&m_View);
            m_HasView = false;
            return false;
        }
        raise(PyExc_TypeError, "%s must hold %s values, not buffer format '%s'", name,
              std::is_same_v<T, float> ? "float32" : "32 bit integer", m_View.format ? m_View.format : "B");
    }
    const Py_ssize_t *shape = m_View.shape;
    const Py_ssize_t *strides = m_View.strides;
    if (m_View.ndim == 2 && shape[1] == (Py_ssize_t) m_Columns && (shape[0] <= 1 || strides[0] > 0) &&
        (m_Columns == 1 || strides[1] == (Py_ssize_t) sizeof(T))) {
        m_Rows = (size_t) shape[0];
        m_Stride = shape[0] > 1 ? (size_t) strides[0] : m_Columns * sizeof(T);
    } else if (m_View.ndim == 1 && m_Columns == 1 && (shape[0] <= 1 || strides[0] > 0)) {
        m_Rows = (size_t) shape[0];
        m_Stride = shape[0] > 1 ? (size_t) strides[0] : sizeof(T);
    } else if (m_View.ndim == 1 && strides[0] == (Py_ssize_t) sizeof(T) && shape[0] % (Py_ssize_t) m_Columns == 0) {
        m_Rows = (size_t) shape[0] / m_Columns;
        m_Stride = m_Columns * sizeof(T);
    } else {
        raise(PyExc_ValueError, "%s must have shape (n, %zu) with contiguous rows", name, m_Columns);
    }
    m_Data = reinterpret_cast<const T *>(m_View.buf);
    return true;
}

template<class T>
void Array<T>::from_iterable(PyObject *object, const char *name) {
    if (PyUnicode_Check(object) || PyBytes_Check(object))
        raise(PyExc_TypeError, "%s must be an array or a sequence of rows, not %.100s", name, type_name(object));
    Ref iterator(PyObject_GetIter(object));
    if (!iterator) {
        PyErr_Clear();
        raise(PyExc_TypeError, "%s must be an array or a sequence of rows, not %.100s", name, type_name(object));
    }
    auto scalar = [&](PyObject *item) -> T {
        if constexpr (std::is_same_v<T, float>)
            return Conv<float>::from(item, name);
        else
            return Conv<uint32_t>::from(item, name);
    };
    // Rows of `columns` numbers, or a flat sequence of numbers (rows * columns of them), decided by the first item.
    enum { Unknown, Flat, Rows } layout = Unknown;
    while (PyObject *row = PyIter_Next(iterator.get())) {
        Ref owned(row);
        const bool is_scalar = PyNumber_Check(row) && !PySequence_Check(row);
        if (layout == Unknown)
            layout = is_scalar ? Flat : Rows;
        if (layout == Flat) {
            if (!is_scalar)
                raise(PyExc_TypeError, "%s must be a flat sequence of numbers or a sequence of rows, not a mix",
                      name);
            m_Copy.push_back(scalar(row));
            continue;
        }
        if (!PySequence_Check(row) || PyUnicode_Check(row))
            raise(PyExc_TypeError, "%s rows must be sequences of %zu numbers, not %.100s", name, m_Columns,
                  type_name(row));
        const Py_ssize_t size = PySequence_Size(row);
        if (size < 0)
            throw PyError{};
        if ((size_t) size != m_Columns)
            raise(PyExc_ValueError, "%s rows must have %zu values, not %zd", name, m_Columns, size);
        for (Py_ssize_t i = 0; i < size; ++i) {
            Ref item = Ref::check(PySequence_GetItem(row, i));
            m_Copy.push_back(scalar(item.get()));
        }
    }
    if (PyErr_Occurred())
        throw PyError{};
    if (m_Copy.size() % m_Columns != 0)
        raise(PyExc_ValueError, "%s has %zu values, not a multiple of %zu", name, m_Copy.size(), m_Columns);
    m_Rows = m_Copy.size() / m_Columns;
    m_Stride = m_Columns * sizeof(T);
    m_Data = m_Copy.data();
}

template class Array<float>;
template class Array<uint32_t>;

// ---------------------------------------------------------------------------------------------------------------------
// Arguments

Args::Args(const char *function, PyObject *args, PyObject *kwargs, std::initializer_list<const char *> names,
           size_t required, size_t positional)
    : m_Values(names.size(), nullptr), m_Names(names) {
    const Py_ssize_t count = args ? PyTuple_Size(args) : 0;
    if ((size_t) count > positional)
        raise(PyExc_TypeError, "%s() takes at most %zu positional arguments (%zd given)", function, positional, count);
    for (Py_ssize_t i = 0; i < count; ++i)
        m_Values[(size_t) i] = PyTuple_GetItem(args, i);
    if (kwargs) {
        PyObject *key;
        PyObject *value;
        Py_ssize_t position = 0;
        while (PyDict_Next(kwargs, &position, &key, &value)) {
            const char *keyword = PyUnicode_AsUTF8AndSize(key, nullptr);
            if (!keyword)
                throw PyError{};
            size_t index = 0;
            while (index < m_Names.size() && std::strcmp(m_Names[index], keyword) != 0)
                ++index;
            if (index == m_Names.size())
                raise(PyExc_TypeError, "%s() got an unexpected keyword argument '%s'", function, keyword);
            if (m_Values[index])
                raise(PyExc_TypeError, "%s() got multiple values for argument '%s'", function, keyword);
            m_Values[index] = value;
        }
    }
    for (size_t i = 0; i < required; ++i)
        if (!m_Values[i])
            raise(PyExc_TypeError, "%s() missing required argument '%s'", function, m_Names[i]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Views

PyObject *view_model_getter(PyObject *self, void *) {
    // Every view starts with PyObject_HEAD followed by the owner.
    auto *owner = reinterpret_cast<ViewObject<BoneRef> *>(self)->owner;
    Py_INCREF(reinterpret_cast<PyObject *>(owner));
    return reinterpret_cast<PyObject *>(owner);
}

static std::vector<PyMethodDef> terminated(PyMethodDef *defs) {
    std::vector<PyMethodDef> result;
    for (PyMethodDef *def = defs; def && def->ml_name; ++def)
        result.push_back(*def);
    result.push_back({nullptr, nullptr, 0, nullptr});
    return result;
}

int add_view_type_impl(PyObject *module, const char *qualname, const char *doc, size_t basicsize, destructor dealloc,
                       richcmpfunc compare, hashfunc hash, reprfunc repr, PyMethodDef *methods, PyGetSetDef *getsets,
                       PyTypeObject **out) {
    // Specs and slot arrays must outlive the types: keep them for the life of the process.
    static std::vector<std::unique_ptr<std::vector<PyType_Slot>>> slot_store;
    static std::vector<std::unique_ptr<PyType_Spec>> spec_store;
    static std::vector<std::unique_ptr<std::vector<PyGetSetDef>>> getset_store;
    static std::vector<std::unique_ptr<std::vector<PyMethodDef>>> method_store;
    static std::vector<std::unique_ptr<std::string>> name_store;

    auto all_getsets = std::make_unique<std::vector<PyGetSetDef>>();
    for (PyGetSetDef *def = getsets; def && def->name; ++def)
        all_getsets->push_back(*def);
    all_getsets->push_back({"model", view_model_getter, nullptr, "Model\n--\n\nThe Model this object belongs to.",
                            nullptr});
    all_getsets->push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    auto all_methods = std::make_unique<std::vector<PyMethodDef>>(terminated(methods));
    if (typed_members(qualname, all_methods->data(), all_getsets->data()) < 0)
        return -1;

    auto slots = std::make_unique<std::vector<PyType_Slot>>();
    slots->push_back({Py_tp_dealloc, reinterpret_cast<void *>(dealloc)});
    slots->push_back({Py_tp_richcompare, reinterpret_cast<void *>(compare)});
    slots->push_back({Py_tp_hash, reinterpret_cast<void *>(hash)});
    slots->push_back({Py_tp_doc, const_cast<char *>(doc)});
    slots->push_back({Py_tp_getset, all_getsets->data()});
    slots->push_back({Py_tp_methods, all_methods->data()});
    if (repr)
        slots->push_back({Py_tp_repr, reinterpret_cast<void *>(repr)});
    slots->push_back({0, nullptr});

    auto name = std::make_unique<std::string>(std::string("pylib.studiomdl.") + qualname);
    // Views are created by the library only (instantiating one from Python raises TypeError).
    auto spec = std::make_unique<PyType_Spec>(PyType_Spec{name->c_str(), (int) basicsize, 0,
                                                          Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
                                                          slots->data()});
    PyObject *type = PyType_FromSpec(spec.get());
    if (!type)
        return -1;
    if (PyModule_AddObjectRef(module, qualname, type) < 0) {
        Py_DECREF(type);
        return -1;
    }
    Py_XDECREF(reinterpret_cast<PyObject *>(*out));
    *out = reinterpret_cast<PyTypeObject *>(type);
    slot_store.push_back(std::move(slots));
    spec_store.push_back(std::move(spec));
    getset_store.push_back(std::move(all_getsets));
    method_store.push_back(std::move(all_methods));
    name_store.push_back(std::move(name));
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Options

namespace {
struct OptionClosure {
    const OptionDef *def;
    OptionKey key;
    OptionOwner owner;
};
}

static PyObject *option_get(PyObject *self, void *closure) {
    auto *c = static_cast<OptionClosure *>(closure);
    return guarded([&]() -> PyObject * {
        Ref key = Ref::check(c->key(self, c->def->name));
        PyObject *value = PyDict_GetItemWithError(c->owner(self)->options, key.get());
        if (!value) {
            if (PyErr_Occurred())
                throw PyError{};
            Py_RETURN_NONE;
        }
        Py_INCREF(value);
        return value;
    });
}

static int option_set(PyObject *self, PyObject *value, void *closure) {
    auto *c = static_cast<OptionClosure *>(closure);
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute '%s'", c->def->name);
        if (value == Py_None)
            raise(PyExc_TypeError, "%s can't be reset to None (studiomdl has no way to restore its default)",
                  c->def->name);
        Ref canonical = Ref::check(c->def->set(self, value, c->def->name));
        Ref key = Ref::check(c->key(self, c->def->name));
        if (PyDict_SetItem(c->owner(self)->options, key.get(), canonical.get()) < 0)
            throw PyError{};
        return 0;
    });
}

PyGetSetDef *option_getsets(std::initializer_list<std::span<const OptionDef>> options,
                            std::initializer_list<std::span<const PyGetSetDef>> extra, OptionKey key,
                            OptionOwner owner) {
    static std::vector<std::unique_ptr<std::vector<PyGetSetDef>>> store;
    static std::vector<std::unique_ptr<OptionClosure>> closures;
    auto defs = std::make_unique<std::vector<PyGetSetDef>>();
    for (const auto &list: options) {
        for (const OptionDef &def: list) {
            closures.push_back(std::make_unique<OptionClosure>(OptionClosure{&def, key, owner}));
            const char *doc = def.doc;
            if (def.annotation) {
                static std::vector<std::unique_ptr<std::string>> docs;
                docs.push_back(std::make_unique<std::string>(def.annotation() + " | None\n--\n\n" + def.doc));
                doc = docs.back()->c_str();
            }
            defs->push_back({def.name, option_get, option_set, doc, closures.back().get()});
        }
    }
    for (const auto &list: extra)
        for (const PyGetSetDef &def: list)
            defs->push_back(def);
    defs->push_back({nullptr, nullptr, nullptr, nullptr, nullptr});
    store.push_back(std::move(defs));
    return store.back()->data();
}

PyObject *model_option_key(PyObject *, const char *name) {
    return PyUnicode_FromString(name);
}

ModelObject *model_option_owner(PyObject *self) {
    return reinterpret_cast<ModelObject *>(self);
}

// ---------------------------------------------------------------------------------------------------------------------
// Typing

static PyObject *typing_entries = nullptr;   // module.__typing__
static PyObject *readonly_entries = nullptr; // module.__typing_readonly__: attributes without a setter

int init_typing(PyObject *module) {
    Py_XDECREF(typing_entries);
    Py_XDECREF(readonly_entries);
    typing_entries = PyDict_New();
    readonly_entries = PySet_New(nullptr);
    if (!typing_entries || !readonly_entries)
        return -1;
    if (PyModule_AddObjectRef(module, "__typing_readonly__", readonly_entries) < 0)
        return -1;
    return PyModule_AddObjectRef(module, "__typing__", typing_entries);
}

void add_typing(const std::string &qualname, std::string_view annotation) {
    if (!typing_entries)
        return;
    Ref value = Ref::check(PyUnicode_FromStringAndSize(annotation.data(), (Py_ssize_t) annotation.size()));
    if (PyDict_SetItemString(typing_entries, qualname.c_str(), value.get()) < 0)
        throw PyError{};
}

namespace {

constexpr std::string_view signature_end = "\n--\n\n";

// Index of the first `stop` character outside brackets and string literals in text[from..], or npos.
size_t find_top_level(std::string_view text, std::string_view stops, size_t from = 0) {
    int depth = 0;
    char quote = 0;
    for (size_t i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == '\\')
                ++i;
            else if (c == quote)
                quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (depth == 0 && stops.find(c) != std::string_view::npos) {
            return i;
        } else if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        }
    }
    return std::string_view::npos;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ')
        text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ')
        text.remove_suffix(1);
    return text;
}

// "a: int, b: str = 'x'" -> "a, b='x'" (the form CPython's __text_signature__ accepts)
std::string strip_annotations(std::string_view params) {
    std::string result;
    size_t start = 0;
    while (start <= params.size()) {
        size_t end = find_top_level(params, ",", start);
        if (end == std::string_view::npos)
            end = params.size();
        const std::string_view param = trim(params.substr(start, end - start));
        if (!param.empty()) {
            const size_t equals = find_top_level(param, "=");
            const size_t colon = find_top_level(param, ":");
            const std::string_view name =
                trim(param.substr(0, std::min(colon, equals) == std::string_view::npos ? param.size()
                                                                                      : std::min(colon, equals)));
            if (!result.empty())
                result += ", ";
            result += name;
            if (equals != std::string_view::npos) {
                result += '=';
                result += trim(param.substr(equals + 1));
            }
        }
        start = end + 1;
    }
    return result;
}

// Docs rewritten for CPython; must outlive the types.
std::vector<std::unique_ptr<std::string>> doc_store;

const char *store_doc(std::string text) {
    doc_store.push_back(std::make_unique<std::string>(std::move(text)));
    return doc_store.back()->c_str();
}

} // namespace

const char *typed_method_doc(const std::string &qualname, const char *doc, bool is_method) {
    if (!doc)
        return doc;
    const std::string_view text(doc);
    const size_t open = text.find('(');
    const size_t marker = text.find(signature_end);
    if (open == std::string_view::npos || marker == std::string_view::npos || open > marker)
        return doc;
    const size_t close = find_top_level(text, ")", open + 1);
    if (close == std::string_view::npos || close > marker)
        return doc;
    const std::string_view params = text.substr(open + 1, close - open - 1);
    const std::string_view returns = trim(text.substr(close + 1, marker - close - 1));
    if (returns.rfind("->", 0) != 0)
        return doc;  // not annotated: left as is (tests flag missing annotations)
    add_typing(qualname, std::string("(") + std::string(params) + ") " + std::string(returns));
    // "$self" marks the bound instance for inspect.signature of method descriptors
    std::string plain = strip_annotations(params);
    if (is_method)
        plain = plain.empty() ? "$self" : "$self, " + plain;
    return store_doc(std::string(text.substr(0, open + 1)) + plain + ")" + std::string(text.substr(marker)));
}

const char *typed_attribute_doc(const std::string &qualname, const char *doc) {
    if (!doc)
        return doc;
    const std::string_view text(doc);
    const size_t marker = text.find(signature_end);
    if (marker == std::string_view::npos)
        return doc;
    add_typing(qualname, trim(text.substr(0, marker)));
    return store_doc(std::string(text.substr(marker + signature_end.size())));
}

int typed_members(const char *owner, PyMethodDef *methods, PyGetSetDef *getsets) {
    try {
        const std::string prefix = owner && *owner ? std::string(owner) + "." : std::string();
        for (PyMethodDef *def = methods; def && def->ml_name; ++def)
            def->ml_doc = typed_method_doc(prefix + def->ml_name, def->ml_doc, !prefix.empty());
        for (PyGetSetDef *def = getsets; def && def->name; ++def) {
            def->doc = typed_attribute_doc(prefix + def->name, def->doc);
            if (!def->set) {
                Ref name = Ref::check(PyUnicode_FromString((prefix + def->name).c_str()));
                if (PySet_Add(readonly_entries, name.get()) < 0)
                    throw PyError{};
            }
        }
        return 0;
    } catch (const PyError &) {
        return -1;
    }
}

} // namespace pysmdl
