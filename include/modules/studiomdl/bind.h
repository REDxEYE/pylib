//
// pylib.studiomdl binding framework (internal). The module wraps studiomdl_v2's C++ API (smdl::*) in Python classes:
//
//  * Model owns an smdl::Model. Every other object (Bone, Mesh, Geometry, ...) is a View<V>: a strong reference to its
//    Model plus a small C++ value (a handle wrapper or an id). Views never own anything else, so there are no
//    reference cycles and no GC support is needed. Two views are equal when they refer to the same model object.
//  * Settings with a single value are properties. studiomdl has no getters for them, so a property reads back the
//    value last assigned through Python (None until assigned, meaning studiomdl's default). The values live in the
//    model's `options` dict under a key made from the object and the property name.
//  * Builder calls are methods with keyword arguments (Args parses them).
//  * C++ exceptions never cross into Python: guarded() translates smdl::Error into the StudioMdlError family and
//    std exceptions into MemoryError/RuntimeError. Converters signal a Python error with `throw PyError{}`.
//  * Arrays (vertex data, ids, weights) are read through the buffer protocol without copying when they are float32 /
//    32 bit integer rows with contiguous columns (numpy arrays, array.array, memoryview); any other iterable of rows is
//    copied.
//

#ifndef PYLIB_STUDIOMDL_BIND_H
#define PYLIB_STUDIOMDL_BIND_H

#include <Python.h>
#include <studiomdl/studiomdl.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace pysmdl {

// A Python exception is set; unwind to guarded().
struct PyError {};

[[noreturn]] inline void throw_py() { throw PyError{}; }

[[noreturn]] void raise(PyObject *type, const char *format, ...);

// Owned reference.
class Ref {
public:
    Ref() = default;
    explicit Ref(PyObject *object) : m_Object(object) {}
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    Ref(Ref &&other) noexcept : m_Object(other.release()) {}
    Ref &operator=(Ref &&other) noexcept {
        if (this != &other) {
            Py_XDECREF(m_Object);
            m_Object = other.release();
        }
        return *this;
    }
    ~Ref() { Py_XDECREF(m_Object); }

    // Throws PyError when the object is null (a failed CPython call).
    static Ref check(PyObject *object) {
        if (!object)
            throw_py();
        return Ref(object);
    }

    PyObject *get() const { return m_Object; }
    PyObject *release() {
        PyObject *object = m_Object;
        m_Object = nullptr;
        return object;
    }
    explicit operator bool() const { return m_Object != nullptr; }

private:
    PyObject *m_Object = nullptr;
};

// ---------------------------------------------------------------------------------------------------------------------
// Errors

// The StudioMdlError family, created by init_errors().
struct ErrorTypes {
    PyObject *base = nullptr;            // StudioMdlError(Exception)
    PyObject *invalid_argument = nullptr; // InvalidArgumentError(StudioMdlError, ValueError)
    PyObject *out_of_range = nullptr;    // OutOfRangeError(StudioMdlError, IndexError)
    PyObject *duplicate_name = nullptr;  // DuplicateNameError(StudioMdlError, ValueError)
    PyObject *not_found = nullptr;       // NotFoundError(StudioMdlError, KeyError)
    PyObject *limit = nullptr;           // LimitError(StudioMdlError, ValueError)
    PyObject *syntax = nullptr;          // ExpressionSyntaxError(StudioMdlError, ValueError)
    PyObject *compile = nullptr;         // CompileError(StudioMdlError)
    PyObject *qc = nullptr;              // QcError(CompileError)
    PyObject *io = nullptr;              // StudioMdlIOError(StudioMdlError, OSError)
    PyObject *internal = nullptr;        // InternalError(StudioMdlError, RuntimeError)
};
extern ErrorTypes errors;

int init_errors(PyObject *module);

// Raises the exception of a studiomdl status. messages: compile messages (list) or null; qc: raise QcError with the QC
// file and line.
void set_smdl_error(smdl_status status, std::string_view message, std::string_view object = {},
                    PyObject *messages = nullptr, std::string_view file = {}, int line = 0, bool qc = false);

// Translates the exception being handled (call inside a catch block). messages as above.
void translate_current_exception(PyObject *messages = nullptr);

// Runs f(), returning its result, or null with a Python exception set when it throws.
template<class F>
auto guarded(F &&f) noexcept -> decltype(f()) {
    try {
        return f();
    } catch (const PyError &) {
    } catch (...) {
        translate_current_exception();
    }
    if constexpr (std::is_pointer_v<decltype(f())>)
        return nullptr;
    else
        return -1;  // setters (int)
}

// ---------------------------------------------------------------------------------------------------------------------
// Enums: a Python IntEnum/IntFlag per C++ enum (or flag word), registered once.

template<class E>
struct EnumRegistry {
    static inline PyObject *type = nullptr;   // strong reference held for the life of the process
    static inline const char *name = nullptr; // Python name, for type annotations
};

int add_enum(PyObject *module, PyObject **slot, const char *name, bool flags,
             std::initializer_list<std::pair<const char *, uint32_t>> members);

template<class E>
int register_enum(PyObject *module, const char *name, std::initializer_list<std::pair<const char *, uint32_t>> members,
                  bool flags = false) {
    EnumRegistry<E>::name = name;
    return add_enum(module, &EnumRegistry<E>::type, name, flags, members);
}

// Python enum member of a value (falls back to a plain int when the value has no member, e.g. combined flags of an
// IntEnum).
PyObject *enum_value(PyObject *type, uint32_t value);

// Tag types for flag words that are plain uint32_t in C++.
struct ContentsFlags {};
struct BoneSaveFlags {};

// ---------------------------------------------------------------------------------------------------------------------
// Scalar converters: Conv<T>::from(object, name) throws PyError (TypeError/ValueError) and Conv<T>::to(value) returns
// a new reference (the canonical value stored for property read-back).

const char *type_name(PyObject *object);
const char *type_name_of(PyTypeObject *type);

template<class T, class = void>
struct Conv;

template<>
struct Conv<bool> {
    static bool from(PyObject *object, const char *name);
    static PyObject *to(bool value) { return PyBool_FromLong(value); }
};

template<>
struct Conv<int> {
    static int from(PyObject *object, const char *name);
    static PyObject *to(int value) { return PyLong_FromLong(value); }
};

template<>
struct Conv<uint32_t> {
    static uint32_t from(PyObject *object, const char *name);
    static PyObject *to(uint32_t value) { return PyLong_FromUnsignedLong(value); }
};

template<>
struct Conv<float> {
    static float from(PyObject *object, const char *name);
    static PyObject *to(float value) { return PyFloat_FromDouble(value); }
};

template<>
struct Conv<std::string> {
    static std::string from(PyObject *object, const char *name);
    static PyObject *to(const std::string &value) {
        return PyUnicode_FromStringAndSize(value.data(), (Py_ssize_t) value.size());
    }
};

// Fixed-size float vectors (Vec2/Vec3/Vec4/Quat): any sequence of N numbers (tuple, list, mathutils.Vector, numpy).
std::vector<float> float_sequence(PyObject *object, size_t count, const char *name);

template<size_t N>
struct Conv<std::array<float, N>> {
    static std::array<float, N> from(PyObject *object, const char *name) {
        std::array<float, N> result{};
        const std::vector<float> values = float_sequence(object, N, name);
        std::copy(values.begin(), values.end(), result.begin());
        return result;
    }
    static PyObject *to(const std::array<float, N> &value) {
        PyObject *tuple = PyTuple_New(N);
        if (!tuple)
            return nullptr;
        for (size_t i = 0; i < N; ++i) {
            PyObject *item = PyFloat_FromDouble(value[i]);
            if (!item) {
                Py_DECREF(tuple);
                return nullptr;
            }
            PyTuple_SetItem(tuple, (Py_ssize_t) i, item);
        }
        return tuple;
    }
};

template<class E>
struct Conv<E, std::enable_if_t<std::is_enum_v<E>>> {
    static E from(PyObject *object, const char *name) {
        return static_cast<E>(Conv<uint32_t>::from(object, name));
    }
    static PyObject *to(E value) { return enum_value(EnumRegistry<E>::type, static_cast<uint32_t>(value)); }
};

// A flag word typed by a tag (ContentsFlags, BoneSaveFlags): converts as uint32_t, reads back as the IntFlag.
template<class Tag>
struct Flags {
    using tag = Tag;
    uint32_t value;
};

template<class Tag>
struct Conv<Flags<Tag>> {
    static Flags<Tag> from(PyObject *object, const char *name) { return {Conv<uint32_t>::from(object, name)}; }
    static PyObject *to(Flags<Tag> value) { return enum_value(EnumRegistry<Tag>::type, value.value); }
};

template<class T>
struct is_flags : std::false_type {};
template<class Tag>
struct is_flags<Flags<Tag>> : std::true_type {};

// A converted value as the C++ API takes it: std::string -> string_view, Flags -> uint32_t, else the value.
template<class T>
decltype(auto) cpp_arg(const T &value) {
    if constexpr (std::is_same_v<T, std::string>)
        return std::string_view(value);
    else if constexpr (is_flags<T>::value)
        return value.value;
    else
        return (value);
}

// Argument type -> stored C++ type (string_view arguments are converted to an owning std::string).
template<class A>
using stored_t = std::conditional_t<std::is_same_v<std::decay_t<A>, std::string_view>, std::string, std::decay_t<A>>;

// str or os.PathLike -> UTF-8 path; None -> empty when allow_none.
std::string fs_path(PyObject *object, const char *name, bool allow_none = true);

// A sequence of str -> strings.
std::vector<std::string> string_list(PyObject *object, const char *name);

// ---------------------------------------------------------------------------------------------------------------------
// Arrays: rows of `columns` float32 or 32 bit integer values.

template<class T>
class Array {
    static_assert(std::is_same_v<T, float> || std::is_same_v<T, uint32_t>);

public:
    // columns: values per row. Accepts a buffer of shape (rows, columns) (or (rows,) when columns == 1, or a flat
    // C-contiguous buffer of rows * columns values) whose columns are contiguous without copying; anything else (other
    // numeric buffer types, iterables of rows, flat iterables of numbers) is copied with per-value conversion.
    Array(PyObject *object, size_t columns, const char *name);
    ~Array() {
        if (m_HasView)
            PyBuffer_Release(&m_View);
    }
    Array(const Array &) = delete;
    Array &operator=(const Array &) = delete;

    const T *data() const { return m_Data; }
    size_t rows() const { return m_Rows; }
    size_t columns() const { return m_Columns; }
    // Bytes between rows (the C++ API's stride; never 0 here)
    size_t stride() const { return m_Stride; }
    // Rows as T[columns] values, e.g. std::span<const smdl::Vec3>
    template<class Row>
    std::span<const Row> as() const {
        static_assert(sizeof(Row) % sizeof(T) == 0);
        if (m_Stride != sizeof(Row) || m_Columns * sizeof(T) != sizeof(Row))
            return {};
        return {reinterpret_cast<const Row *>(m_Data), m_Rows};
    }

private:
    bool from_buffer(PyObject *object, const char *name);
    void from_iterable(PyObject *object, const char *name);

    Py_buffer m_View{};
    bool m_HasView = false;
    std::vector<T> m_Copy;
    const T *m_Data = nullptr;
    size_t m_Rows = 0;
    size_t m_Columns = 0;
    size_t m_Stride = 0;
};

extern template class Array<float>;
extern template class Array<uint32_t>;

// ---------------------------------------------------------------------------------------------------------------------
// Arguments: positional-or-keyword parameters, the first `positional` of which may be passed by position.

class Args {
public:
    // names: parameter names in order; required: how many of the first names are required; positional: how many of the
    // first names may be passed by position (the rest are keyword-only).
    Args(const char *function, PyObject *args, PyObject *kwargs, std::initializer_list<const char *> names,
         size_t required, size_t positional);

    // Borrowed value of parameter i, or null when not passed.
    PyObject *operator[](size_t i) const { return m_Values[i]; }
    // Passed and not None
    bool has(size_t i) const { return m_Values[i] && m_Values[i] != Py_None; }

    template<class T>
    stored_t<T> get(size_t i, stored_t<T> fallback = {}) const {
        return has(i) ? Conv<stored_t<T>>::from(m_Values[i], m_Names[i]) : fallback;
    }
    const char *name(size_t i) const { return m_Names[i]; }

private:
    std::vector<PyObject *> m_Values;
    std::vector<const char *> m_Names;
};

// ---------------------------------------------------------------------------------------------------------------------
// Model

struct ModelObject {
    PyObject_HEAD
    smdl::Model *model;
    PyObject *options;                      // dict: option key -> value last assigned
    std::vector<std::string> *bone_names;   // by BoneId (the C++ API has no bone name or parent getter)
    std::vector<smdl::BoneId> *bone_parents;
    bool busy;                              // a compile runs with the GIL released
};

extern PyTypeObject *ModelType;

// The C++ model; throws PyError (RuntimeError) while the model is compiling.
smdl::Model &model_of(ModelObject *self);

// ---------------------------------------------------------------------------------------------------------------------
// Views

template<class V>
struct ViewObject {
    PyObject_HEAD
    ModelObject *owner;
    alignas(V) unsigned char storage[sizeof(V)];
    bool constructed;

    V &value() { return *std::launder(reinterpret_cast<V *>(storage)); }
};

// Identity of a view value for equality/hashing: its handle (specialized for values without one, e.g. BoneRef).
template<class V>
uintptr_t view_key(const V &value) {
    return reinterpret_cast<uintptr_t>(value.handle());
}

template<class V>
struct ViewRegistry {
    static inline PyTypeObject *type = nullptr;
};

// New view of `value` owned by `owner` (new reference); throws PyError.
template<class V>
PyObject *make_view(ModelObject *owner, V value) {
    PyTypeObject *type = ViewRegistry<V>::type;
    auto *self = reinterpret_cast<ViewObject<V> *>(PyType_GenericAlloc(type, 0));
    if (!self)
        throw_py();
    Py_INCREF(reinterpret_cast<PyObject *>(owner));
    self->owner = owner;
    new (self->storage) V(std::move(value));
    self->constructed = true;
    return reinterpret_cast<PyObject *>(self);
}

// The view's value; throws PyError while its model compiles.
template<class V>
V &view_of(PyObject *self) {
    auto *view = reinterpret_cast<ViewObject<V> *>(self);
    model_of(view->owner);
    return view->value();
}

template<class V>
ModelObject *owner_of(PyObject *self) {
    return reinterpret_cast<ViewObject<V> *>(self)->owner;
}

// The view value of `object` when it is a view of type V owned by `owner`, else throws TypeError/ValueError.
template<class V>
V &view_arg(ModelObject *owner, PyObject *object, const char *name) {
    PyTypeObject *type = ViewRegistry<V>::type;
    if (!PyObject_TypeCheck(object, type))
        raise(PyExc_TypeError, "%s must be %s, not %.100s", name, type_name_of(type), type_name(object));
    auto *view = reinterpret_cast<ViewObject<V> *>(object);
    if (view->owner != owner)
        raise(PyExc_ValueError, "%s belongs to another model", name);
    return view->value();
}

template<class V>
void view_dealloc(PyObject *self) {
    auto *view = reinterpret_cast<ViewObject<V> *>(self);
    if (view->constructed)
        view->value().~V();
    Py_XDECREF(reinterpret_cast<PyObject *>(view->owner));
    PyTypeObject *type = Py_TYPE(self);
    auto free_fn = reinterpret_cast<freefunc>(PyType_GetSlot(type, Py_tp_free));
    free_fn(self);
    Py_DECREF(type);
}

template<class V>
PyObject *view_richcompare(PyObject *a, PyObject *b, int op) {
    if ((op != Py_EQ && op != Py_NE) || !PyObject_TypeCheck(b, ViewRegistry<V>::type))
        Py_RETURN_NOTIMPLEMENTED;
    auto *x = reinterpret_cast<ViewObject<V> *>(a);
    auto *y = reinterpret_cast<ViewObject<V> *>(b);
    const bool equal = x->owner == y->owner && view_key(x->value()) == view_key(y->value());
    return PyBool_FromLong(op == Py_EQ ? equal : !equal);
}

template<class V>
Py_hash_t view_hash(PyObject *self) {
    auto *view = reinterpret_cast<ViewObject<V> *>(self);
    Py_hash_t hash = (Py_hash_t) (((uintptr_t) view->owner >> 4) * 1000003u ^ view_key(view->value()));
    return hash == -1 ? -2 : hash;
}

PyObject *view_model_getter(PyObject *self, void *);

// Creates a view type: tp_dealloc/richcompare/hash/`model` property are added to the given methods/getsets.
int add_view_type_impl(PyObject *module, const char *qualname, const char *doc, size_t basicsize,
                       destructor dealloc, richcmpfunc compare, hashfunc hash, reprfunc repr, PyMethodDef *methods,
                       PyGetSetDef *getsets, PyTypeObject **out);

template<class V>
int add_view_type(PyObject *module, const char *qualname, const char *doc, PyMethodDef *methods, PyGetSetDef *getsets,
                  reprfunc repr = nullptr) {
    return add_view_type_impl(module, qualname, doc, sizeof(ViewObject<V>), view_dealloc<V>, view_richcompare<V>,
                              view_hash<V>, repr, methods, getsets, &ViewRegistry<V>::type);
}

// ---------------------------------------------------------------------------------------------------------------------
// Options: single-value settings exposed as properties with read-back.

// Applies a value (not null, may be None only when the converter accepts it) and returns the canonical value to store
// (new reference); throws PyError.
using OptionSetter = PyObject *(*)(PyObject *self, PyObject *value, const char *name);
// The dict key of an option of an object (new reference).
using OptionKey = PyObject *(*)(PyObject *self, const char *name);
// The model holding an object's options.
using OptionOwner = ModelObject *(*)(PyObject *self);

// Python annotation of a converted C++ type, derived at compile time (enum names resolve once registered).
template<class T>
std::string python_type() {
    if constexpr (std::is_same_v<T, bool>)
        return "bool";
    else if constexpr (std::is_same_v<T, float>)
        return "float";
    else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, uint32_t>)
        return "int";
    else if constexpr (std::is_same_v<T, std::string>)
        return "str";
    else if constexpr (is_flags<T>::value)
        return EnumRegistry<typename T::tag>::name ? EnumRegistry<typename T::tag>::name : "int";
    else if constexpr (std::is_enum_v<T>)
        return EnumRegistry<T>::name ? EnumRegistry<T>::name : "int";
    else if constexpr (requires { std::tuple_size<T>::value; }) {
        std::string result = "tuple[";
        for (size_t i = 0; i < std::tuple_size<T>::value; ++i)
            result += i ? ", float" : "float";
        return result + "]";
    } else {
        static_assert(sizeof(T) == 0, "no Python annotation for this type");
    }
}

struct OptionDef {
    const char *name;
    const char *doc;   // "Type\n--\n\nDescription", or just the description when `annotation` is set
    OptionSetter set;
    std::string (*annotation)() = nullptr;  // value type; the property's annotation is "<type> | None"
};

// PyGetSetDefs (null terminated, stored for the life of the process) for options plus extra getsets.
PyGetSetDef *option_getsets(std::initializer_list<std::span<const OptionDef>> options,
                            std::initializer_list<std::span<const PyGetSetDef>> extra, OptionKey key,
                            OptionOwner owner);

// Model options: key = the option name
PyObject *model_option_key(PyObject *self, const char *name);
ModelObject *model_option_owner(PyObject *self);

// View options: key = (type name, view key, option name)
template<class V>
PyObject *view_option_key(PyObject *self, const char *name) {
    auto *view = reinterpret_cast<ViewObject<V> *>(self);
    return Py_BuildValue("(sKs)", type_name_of(Py_TYPE(self)), (unsigned long long) view_key(view->value()), name);
}

template<class V>
ModelObject *view_option_owner(PyObject *self) {
    return reinterpret_cast<ViewObject<V> *>(self)->owner;
}

// Member function traits: the single argument type of a setter.
template<class>
struct setter_traits;
template<class C, class R, class A>
struct setter_traits<R (C::*)(A)> {
    using cls = C;
    using arg = A;
};
template<class C, class R, class A>
struct setter_traits<R (C::*)(A) const> {
    using cls = C;
    using arg = A;
};

// Annotation function of a one-argument setter's value type, for OptionDef::annotation.
template<auto Setter, class Stored = stored_t<typename setter_traits<decltype(Setter)>::arg>>
inline constexpr std::string (*setter_annotation)() = &python_type<Stored>;

// Option setter calling a one-argument smdl::Model member (any detail::Model*Api base).
template<auto Setter, class Stored = stored_t<typename setter_traits<decltype(Setter)>::arg>>
PyObject *model_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    smdl::Model &model = model_of(reinterpret_cast<ModelObject *>(self));
    (model.*Setter)(cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

// Option setter calling a one-argument member of a view value V.
template<class V, auto Setter, class Stored = stored_t<typename setter_traits<decltype(Setter)>::arg>>
PyObject *view_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    V &view = view_of<V>(self);
    (view.*Setter)(cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

// ---------------------------------------------------------------------------------------------------------------------
// Results

// Result object owning a compile result (new reference); throws PyError.
PyObject *new_result(smdl::Result &&result);
// List of Message(severity, text) (new reference, null with an exception set).
PyObject *message_list(const std::vector<std::pair<smdl::Severity, std::string>> &messages);

// ---------------------------------------------------------------------------------------------------------------------
// Bones

struct BoneRef {
    smdl::BoneId id;
};

template<>
inline uintptr_t view_key(const BoneRef &value) { return value.id; }

// Bone argument: a Bone of this model or a bone name; None -> NoBone when allow_none.
smdl::BoneId bone_arg(ModelObject *owner, PyObject *object, const char *name, bool allow_none = false);
// Bone view of an id (None for NoBone); new reference.
PyObject *bone_view(ModelObject *owner, smdl::BoneId id);

// ---------------------------------------------------------------------------------------------------------------------
// Method helpers

#define PYSMDL_METHOD(fn) reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)(void)>(fn))
#define PYSMDL_KW (METH_VARARGS | METH_KEYWORDS)

inline PyObject *none() { Py_RETURN_NONE; }

// ---------------------------------------------------------------------------------------------------------------------
// Typing: docstrings carry Python type annotations, exported for the .pyi generator.
//
//  * Methods/functions: "name(a: int, b: Bone | str | None = None) -> Bone\n--\n\nDescription". CPython can't parse
//    annotated text signatures, so the doc is rewritten to "name(a, b=None)\n--\n\n..." (inspect.signature keeps
//    working) and "(a: int, ...) -> Bone" is recorded.
//  * Attributes (getsets, options): "float | None\n--\n\nDescription" records the type and keeps the description.
//  * Records go to the module's __typing__ dict: {"Class.member" or "function": annotation}. Extra entries for members
//    CPython can't document (exception attributes, namedtuple fields) are added with add_typing(). The module's
//    __pyi_header__ string holds imports and type aliases the annotations use.

int init_typing(PyObject *module);
void add_typing(const std::string &qualname, std::string_view annotation);  // throws PyError
// Rewrites the docs of a null terminated method / getset array in place (arrays must outlive their type); owner: the
// class name, or empty for module functions. Returns -1 with an exception set on failure.
int typed_members(const char *owner, PyMethodDef *methods, PyGetSetDef *getsets);

// ---------------------------------------------------------------------------------------------------------------------
// Parts: each source file contributes Model methods/options and registers its own types and enums.

struct Part {
    std::span<const PyMethodDef> module_methods; // without the terminator
    std::span<const PyMethodDef> model_methods;  // without the terminator
    std::span<const OptionDef> model_options;
    std::span<const PyGetSetDef> model_getsets;  // without the terminator
    int (*init)(PyObject *module);               // types and enums; runs before the Model type is created
    std::span<const PyMethodDef> mesh_methods;   // methods added to Mesh (flex.cpp), without the terminator
};

// Mesh methods contributed by every part (null terminated); for the Mesh type in geometry.cpp.
PyMethodDef *collected_mesh_methods();

extern const Part part_globals;        // globals.cpp: ModelGlobalsApi
extern const Part part_skeleton;       // skeleton.cpp: ModelSkeletonApi, Bone
extern const Part part_geometry;       // geometry.cpp: Geometry (+ shapes), Mesh, BodyGroup, Lod
extern const Part part_flex;           // flex.cpp: ModelFlexApi, MeshFlexApi (Mesh methods via mesh_methods)
extern const Part part_rig;            // rig.cpp: ModelRigApi
extern const Part part_animation;      // animation.cpp: Animation, IkRule, Sequence
extern const Part part_collision;      // collision.cpp: Collision
extern const Part part_bone_dynamics;  // bone_dynamics.cpp: JiggleBone, QuatInterp, AimConstraint, AxisInterp
extern const Part part_qc;             // qc.cpp: compile_qc

} // namespace pysmdl

#endif //PYLIB_STUDIOMDL_BIND_H
