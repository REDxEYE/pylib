//
// pylib.studiomdl: Geometry (+ shapes), Mesh, BodyGroup, Lod (smdl::Geometry, smdl::detail::GeometryShapesApi,
// smdl::Mesh, smdl::BodyGroup, smdl::Lod, smdl::detail::ModelBodyApi, smdl::detail::ModelLodApi).
//

#include "geometry_shared.h"

namespace pysmdl {

// ---------------------------------------------------------------------------------------------------------------------
// Remembered names (shared with flex.cpp)

static Ref value_key(const char *kind, uintptr_t scope, uint32_t id) {
    return Ref::check(Py_BuildValue("(sKI)", kind, (unsigned long long) scope, (unsigned int) id));
}

void store_value(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id, PyObject *value) {
    Ref key = value_key(kind, scope, id);
    if (PyDict_SetItem(owner->options, key.get(), value) < 0)
        throw PyError{};
}

void store_value_default(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id, PyObject *value) {
    Ref key = value_key(kind, scope, id);
    if (PyDict_GetItemWithError(owner->options, key.get()))
        return;
    if (PyErr_Occurred() || PyDict_SetItem(owner->options, key.get(), value) < 0)
        throw PyError{};
}

PyObject *stored_value(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id) {
    Ref key = value_key(kind, scope, id);
    PyObject *value = PyDict_GetItemWithError(owner->options, key.get());
    if (!value) {
        if (PyErr_Occurred())
            throw PyError{};
        Py_RETURN_NONE;
    }
    Py_INCREF(value);
    return value;
}

bool find_stored_name(ModelObject *owner, const char *kind, uintptr_t scope, const std::string &name, uint32_t &id) {
    PyObject *key;
    PyObject *value;
    Py_ssize_t position = 0;
    while (PyDict_Next(owner->options, &position, &key, &value)) {
        if (!PyTuple_Check(key) || PyTuple_Size(key) != 3 || !PyUnicode_Check(value))
            continue;
        PyObject *item_kind = PyTuple_GetItem(key, 0);
        if (!PyUnicode_Check(item_kind) || PyUnicode_CompareWithASCIIString(item_kind, kind) != 0)
            continue;
        const unsigned long long item_scope = PyLong_AsUnsignedLongLong(PyTuple_GetItem(key, 1));
        if (item_scope == (unsigned long long) -1 && PyErr_Occurred())
            throw PyError{};
        if (item_scope != scope)
            continue;
        const char *text = PyUnicode_AsUTF8AndSize(value, nullptr);
        if (!text)
            throw PyError{};
        if (name == text) {
            id = (uint32_t) PyLong_AsUnsignedLong(PyTuple_GetItem(key, 2));
            return true;
        }
    }
    return false;
}

uint32_t shape_arg(ModelObject *owner, const smdl::Geometry &geometry, PyObject *object, const char *name,
                   bool allow_base) {
    if (object == Py_None) {
        if (allow_base)
            return smdl::NoId;
        raise(PyExc_TypeError, "%s must be a Shape, a shape name or a shape index, not None", name);
    }
    if (PyUnicode_Check(object))
        return geometry.findShape(Conv<std::string>::from(object, name));
    if (PyLong_Check(object))
        return Conv<uint32_t>::from(object, name);
    const ShapeRef &shape = view_arg<ShapeRef>(owner, object, name);
    if (shape.geometry.handle() != geometry.handle())
        raise(PyExc_ValueError, "%s belongs to another geometry", name);
    return shape.id;
}

namespace {

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

PyObject *utf8(std::string_view text) {
    return PyUnicode_DecodeUTF8(text.data(), (Py_ssize_t) text.size(), "replace");
}

// A rotation from rotation= (quaternion x, y, z, w) or euler= (radians); calls quat(q) or euler(e).
template<class Quat, class Euler>
void rotation_args(const Args &a, size_t rotation, size_t euler, Quat &&quat, Euler &&eul) {
    if (a.has(rotation) && a.has(euler))
        raise(PyExc_TypeError, "pass rotation= (quaternion) or euler= (radians), not both");
    if (a.has(euler))
        eul(a.get<smdl::Vec3>(euler));
    else
        quat(a.has(rotation) ? a.get<smdl::Quat>(rotation) : smdl::Quat{0.0f, 0.0f, 0.0f, 1.0f});
}

// Values per row of a 2-D array (numpy array, memoryview, sequence of rows): needed where the C++ API takes the
// row length from the data (weights).
size_t columns_of(PyObject *object, const char *name) {
    if (PyObject_CheckBuffer(object)) {
        Py_buffer view;
        if (PyObject_GetBuffer(object, &view, PyBUF_ND) == 0) {
            const int ndim = view.ndim;
            const Py_ssize_t columns = ndim == 2 ? view.shape[1] : 0;
            PyBuffer_Release(&view);
            if (ndim != 2)
                raise(PyExc_ValueError, "%s must be 2-D with shape (vertex count, influences per vertex), not %d-D",
                      name, ndim);
            return (size_t) columns;
        }
        PyErr_Clear();
    }
    if (PyUnicode_Check(object) || PyBytes_Check(object) || !PySequence_Check(object))
        raise(PyExc_TypeError, "%s must be a 2-D array or a sequence of rows, not %.100s", name, type_name(object));
    const Py_ssize_t size = PySequence_Size(object);
    if (size < 0)
        throw PyError{};
    if (size == 0)
        return 0;
    Ref row = Ref::check(PySequence_GetItem(object, 0));
    if (PyUnicode_Check(row.get()) || !PySequence_Check(row.get()))
        raise(PyExc_ValueError, "%s must hold one row of influences per vertex, not single numbers", name);
    const Py_ssize_t columns = PySequence_Size(row.get());
    if (columns < 0)
        throw PyError{};
    return (size_t) columns;
}

// ---- Shape

PyObject *Shape_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const ShapeRef &shape = view_of<ShapeRef>(self);
        return stored_value(owner_of<ShapeRef>(self), "name:shape", reinterpret_cast<uintptr_t>(shape.geometry.handle()),
                            shape.id);
    });
}

PyObject *Shape_index(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<ShapeRef>(self).id); });
}

PyObject *Shape_geometry(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return make_view(owner_of<ShapeRef>(self), smdl::Geometry(view_of<ShapeRef>(self).geometry));
    });
}

PyObject *Shape_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const ShapeRef &shape = reinterpret_cast<ViewObject<ShapeRef> *>(self)->value();
        Ref name(stored_value(owner_of<ShapeRef>(self), "name:shape",
                              reinterpret_cast<uintptr_t>(shape.geometry.handle()), shape.id));
        if (name.get() == Py_None)
            return PyUnicode_FromFormat("<Shape %u>", shape.id);
        return PyUnicode_FromFormat("<Shape %u %R>", shape.id, name.get());
    });
}

PyObject *Shape_set_normal_deltas(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_normal_deltas", args, kwargs, {"deltas"}, 1, 1);
        const ShapeRef &shape = view_of<ShapeRef>(self);
        Array<float> deltas(a[0], 3, "deltas");
        shape.geometry.setShapeNormalDeltas(shape.id, deltas.data(), deltas.rows(), deltas.stride());
        return none();
    });
}

PyObject *Shape_set_wrinkle(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_wrinkle", args, kwargs, {"values"}, 1, 1);
        const ShapeRef &shape = view_of<ShapeRef>(self);
        Array<float> values(a[0], 1, "values");
        shape.geometry.setShapeWrinkle(shape.id, values.data(), values.rows(), values.stride());
        return none();
    });
}

const PyGetSetDef shape_getsets[] = {
    {"name", Shape_name, nullptr, "str | None\n--\n\nName of the shape.", nullptr},
    {"index", Shape_index, nullptr,
     "int\n--\n\nIndex of the shape in its geometry (order of Geometry.add_shape calls).", nullptr},
    {"geometry", Shape_geometry, nullptr, "Geometry\n--\n\nThe Geometry the shape belongs to.", nullptr},
};

PyMethodDef shape_methods[] = {
    {"set_normal_deltas", PYSMDL_METHOD(Shape_set_normal_deltas), PYSMDL_KW,
     "set_normal_deltas(deltas: FloatArray) -> None\n--\n\nNormal deltas, one (x, y, z) per vertex entry of the shape "
     "(array of shape (n, 3), n = the shape's vertex count). Without them the shape leaves normals unchanged."},
    {"set_wrinkle", PYSMDL_METHOD(Shape_set_wrinkle), PYSMDL_KW,
     "set_wrinkle(values: FloatArray) -> None\n--\n\nWrinkle values, one float per vertex entry of the shape."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Geometry

#define GEOMETRY_ARRAY_SETTER(cname, pyname, argname, columns, method)                                                 \
    PyObject *cname(PyObject *self, PyObject *args, PyObject *kwargs) {                                                \
        return guarded([&]() -> PyObject * {                                                                           \
            Args a(pyname, args, kwargs, {argname}, 1, 1);                                                             \
            smdl::Geometry &geometry = view_of<smdl::Geometry>(self);                                                  \
            Array<float> values(a[0], columns, argname);                                                               \
            geometry.method(values.data(), values.rows(), values.stride());                                            \
            return none();                                                                                             \
        });                                                                                                            \
    }

GEOMETRY_ARRAY_SETTER(Geometry_set_positions, "set_positions", "positions", 3, setPositions)
GEOMETRY_ARRAY_SETTER(Geometry_set_normals, "set_normals", "normals", 3, setNormals)
GEOMETRY_ARRAY_SETTER(Geometry_set_tangents, "set_tangents", "tangents", 4, setTangents)
GEOMETRY_ARRAY_SETTER(Geometry_set_corner_normals, "set_corner_normals", "normals", 3, setCornerNormals)
#undef GEOMETRY_ARRAY_SETTER

PyObject *Geometry_set_uvs(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_uvs", args, kwargs, {"uvs", "uv_set"}, 1, 2);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        const uint32_t uv_set = a.get<uint32_t>(1);
        Array<float> uvs(a[0], 2, "uvs");
        geometry.setUVs(uv_set, uvs.data(), uvs.rows(), uvs.stride());
        return none();
    });
}

PyObject *Geometry_set_corner_uvs(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_corner_uvs", args, kwargs, {"uvs", "uv_set"}, 1, 2);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        const uint32_t uv_set = a.get<uint32_t>(1);
        Array<float> uvs(a[0], 2, "uvs");
        geometry.setCornerUVs(uv_set, uvs.data(), uvs.rows(), uvs.stride());
        return none();
    });
}

PyObject *Geometry_set_weights(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_weights", args, kwargs, {"bones", "weights"}, 2, 2);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        const size_t influences = columns_of(a[0], "bones");
        const size_t weight_columns = columns_of(a[1], "weights");
        if (influences != weight_columns)
            raise(PyExc_ValueError, "bones has %zu influences per vertex but weights has %zu", influences,
                  weight_columns);
        if (influences == 0 || influences > 1024)
            raise(PyExc_ValueError, "influences per vertex must be in 1..1024, not %zu", influences);
        Array<uint32_t> bones(a[0], influences, "bones");
        Array<float> weights(a[1], influences, "weights");
        if (bones.rows() != weights.rows())
            raise(PyExc_ValueError, "bones has %zu rows but weights has %zu (one row per vertex)", bones.rows(),
                  weights.rows());
        geometry.setWeights((uint32_t) influences, bones.data(), bones.stride(), weights.data(), weights.stride(),
                            bones.rows());
        return none();
    });
}

PyObject *Geometry_add_material(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_material", args, kwargs, {"name"}, 1, 1);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        return PyLong_FromUnsignedLong(geometry.addMaterial(a.get<std::string>(0)));
    });
}

template<bool Quads>
PyObject *Geometry_set_polygons(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        constexpr size_t corners = Quads ? 4 : 3;
        Args a(Quads ? "set_quads" : "set_triangles", args, kwargs, {"indices", "materials"}, 1, 2);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        Array<uint32_t> indices(a[0], corners, "indices");
        std::optional<Array<uint32_t>> materials;
        if (a.has(1)) {
            materials.emplace(a[1], 1, "materials");
            if (materials->rows() != indices.rows())
                raise(PyExc_ValueError, "materials needs one id per %s: %zu ids for %zu %ss",
                      Quads ? "quad" : "triangle", materials->rows(), indices.rows(), Quads ? "quad" : "triangle");
        }
        const uint32_t *material_data = materials ? materials->data() : nullptr;
        const size_t material_stride = materials ? materials->stride() : 0;
        if (Quads)
            geometry.setQuads(indices.data(), indices.rows(), indices.stride(), material_data, material_stride);
        else
            geometry.setTriangles(indices.data(), indices.rows(), indices.stride(), material_data, material_stride);
        return none();
    });
}

PyObject *Geometry_set_rest_pose(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_rest_pose", args, kwargs, {"bone", "position", "rotation", "euler"}, 1, 2);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        ModelObject *owner = owner_of<smdl::Geometry>(self);
        const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
        const auto position = a.get<smdl::Vec3>(1);
        rotation_args(a, 2, 3, [&](const smdl::Quat &q) { geometry.setRestPose(bone, position, q); },
                      [&](const smdl::Vec3 &e) { geometry.setRestPoseEuler(bone, position, e); });
        return none();
    });
}

PyObject *Geometry_set_append_offset(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_append_offset", args, kwargs, {"position", "angles", "scale"}, 0, 3);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        geometry.setAppendOffset(a.get<smdl::Vec3>(0), a.get<smdl::Vec3>(1), a.get<float>(2, 1.0f));
        return none();
    });
}

PyObject *Geometry_add_shape(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_shape", args, kwargs, {"name", "vertices", "position_deltas", "normal_deltas", "wrinkle"}, 3, 3);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        ModelObject *owner = owner_of<smdl::Geometry>(self);
        const std::string name = a.get<std::string>(0);
        Array<uint32_t> vertices(a[1], 1, "vertices");
        Array<float> deltas(a[2], 3, "position_deltas");
        if (vertices.rows() != deltas.rows())
            raise(PyExc_ValueError, "%zu vertices but %zu position deltas", vertices.rows(), deltas.rows());
        std::optional<Array<float>> normal_deltas;
        if (a.has(3)) {
            normal_deltas.emplace(a[3], 3, "normal_deltas");
            if (normal_deltas->rows() != vertices.rows())
                raise(PyExc_ValueError, "%zu vertices but %zu normal deltas", vertices.rows(), normal_deltas->rows());
        }
        std::optional<Array<float>> wrinkle;
        if (a.has(4)) {
            wrinkle.emplace(a[4], 1, "wrinkle");
            if (wrinkle->rows() != vertices.rows())
                raise(PyExc_ValueError, "%zu vertices but %zu wrinkle values", vertices.rows(), wrinkle->rows());
        }
        const smdl::ShapeId id = geometry.addShape(name, vertices.data(), vertices.rows(), vertices.stride(),
                                                   deltas.data(), deltas.stride());
        if (normal_deltas)
            geometry.setShapeNormalDeltas(id, normal_deltas->data(), normal_deltas->rows(), normal_deltas->stride());
        if (wrinkle)
            geometry.setShapeWrinkle(id, wrinkle->data(), wrinkle->rows(), wrinkle->stride());
        Ref text = Ref::check(Conv<std::string>::to(name));
        store_value(owner, "name:shape", reinterpret_cast<uintptr_t>(geometry.handle()), id, text.get());
        return make_view(owner, ShapeRef{geometry, id});
    });
}

PyObject *Geometry_find_shape(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_shape", args, kwargs, {"name"}, 1, 1);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        const std::string name = a.get<std::string>(0);
        const smdl::ShapeId id = geometry.findShape(name);
        Ref text = Ref::check(Conv<std::string>::to(name));
        store_value_default(owner_of<smdl::Geometry>(self), "name:shape",
                            reinterpret_cast<uintptr_t>(geometry.handle()), id, text.get());
        return make_view(owner_of<smdl::Geometry>(self), ShapeRef{geometry, id});
    });
}

PyObject *Geometry_set_balance(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_balance", args, kwargs, {"values"}, 1, 1);
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        Array<float> values(a[0], 1, "values");
        geometry.setBalance(values.data(), values.rows(), values.stride());
        return none();
    });
}

PyObject *Geometry_shapes(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        smdl::Geometry &geometry = view_of<smdl::Geometry>(self);
        const uint32_t count = geometry.shapeCount();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(owner_of<smdl::Geometry>(self), ShapeRef{geometry, i}));
        return tuple.release();
    });
}

template<class V, auto Fn>
PyObject *count_getter(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong((view_of<V>(self).*Fn)()); });
}

PyObject *Geometry_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const smdl::Geometry &geometry = reinterpret_cast<ViewObject<smdl::Geometry> *>(self)->value();
        return PyUnicode_FromFormat("<Geometry: %u vertices, %u triangles>", geometry.vertexCount(),
                                    geometry.triangleCount());
    });
}

const OptionDef geometry_options[] = {
    {"flip_triangles",
     "bool | None\n--\n\nReverse the winding of every triangle (the `reverse` option of $body / studio).",
     &view_setter<smdl::Geometry, &smdl::Geometry::setFlipTriangles>},
    {"relative_materials",
     "bool | None\n--\n\nMaterial names are relative to materials/ like those of a DMX file; the compile also "
     "searches an empty $cdmaterials path.",
     &view_setter<smdl::Geometry, &smdl::Geometry::setRelativeMaterials>},
    {"indexed_corners",
     "bool | None\n--\n\nUse input vertex indices to unify corners, preserving separate vertices at the same "
     "position. Corners still split on material, normals and UVs. Default False retains SMD-style position welding.",
     &view_setter<smdl::Geometry, &smdl::Geometry::setIndexedCorners>},
    {"source_name",
     "str | None\n--\n\nFile name of the source built from this geometry as the compile's messages and log name it "
     "(\"body.blend\"), used as it is; by default the mesh name with \".smd\".",
     &view_setter<smdl::Geometry, &smdl::Geometry::setSourceName>},
    {"scale",
     "float | None\n--\n\nScale of positions and rest poses of this geometry; 0 uses the model scale.",
     &view_setter<smdl::Geometry, &smdl::Geometry::setScale>},
};

const PyGetSetDef geometry_getsets[] = {
    {"vertex_count", count_getter<smdl::Geometry, &smdl::Geometry::vertexCount>, nullptr,
     "int\n--\n\nNumber of vertices (rows of the positions).", nullptr},
    {"triangle_count", count_getter<smdl::Geometry, &smdl::Geometry::triangleCount>, nullptr,
     "int\n--\n\nNumber of triangles.", nullptr},
    {"quad_count", count_getter<smdl::Geometry, &smdl::Geometry::quadCount>, nullptr,
     "int\n--\n\nNumber of quads.", nullptr},
    {"material_count", count_getter<smdl::Geometry, &smdl::Geometry::materialCount>, nullptr,
     "int\n--\n\nNumber of materials added with add_material.", nullptr},
    {"shape_count", count_getter<smdl::Geometry, &smdl::Geometry::shapeCount>, nullptr,
     "int\n--\n\nNumber of shapes.", nullptr},
    {"shapes", Geometry_shapes, nullptr, "tuple[Shape, ...]\n--\n\nThe shapes in index order.", nullptr},
};

PyMethodDef geometry_methods[] = {
    {"set_positions", PYSMDL_METHOD(Geometry_set_positions), PYSMDL_KW,
     "set_positions(positions: FloatArray) -> None\n--\n\nVertex positions in Source units (Z up): float32 array of "
     "shape (n, 3). Defines the vertex count; set it first, then the per-vertex arrays, then the triangles, then the "
     "per-corner arrays."},
    {"set_normals", PYSMDL_METHOD(Geometry_set_normals), PYSMDL_KW,
     "set_normals(normals: FloatArray) -> None\n--\n\nVertex normals, float32 (n, 3), n = the vertex count. Normals "
     "are required: here or with set_corner_normals."},
    {"set_tangents", PYSMDL_METHOD(Geometry_set_tangents), PYSMDL_KW,
     "set_tangents(tangents: FloatArray) -> None\n--\n\nOptional vertex tangents, float32 (n, 4): x, y, z and the "
     "handedness w (+1/-1). Without them studiomdl computes the tangent space."},
    {"set_uvs", PYSMDL_METHOD(Geometry_set_uvs), PYSMDL_KW,
     "set_uvs(uvs: FloatArray, uv_set: int = 0) -> None\n--\n\nUVs of a set (0..7), float32 (n, 2), n = the vertex "
     "count. u to the right, v up from the bottom-left corner (set 0 is stored as 1 - v like an SMD file); fill the "
     "sets from 0 upwards."},
    {"set_weights", PYSMDL_METHOD(Geometry_set_weights), PYSMDL_KW,
     "set_weights(bones: IntArray, weights: FloatArray) -> None\n--\n\nBone weights: bones is a uint32 array of bone "
     "indices (see Bone.index), weights a float32 array, both of shape (n, k): k influences per vertex (1..16; "
     "trim more to the 16 largest first, else InvalidArgumentError), n = the vertex count. Pad short rows with weight 0. studiomdl keeps the 3 largest influences per vertex and "
     "renormalizes. Without weights every vertex is bound to bone 0."},
    {"add_material", PYSMDL_METHOD(Geometry_add_material), PYSMDL_KW,
     "add_material(name: str) -> int\n--\n\nAdd a material (path relative to the $cdmaterials paths, like the "
     "texture name of an SMD triangle) and return its id (0, 1, 2... in call order) for the materials argument of "
     "set_triangles. Names are unique in a geometry."},
    {"set_triangles", PYSMDL_METHOD(Geometry_set_polygons<false>), PYSMDL_KW,
     "set_triangles(indices: IntArray, materials: IntArray | None = None) -> None\n--\n\nTriangles: uint32 array of "
     "shape (n, 3) of vertex indices, counter-clockwise like SMD corners. materials: one material id per triangle "
     "(shape (n,)); None puts every triangle in material 0."},
    {"set_quads", PYSMDL_METHOD(Geometry_set_polygons<true>), PYSMDL_KW,
     "set_quads(indices: IntArray, materials: IntArray | None = None) -> None\n--\n\nQuads for $subd models: uint32 "
     "array of shape (n, 4) of vertex indices; materials as for set_triangles. The model must be subd."},
    {"set_corner_normals", PYSMDL_METHOD(Geometry_set_corner_normals), PYSMDL_KW,
     "set_corner_normals(normals: FloatArray) -> None\n--\n\nNormals per triangle corner, float32 (3 * triangle "
     "count, 3) in triangle order; they replace the indexed normals."},
    {"set_corner_uvs", PYSMDL_METHOD(Geometry_set_corner_uvs), PYSMDL_KW,
     "set_corner_uvs(uvs: FloatArray, uv_set: int = 0) -> None\n--\n\nUVs of a set per triangle corner, float32 "
     "(3 * triangle count, 2); they replace the indexed UVs of that set."},
    {"set_rest_pose", PYSMDL_METHOD(Geometry_set_rest_pose), PYSMDL_KW,
     "set_rest_pose(bone: BoneLike, position: Vec3 = (0, 0, 0), *, rotation: Quat | None = None, euler: Vec3 | None "
     "= None) -> None\n--\n\nRest pose of a bone (Bone or name) as this geometry sees it, local to the parent; "
     "replaces the skeleton's for this geometry only. rotation: quaternion (x, y, z, w), or euler: SMD angles in "
     "radians; identity by default."},
    {"set_append_offset", PYSMDL_METHOD(Geometry_set_append_offset), PYSMDL_KW,
     "set_append_offset(position: Vec3 = (0, 0, 0), angles: Vec3 = (0, 0, 0), scale: float = 1.0) -> "
     "None\n--\n\n$appendsource offset of a geometry from Mesh.append_geometry: position, angles in degrees (pitch, "
     "yaw, roll) and a uniform scale."},
    {"add_shape", PYSMDL_METHOD(Geometry_add_shape), PYSMDL_KW,
     "add_shape(name: str, vertices: IntArray, position_deltas: FloatArray, *, normal_deltas: FloatArray | None = "
     "None, wrinkle: FloatArray | None = None) -> Shape\n--\n\nAdd a shape (vertex animation) and return it: "
     "vertices is a uint32 array of vertex indices, position_deltas a float32 array (n, 3), one per index; optional "
     "normal_deltas (n, 3) and wrinkle (n,) values. Set the positions first."},
    {"find_shape", PYSMDL_METHOD(Geometry_find_shape), PYSMDL_KW,
     "find_shape(name: str) -> Shape\n--\n\nThe Shape with this name; raises NotFoundError."},
    {"set_balance", PYSMDL_METHOD(Geometry_set_balance), PYSMDL_KW,
     "set_balance(values: FloatArray) -> None\n--\n\nStereo balance per vertex (DMX `balance`): float32 array of n "
     "values in 0..1, n = the vertex count. Only reaches the MDL through combination-operator shapes and DMX "
     "eyelids."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Mesh

PyObject *Mesh_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return utf8(view_of<smdl::Mesh>(self).name()); });
}

PyObject *Mesh_geometry(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return make_view(owner_of<smdl::Mesh>(self), view_of<smdl::Mesh>(self).geometry());
    });
}

PyObject *Mesh_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = reinterpret_cast<ViewObject<smdl::Mesh> *>(self)->value().name();
        return PyUnicode_FromFormat("<Mesh '%s'>", std::string(name).c_str());
    });
}

PyObject *Mesh_append_geometry(PyObject *self, PyObject *, PyObject *) {
    return guarded([&]() -> PyObject * {
        return make_view(owner_of<smdl::Mesh>(self), view_of<smdl::Mesh>(self).appendGeometry());
    });
}

const PyGetSetDef mesh_getsets[] = {
    {"name", Mesh_name, nullptr,
     "str\n--\n\nName of the mesh (the name studiomdl writes for the model).", nullptr},
    {"geometry", Mesh_geometry, nullptr, "Geometry\n--\n\nThe Geometry (source data) of the mesh.", nullptr},
};

// ---- BodyGroup

PyObject *BodyGroup_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return utf8(view_of<smdl::BodyGroup>(self).name()); });
}

PyObject *BodyGroup_choices(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const std::vector<smdl::Mesh> choices = view_of<smdl::BodyGroup>(self).choices();
        Ref tuple = Ref::check(PyTuple_New((Py_ssize_t) choices.size()));
        for (size_t i = 0; i < choices.size(); ++i) {
            PyObject *item = choices[i].handle() ? make_view(owner_of<smdl::BodyGroup>(self), choices[i])
                                                  : (Py_INCREF(Py_None), Py_None);
            PyTuple_SetItem(tuple.get(), (Py_ssize_t) i, item);
        }
        return tuple.release();
    });
}

PyObject *BodyGroup_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = reinterpret_cast<ViewObject<smdl::BodyGroup> *>(self)->value().name();
        return PyUnicode_FromFormat("<BodyGroup '%s'>", std::string(name).c_str());
    });
}

PyObject *BodyGroup_create_mesh(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("create_mesh", args, kwargs, {"name"}, 1, 1);
        return make_view(owner_of<smdl::BodyGroup>(self),
                         view_of<smdl::BodyGroup>(self).createMesh(a.get<std::string>(0)));
    });
}

PyObject *BodyGroup_add_blank(PyObject *self, PyObject *, PyObject *) {
    return guarded([&]() -> PyObject * {
        view_of<smdl::BodyGroup>(self).addBlank();
        return none();
    });
}

const PyGetSetDef body_group_getsets[] = {
    {"name", BodyGroup_name, nullptr, "str\n--\n\nName of the body group.", nullptr},
    {"choice_count", count_getter<smdl::BodyGroup, &smdl::BodyGroup::choiceCount>, nullptr,
     "int\n--\n\nNumber of choices (meshes and blanks).", nullptr},
    {"choices", BodyGroup_choices, nullptr,
     "tuple[Mesh | None, ...]\n--\n\nThe choices in order: a Mesh, or None for a blank.", nullptr},
};

PyMethodDef body_group_methods[] = {
    {"create_mesh", PYSMDL_METHOD(BodyGroup_create_mesh), PYSMDL_KW,
     "create_mesh(name: str) -> Mesh\n--\n\nAdd a mesh as the next choice and return it. Mesh names are unique in "
     "the model, non-empty and shorter than 64 bytes."},
    {"add_blank", PYSMDL_METHOD(BodyGroup_add_blank), METH_VARARGS | METH_KEYWORDS,
     "add_blank() -> None\n--\n\nAdd a blank choice (nothing drawn): `blank` of $bodygroup."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Lod

smdl::Mesh mesh_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findMesh(Conv<std::string>::from(object, name));
    return view_arg<smdl::Mesh>(owner, object, name);
}

smdl::BodyGroup body_group_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findBodyGroup(Conv<std::string>::from(object, name));
    return view_arg<smdl::BodyGroup>(owner, object, name);
}

PyObject *Lod_is_shadow(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyBool_FromLong(view_of<smdl::Lod>(self).isShadow()); });
}

PyObject *Lod_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        return PyUnicode_FromString(reinterpret_cast<ViewObject<smdl::Lod> *>(self)->value().isShadow()
                                        ? "<Lod shadow>"
                                        : "<Lod>");
    });
}

PyObject *Lod_replace_mesh(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("replace_mesh", args, kwargs, {"mesh", "geometry"}, 1, 2);
        smdl::Lod &lod = view_of<smdl::Lod>(self);
        ModelObject *owner = owner_of<smdl::Lod>(self);
        const smdl::Mesh mesh = mesh_arg(owner, a[0], "mesh");
        if (a.has(1)) {
            const smdl::Geometry geometry = view_arg<smdl::Geometry>(owner, a[1], "geometry");
            lod.replaceMesh(mesh, geometry);
            return make_view(owner, smdl::Geometry(geometry));
        }
        return make_view(owner, lod.replaceMesh(mesh));
    });
}

PyObject *Lod_remove_mesh(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("remove_mesh", args, kwargs, {"mesh"}, 1, 1);
        view_of<smdl::Lod>(self).removeMesh(mesh_arg(owner_of<smdl::Lod>(self), a[0], "mesh"));
        return none();
    });
}

PyObject *Lod_replace_bone(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("replace_bone", args, kwargs, {"from_bone", "to_bone"}, 2, 2);
        ModelObject *owner = owner_of<smdl::Lod>(self);
        smdl::Lod &lod = view_of<smdl::Lod>(self);
        lod.replaceBone(bone_arg(owner, a[0], "from_bone"), bone_arg(owner, a[1], "to_bone"));
        return none();
    });
}

PyObject *Lod_collapse_bone_tree(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("collapse_bone_tree", args, kwargs, {"bone"}, 1, 1);
        smdl::Lod &lod = view_of<smdl::Lod>(self);
        lod.collapseBoneTree(bone_arg(owner_of<smdl::Lod>(self), a[0], "bone"));
        return none();
    });
}

PyObject *Lod_replace_material(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("replace_material", args, kwargs, {"from_material", "to_material"}, 2, 2);
        view_of<smdl::Lod>(self).replaceMaterial(a.get<std::string>(0), a.get<std::string>(1));
        return none();
    });
}

PyObject *Lod_remove_material(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("remove_material", args, kwargs, {"material"}, 1, 1);
        view_of<smdl::Lod>(self).removeMaterial(a.get<std::string>(0));
        return none();
    });
}

const OptionDef lod_options[] = {
    {"facial_animation",
     "bool | None\n--\n\nfacial / nofacial: facial animation in this LOD (a shadow LOD can't enable it).",
     &view_setter<smdl::Lod, &smdl::Lod::setFacialAnimation>},
    {"use_shadow_lod_materials", "bool | None\n--\n\nuse_shadowlod_materials (shadow LOD only).",
     &view_setter<smdl::Lod, &smdl::Lod::setUseShadowLodMaterials>},
};

const PyGetSetDef lod_getsets[] = {
    {"is_shadow", Lod_is_shadow, nullptr, "bool\n--\n\nTrue for the $shadowlod.", nullptr},
};

PyMethodDef lod_methods[] = {
    {"replace_mesh", PYSMDL_METHOD(Lod_replace_mesh), PYSMDL_KW,
     "replace_mesh(mesh: Mesh | str, geometry: Geometry | None = None) -> Geometry\n--\n\nreplacemodel: the LOD "
     "draws a replacement geometry instead of the mesh (a Mesh or mesh name). Without geometry a new Geometry is "
     "created, to be filled like a mesh geometry; with one, an existing Geometry of the model is shared. Returns the "
     "Geometry."},
    {"remove_mesh", PYSMDL_METHOD(Lod_remove_mesh), PYSMDL_KW,
     "remove_mesh(mesh: Mesh | str) -> None\n--\n\nremovemodel: the LOD does not draw the mesh (a Mesh or mesh "
     "name)."},
    {"replace_bone", PYSMDL_METHOD(Lod_replace_bone), PYSMDL_KW,
     "replace_bone(from_bone: BoneLike, to_bone: BoneLike) -> None\n--\n\nreplacebone: vertices weighted to "
     "from_bone are weighted to to_bone in this LOD (Bones or bone names)."},
    {"collapse_bone_tree", PYSMDL_METHOD(Lod_collapse_bone_tree), PYSMDL_KW,
     "collapse_bone_tree(bone: BoneLike) -> None\n--\n\nbonetreecollapse: the bone and its subtree are replaced by "
     "the bone."},
    {"replace_material", PYSMDL_METHOD(Lod_replace_material), PYSMDL_KW,
     "replace_material(from_material: str, to_material: str) -> None\n--\n\nreplacematerial: material names as in "
     "the geometries."},
    {"remove_material", PYSMDL_METHOD(Lod_remove_material), PYSMDL_KW,
     "remove_material(material: str) -> None\n--\n\nremovemesh <material>: the LOD drops every mesh that uses the "
     "material."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Model methods

PyObject *create_mesh(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("create_mesh", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).createMesh(a.get<std::string>(0)));
    });
}

PyObject *add_body_group(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_body_group", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).addBodyGroup(a.get<std::string>(0)));
    });
}

PyObject *find_mesh(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_mesh", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).findMesh(a.get<std::string>(0)));
    });
}

PyObject *find_body_group(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_body_group", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).findBodyGroup(a.get<std::string>(0)));
    });
}

PyObject *add_body_group_preset(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_body_group_preset", args, kwargs, {"name", "values"}, 2, 2);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        std::vector<std::pair<smdl::BodyGroup, uint32_t>> values;
        if (PyDict_Check(a[1])) {
            PyObject *key;
            PyObject *value;
            Py_ssize_t position = 0;
            while (PyDict_Next(a[1], &position, &key, &value))
                values.emplace_back(body_group_arg(model, key, "values key"), Conv<uint32_t>::from(value, "choice"));
        } else {
            Ref iterator(PyObject_GetIter(a[1]));
            if (!iterator) {
                PyErr_Clear();
                raise(PyExc_TypeError, "values must be a dict or a sequence of (group, choice) pairs, not %.100s",
                      type_name(a[1]));
            }
            while (PyObject *item = PyIter_Next(iterator.get())) {
                Ref owned(item);
                if (PyUnicode_Check(item) || !PySequence_Check(item) || PySequence_Size(item) != 2) {
                    PyErr_Clear();
                    raise(PyExc_TypeError, "values must hold (group, choice) pairs, not %.100s", type_name(item));
                }
                Ref group = Ref::check(PySequence_GetItem(item, 0));
                Ref choice = Ref::check(PySequence_GetItem(item, 1));
                values.emplace_back(body_group_arg(model, group.get(), "group"),
                                    Conv<uint32_t>::from(choice.get(), "choice"));
            }
            if (PyErr_Occurred())
                throw PyError{};
        }
        model_of(model).addBodyGroupPreset(name, std::span<const std::pair<smdl::BodyGroup, uint32_t>>(values));
        return none();
    });
}

PyObject *add_lod(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_lod", args, kwargs, {"switch_distance"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).addLod(a.get<float>(0)));
    });
}

PyObject *add_shadow_lod(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_shadow_lod", args, kwargs, {}, 0, 0);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).addShadowLod());
    });
}

PyObject *Model_body_group_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return PyLong_FromUnsignedLong(model_of(model_ptr(self)).bodyGroupCount());
    });
}

PyObject *Model_lod_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(model_of(model_ptr(self)).lodCount()); });
}

const PyGetSetDef model_getsets[] = {
    {"body_group_count", Model_body_group_count, nullptr,
     "int\n--\n\nNumber of body groups ($body and $bodygroup).", nullptr},
    {"lod_count", Model_lod_count, nullptr,
     "int\n--\n\nNumber of LODs added (LOD 0 is not counted, the shadow LOD is).", nullptr},
};

const PyMethodDef model_methods[] = {
    {"create_mesh", PYSMDL_METHOD(create_mesh), PYSMDL_KW,
     "create_mesh(name: str) -> Mesh\n--\n\n$body: a body group with one choice, a mesh, both called name. Returns "
     "the Mesh."},
    {"add_body_group", PYSMDL_METHOD(add_body_group), PYSMDL_KW,
     "add_body_group(name: str) -> BodyGroup\n--\n\n$bodygroup: an empty body group; add choices with "
     "BodyGroup.create_mesh / add_blank. Returns the BodyGroup."},
    {"find_mesh", PYSMDL_METHOD(find_mesh), PYSMDL_KW,
     "find_mesh(name: str) -> Mesh\n--\n\nThe Mesh with this name; raises NotFoundError."},
    {"find_body_group", PYSMDL_METHOD(find_body_group), PYSMDL_KW,
     "find_body_group(name: str) -> BodyGroup\n--\n\nThe BodyGroup with this name; raises NotFoundError."},
    {"add_body_group_preset", PYSMDL_METHOD(add_body_group_preset), PYSMDL_KW,
     "add_body_group_preset(name: str, values: Mapping[BodyGroup | str, int] | Iterable[tuple[BodyGroup | str, "
     "int]]) -> None\n--\n\n$bodygrouppreset: a named bodygroup value. values: a dict or a sequence of (group, "
     "choice index) pairs, group being a BodyGroup or its name. The choice indices are checked by the compile."},
    {"add_lod", PYSMDL_METHOD(add_lod), PYSMDL_KW,
     "add_lod(switch_distance: float) -> Lod\n--\n\n$lod: LOD 1, 2, ... in call order (at most 7, none after the "
     "shadow LOD). Returns the Lod."},
    {"add_shadow_lod", PYSMDL_METHOD(add_shadow_lod), PYSMDL_KW,
     "add_shadow_lod() -> Lod\n--\n\n$shadowlod: the last LOD (at most one). Returns the Lod."},
};

int init(PyObject *module) {
    PyGetSetDef *getsets = option_getsets({geometry_options}, {geometry_getsets}, view_option_key<smdl::Geometry>,
                                          view_option_owner<smdl::Geometry>);
    if (add_view_type<smdl::Geometry>(
            module, "Geometry",
            "The source data of a mesh (what an SMD file holds), built in memory: vertices, materials, triangles, "
            "shapes. From Mesh.geometry / Mesh.append_geometry / Lod.replace_mesh. Settings are properties reading "
            "back the value last assigned (None: studiomdl's default).",
            geometry_methods, getsets, Geometry_repr) < 0)
        return -1;

    if (add_view_type<ShapeRef>(module, "Shape",
                                "A shape (vertex animation) of a Geometry, from Geometry.add_shape / find_shape / "
                                "shapes. Flexes are made from shapes.",
                                shape_methods, option_getsets({}, {shape_getsets}, view_option_key<ShapeRef>,
                                                              view_option_owner<ShapeRef>),
                                Shape_repr) < 0)
        return -1;

    // Mesh: the methods of this part plus the ones the other parts contribute (flex.cpp)
    static std::vector<PyMethodDef> mesh_methods;
    mesh_methods.clear();
    mesh_methods.push_back({"append_geometry", PYSMDL_METHOD(Mesh_append_geometry), METH_VARARGS | METH_KEYWORDS,
                            "append_geometry() -> Geometry\n--\n\n$appendsource: add more geometry merged into this "
                            "mesh's source (use it for static props). Returns the new Geometry; configure it through "
                            "set_append_offset."});
    for (PyMethodDef *method = collected_mesh_methods(); method->ml_name; ++method)
        mesh_methods.push_back(*method);
    mesh_methods.push_back({nullptr, nullptr, 0, nullptr});
    if (add_view_type<smdl::Mesh>(module, "Mesh",
                                  "A mesh: one choice of a body group, with its Geometry (from Model.create_mesh / "
                                  "BodyGroup.create_mesh / find_mesh). Also carries the flex options of the $model "
                                  "block (eyeballs, flexes, eyelids, combination operator).",
                                  mesh_methods.data(),
                                  option_getsets({}, {mesh_getsets}, view_option_key<smdl::Mesh>,
                                                 view_option_owner<smdl::Mesh>),
                                  Mesh_repr) < 0)
        return -1;

    if (add_view_type<smdl::BodyGroup>(
            module, "BodyGroup",
            "A body group: a list of choices, each a mesh or a blank (from Model.add_body_group / create_mesh / "
            "find_body_group).",
            body_group_methods,
            option_getsets({}, {body_group_getsets}, view_option_key<smdl::BodyGroup>,
                           view_option_owner<smdl::BodyGroup>),
            BodyGroup_repr) < 0)
        return -1;

    return add_view_type<smdl::Lod>(
        module, "Lod",
        "A LOD ($lod or $shadowlod) from Model.add_lod / add_shadow_lod: lists what it changes compared with LOD 0. "
        "Settings are properties reading back the value last assigned.",
        lod_methods, option_getsets({lod_options}, {lod_getsets}, view_option_key<smdl::Lod>, view_option_owner<smdl::Lod>),
        Lod_repr);
}

} // namespace

const Part part_geometry = {
    {},
    model_methods,
    {},
    model_getsets,
    init,
};

} // namespace pysmdl
