//
// pylib.studiomdl: Collision (smdl::Collision, smdl_collision.h) and its convex sources.
//

#include "modules/studiomdl/bind.h"

namespace pysmdl {

// A convex source of the collision model ($addconvexsrc), identified by its index.
struct ConvexSourceRef {
    uint32_t index;
};

template<>
inline uintptr_t view_key(const ConvexSourceRef &value) { return value.index; }

namespace {

using C = smdl::Collision;

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

// Geometry is owned by geometry.cpp, which registers its view type when it is built.
PyObject *geometry_view(ModelObject *owner, const smdl::Geometry &geometry) {
    if (!ViewRegistry<smdl::Geometry>::type)
        raise(PyExc_RuntimeError, "Geometry objects are not available in this build");
    return make_view<smdl::Geometry>(owner, geometry);
}

smdl::Geometry geometry_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (!ViewRegistry<smdl::Geometry>::type)
        raise(PyExc_RuntimeError, "Geometry objects are not available in this build");
    if (object == Py_None)
        raise(PyExc_TypeError, "%s must be a Geometry, not None", name);
    return view_arg<smdl::Geometry>(owner, object, name);
}

// The geometry last chosen for a view (a collision model or a convex source) is remembered as its handle: a Geometry
// view in the options dict would keep the model alive.
template<class V>
void store_geometry(PyObject *self, const smdl::Geometry &geometry) {
    Ref key = Ref::check(view_option_key<V>(self, "geometry"));
    Ref value = Ref::check(PyLong_FromVoidPtr(geometry.handle()));
    if (PyDict_SetItem(owner_of<V>(self)->options, key.get(), value.get()) < 0)
        throw_py();
}

template<class V>
std::optional<smdl::Geometry> stored_geometry(PyObject *self, smdl_model *model) {
    Ref key = Ref::check(view_option_key<V>(self, "geometry"));
    PyObject *value = PyDict_GetItemWithError(owner_of<V>(self)->options, key.get());
    if (!value) {
        if (PyErr_Occurred())
            throw_py();
        return std::nullopt;
    }
    void *handle = PyLong_AsVoidPtr(value);
    if (!handle && PyErr_Occurred())
        throw_py();
    return smdl::Geometry(static_cast<smdl_geometry *>(handle), model);
}

// ---- ConvexSource

template<class>
struct source_setter_traits;
template<class R, class A>
struct source_setter_traits<R (C::*)(uint32_t, A) const> {
    using arg = A;
};

template<auto Setter, class Stored = stored_t<typename source_setter_traits<decltype(Setter)>::arg>>
PyObject *source_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    const uint32_t index = view_of<ConvexSourceRef>(self).index;
    C collision = model_of(owner_of<ConvexSourceRef>(self)).collision();
    (collision.*Setter)(index, cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

PyObject *ConvexSource_index(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(reinterpret_cast<ViewObject<ConvexSourceRef> *>(self)->value().index);
}

PyObject *ConvexSource_geometry(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        view_of<ConvexSourceRef>(self);
        ModelObject *owner = owner_of<ConvexSourceRef>(self);
        C collision = model_of(owner).collision();
        const auto geometry = stored_geometry<ConvexSourceRef>(self, collision.model());
        if (!geometry)
            Py_RETURN_NONE;
        return geometry_view(owner, *geometry);
    });
}

PyObject *ConvexSource_repr(PyObject *self) {
    return PyUnicode_FromFormat("<ConvexSource %u>",
                                reinterpret_cast<ViewObject<ConvexSourceRef> *>(self)->value().index);
}

PyObject *ConvexSource_set_offset(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_offset", args, kwargs, {"position", "angles", "scale"}, 0, 3);
        const auto position = a.get<smdl::Vec3>(0);
        const auto angles = a.get<smdl::Vec3>(1);
        const float scale = a.get<float>(2, 1.0f);
        const uint32_t index = view_of<ConvexSourceRef>(self).index;
        C collision = model_of(owner_of<ConvexSourceRef>(self)).collision();
        collision.setConvexSourceOffset(index, position, angles, scale);
        return none();
    });
}

const OptionDef convex_source_options[] = {
    {"name", "str | None\n--\n\nName of the source, a path without extension; only for a source whose geometry was "
             "created by add_convex_source (default \"collision_convex<index>\").",
     &source_setter<&C::setConvexSourceName>},
    {"concave", "bool | None\n--\n\nThe source's pieces are not split into convex parts.",
     &source_setter<&C::setConvexSourceConcave>},
};

const PyGetSetDef convex_source_getsets[] = {
    {"index", ConvexSource_index, nullptr, "int\n--\n\nIndex of the source (0, 1, ... in creation order).", nullptr},
    {"geometry", ConvexSource_geometry, nullptr, "Geometry | None\n--\n\nGeometry of the source.", nullptr},
};

PyMethodDef convex_source_methods[] = {
    {"set_offset", PYSMDL_METHOD(ConvexSource_set_offset), PYSMDL_KW,
     "set_offset(position: Vec3 = (0, 0, 0), angles: Vec3 = (0, 0, 0), scale: float = 1.0) -> None\n--\n\n"
     "offset pos[...] angle[...] scale[...]: moves the source. angles are a QAngle in degrees (pitch, yaw, roll); "
     "scale is relative to the model's scale."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Collision: source

PyObject *Collision_get_geometry(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        auto geometry = stored_geometry<C>(self, collision.model());
        if (!geometry) {
            geometry.emplace(collision.geometry());
            store_geometry<C>(self, *geometry);
        }
        return geometry_view(owner, *geometry);
    });
}

int Collision_set_geometry(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'geometry'");
        C &collision = view_of<C>(self);
        const smdl::Geometry geometry = geometry_arg(owner_of<C>(self), value, "geometry");
        collision.setGeometry(geometry);
        store_geometry<C>(self, geometry);
        return 0;
    });
}

PyObject *Collision_get_root_bone(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        Ref key = Ref::check(view_option_key<C>(self, "root_bone"));
        PyObject *id = PyDict_GetItemWithError(owner->options, key.get());
        if (!id) {
            if (PyErr_Occurred())
                throw_py();
            Py_RETURN_NONE;
        }
        return bone_view(owner, (smdl::BoneId) PyLong_AsUnsignedLong(id));
    });
}

int Collision_set_root_bone(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'root_bone'");
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        const smdl::BoneId bone = bone_arg(owner, value, "root_bone");
        collision.setRootBone(bone);
        // Stored as the id: a Bone in the options dict would keep the model alive (a cycle).
        Ref key = Ref::check(view_option_key<C>(self, "root_bone"));
        Ref id = Ref::check(PyLong_FromUnsignedLong(bone));
        if (PyDict_SetItem(owner->options, key.get(), id.get()) < 0)
            throw_py();
        return 0;
    });
}

PyObject *Collision_convex_sources(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        const uint32_t count = collision.convexSourceCount();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i) {
            Ref view = Ref::check(make_view(owner, ConvexSourceRef{i}));
            PyTuple_SetItem(tuple.get(), i, view.release());
        }
        return tuple.release();
    });
}

PyObject *Collision_add_convex_source(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_convex_source", args, kwargs, {"geometry", "name"}, 0, 1);
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        std::optional<smdl::Geometry> given;
        if (a.has(0))
            given.emplace(geometry_arg(owner, a[0], "geometry"));
        const bool has_name = a.has(1);
        const std::string name = a.get<std::string>(1);
        if (given && has_name)
            raise(PyExc_ValueError, "name only applies to a geometry created by add_convex_source; the name of a "
                                    "geometry you pass comes from its source");
        uint32_t index = 0;
        std::optional<smdl::Geometry> geometry;
        if (given) {
            index = collision.addConvexSource(*given);
            geometry = given;
        } else {
            geometry.emplace(collision.addConvexSource(&index));
        }
        if (has_name)
            collision.setConvexSourceName(index, name);
        Ref view = Ref::check(make_view(owner, ConvexSourceRef{index}));
        store_geometry<ConvexSourceRef>(view.get(), *geometry);
        if (has_name) {
            Ref key = Ref::check(view_option_key<ConvexSourceRef>(view.get(), "name"));
            Ref value = Ref::check(Conv<std::string>::to(name));
            if (PyDict_SetItem(owner->options, key.get(), value.get()) < 0)
                throw_py();
        }
        return view.release();
    });
}

// ---- Collision: whole model

PyObject *Collision_animated_friction(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("animated_friction", args, kwargs,
               {"min_friction", "max_friction", "time_in", "time_hold", "time_out"}, 5, 5);
        view_of<C>(self).setAnimatedFriction(a.get<int>(0), a.get<int>(1), a.get<float>(2), a.get<float>(3),
                                             a.get<float>(4));
        return none();
    });
}

template<const C &(C::*Command)() const>
PyObject *Collision_command(PyObject *self, PyObject *) {
    return guarded([&]() -> PyObject * {
        (view_of<C>(self).*Command)();
        return none();
    });
}

// ---- Collision: joints

PyObject *Collision_joint_skip(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_skip", args, kwargs, {"joint"}, 1, 1);
        C &collision = view_of<C>(self);
        collision.jointSkip(bone_arg(owner_of<C>(self), a[0], "joint"));
        return none();
    });
}

PyObject *Collision_joint_merge(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_merge", args, kwargs, {"parent", "child"}, 2, 2);
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        const smdl::BoneId parent = bone_arg(owner, a[0], "parent");
        const smdl::BoneId child = bone_arg(owner, a[1], "child");
        collision.jointMerge(parent, child);
        return none();
    });
}

PyObject *Collision_joint_constrain(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_constrain", args, kwargs, {"joint", "axis", "type", "min", "max", "friction"}, 3, 6);
        C &collision = view_of<C>(self);
        const smdl::BoneId joint = bone_arg(owner_of<C>(self), a[0], "joint");
        collision.jointConstrain(joint, a.get<smdl::CollisionAxis>(1), a.get<smdl::JointType>(2),
                                 a.get<float>(3, 0.0f), a.get<float>(4, 0.0f), a.get<float>(5, 1.0f));
        return none();
    });
}

#define JOINT_VALUE(function, setter, param, label)                                                                    \
    PyObject *function(PyObject *self, PyObject *args, PyObject *kwargs) {                                             \
        return guarded([&]() -> PyObject * {                                                                           \
            Args a(label, args, kwargs, {"joint", param}, 2, 2);                                                       \
            C &collision = view_of<C>(self);                                                                           \
            const smdl::BoneId joint = bone_arg(owner_of<C>(self), a[0], "joint");                                     \
            collision.setter(joint, a.get<float>(1));                                                                  \
            return none();                                                                                             \
        });                                                                                                            \
    }

JOINT_VALUE(Collision_joint_set_inertia, jointSetInertia, "inertia", "joint_set_inertia")
JOINT_VALUE(Collision_joint_set_damping, jointSetDamping, "damping", "joint_set_damping")
JOINT_VALUE(Collision_joint_set_rot_damping, jointSetRotDamping, "rot_damping", "joint_set_rot_damping")
JOINT_VALUE(Collision_joint_set_mass_bias, jointSetMassBias, "mass_bias", "joint_set_mass_bias")
#undef JOINT_VALUE

PyObject *Collision_joint_collide(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_collide", args, kwargs, {"a", "b"}, 2, 2);
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        const smdl::BoneId first = bone_arg(owner, a[0], "a");
        const smdl::BoneId second = bone_arg(owner, a[1], "b");
        collision.jointCollide(first, second);
        return none();
    });
}

PyObject *Collision_joint_no_collide(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_no_collide", args, kwargs, {"a", "b"}, 2, 2);
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        const smdl::BoneId first = bone_arg(owner, a[0], "a");
        const smdl::BoneId second = bone_arg(owner, a[1], "b");
        collision.jointNoCollide(first, second);
        return none();
    });
}

PyObject *Collision_joint_collide_all(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("joint_collide_all", args, kwargs, {"joints"}, 1, 1);
        C &collision = view_of<C>(self);
        ModelObject *owner = owner_of<C>(self);
        Ref items = Ref::check(PySequence_List(a[0]));
        const Py_ssize_t count = PyList_Size(items.get());
        std::vector<smdl::BoneId> joints;
        joints.reserve((size_t) count);
        for (Py_ssize_t i = 0; i < count; ++i)
            joints.push_back(bone_arg(owner, PyList_GetItem(items.get(), i), "joints item"));
        collision.jointCollideAll(std::span<const smdl::BoneId>(joints));
        return none();
    });
}

#define COLLISION_OPT(name, setter, doc) OptionDef{name, doc, &view_setter<C, &C::setter>}

const OptionDef collision_options[] = {
    COLLISION_OPT("jointed", setJointed,
                  "bool | None\n--\n\n$collisionjoints (True) instead of $collisionmodel: one body per joint."),
    COLLISION_OPT("source_name", setSourceName,
                  "str | None\n--\n\nName of the collision source, a path without extension (\"src/phys_box\"); "
                  "shows in the .phy. Only for the geometry created by the geometry property (default "
                  "\"collision\")."),
    COLLISION_OPT("text", setText,
                  "str | None\n--\n\nThe .phy text section (KeyValues text), applied after the commands."),
    COLLISION_OPT("mass", setMass, "float | None\n--\n\n$mass."),
    COLLISION_OPT("inertia", setInertia, "float | None\n--\n\n$inertia: default of every joint."),
    COLLISION_OPT("damping", setDamping, "float | None\n--\n\n$damping."),
    COLLISION_OPT("rot_damping", setRotDamping, "float | None\n--\n\n$rotdamping."),
    COLLISION_OPT("drag", setDrag, "float | None\n--\n\n$drag."),
    COLLISION_OPT("rolling_drag", setRollingDrag,
                  "float | None\n--\n\n$rollingDrag; accepted by studiomdl, no effect."),
    COLLISION_OPT("max_convex_pieces", setMaxConvexPieces, "int | None\n--\n\n$maxconvexpieces."),
    COLLISION_OPT("weld_position", setWeldPosition, "float | None\n--\n\n$weldposition epsilon."),
    COLLISION_OPT("weld_normal", setWeldNormal, "float | None\n--\n\n$weldnormal epsilon."),
    COLLISION_OPT("mass_center", setMassCenter, "Vec3 | None\n--\n\n$masscenter (x, y, z)."),
};
#undef COLLISION_OPT

const PyGetSetDef collision_getsets[] = {
    {"geometry", Collision_get_geometry, Collision_set_geometry,
     "Geometry\n--\n\nThe collision source geometry. Reading gives the collision model's own geometry (created on "
     "first use; fill it like a mesh geometry); assigning a Geometry of the model (for example a mesh's) uses it "
     "instead.", nullptr},
    {"root_bone", Collision_get_root_bone, Collision_set_root_bone,
     "BoneLike | None\n--\n\n$rootbone: a Bone or a bone name (None until assigned).", nullptr},
    {"convex_sources", Collision_convex_sources, nullptr,
     "tuple[ConvexSource, ...]\n--\n\nThe convex sources added with add_convex_source.", nullptr},
};

PyMethodDef collision_methods[] = {
    {"add_convex_source", PYSMDL_METHOD(Collision_add_convex_source), PYSMDL_KW,
     "add_convex_source(geometry: Geometry | None = None, *, name: str | None = None) -> ConvexSource\n--\n\n"
     "$addconvexsrc: another source of convex pieces (at most 24; LimitError beyond). With no geometry a new one is "
     "created (ConvexSource.geometry; fill it like a mesh geometry) and name sets its source name; or pass an existing "
     "Geometry of the model. Returns the ConvexSource."},
    {"animated_friction", PYSMDL_METHOD(Collision_animated_friction), PYSMDL_KW,
     "animated_friction(min_friction: int, max_friction: int, time_in: float, time_hold: float, time_out: float) -> "
     "None\n--\n\n$animatedfriction."},
    {"auto_mass", PYSMDL_METHOD(Collision_command<&C::setAutoMass>), METH_NOARGS,
     "auto_mass() -> None\n--\n\n$automass."},
    {"remove_2d", PYSMDL_METHOD(Collision_command<&C::setRemove2D>), METH_NOARGS,
     "remove_2d() -> None\n--\n\n$remove2d."},
    {"concave_per_joint", PYSMDL_METHOD(Collision_command<&C::setConcavePerJoint>), METH_NOARGS,
     "concave_per_joint() -> None\n--\n\n$concaveperjoint."},
    {"concave", PYSMDL_METHOD(Collision_command<&C::setConcave>), METH_NOARGS,
     "concave() -> None\n--\n\n$concave."},
    {"convex_hull_count_override", PYSMDL_METHOD(Collision_command<&C::setConvexHullCountOverride>), METH_NOARGS,
     "convex_hull_count_override() -> None\n--\n\n$convexhullcountoverride."},
    {"no_self_collisions", PYSMDL_METHOD(Collision_command<&C::setNoSelfCollisions>), METH_NOARGS,
     "no_self_collisions() -> None\n--\n\n$noselfcollisions."},
    {"assume_world_space", PYSMDL_METHOD(Collision_command<&C::setAssumeWorldSpace>), METH_NOARGS,
     "assume_world_space() -> None\n--\n\n$assumeworldspace."},
    {"joint_skip", PYSMDL_METHOD(Collision_joint_skip), PYSMDL_KW,
     "joint_skip(joint: BoneLike) -> None\n--\n\n$jointskip. joint: a Bone or a bone name."},
    {"joint_merge", PYSMDL_METHOD(Collision_joint_merge), PYSMDL_KW,
     "joint_merge(parent: BoneLike, child: BoneLike) -> None\n--\n\n$jointmerge: the child's vertices are added to "
     "the parent's collision model."},
    {"joint_constrain", PYSMDL_METHOD(Collision_joint_constrain), PYSMDL_KW,
     "joint_constrain(joint: BoneLike, axis: CollisionAxis, type: CollisionJointType, min: float = 0.0, "
     "max: float = 0.0, friction: float = 1.0) -> None\n--\n\n"
     "$jointconstrain. axis: CollisionAxis; type: CollisionJointType. min <= max in degrees, only used by LIMIT. "
     "friction is the QC value (the QC default is 1)."},
    {"joint_set_inertia", PYSMDL_METHOD(Collision_joint_set_inertia), PYSMDL_KW,
     "joint_set_inertia(joint: BoneLike, inertia: float) -> None\n--\n\n$jointinertia."},
    {"joint_set_damping", PYSMDL_METHOD(Collision_joint_set_damping), PYSMDL_KW,
     "joint_set_damping(joint: BoneLike, damping: float) -> None\n--\n\n$jointdamping."},
    {"joint_set_rot_damping", PYSMDL_METHOD(Collision_joint_set_rot_damping), PYSMDL_KW,
     "joint_set_rot_damping(joint: BoneLike, rot_damping: float) -> None\n--\n\n$jointrotdamping."},
    {"joint_set_mass_bias", PYSMDL_METHOD(Collision_joint_set_mass_bias), PYSMDL_KW,
     "joint_set_mass_bias(joint: BoneLike, mass_bias: float) -> None\n--\n\n$jointmassbias."},
    {"joint_collide", PYSMDL_METHOD(Collision_joint_collide), PYSMDL_KW,
     "joint_collide(a: BoneLike, b: BoneLike) -> None\n--\n\n$jointcollide: the two joints collide (the pair is "
     "added in this order)."},
    {"joint_collide_all", PYSMDL_METHOD(Collision_joint_collide_all), PYSMDL_KW,
     "joint_collide_all(joints: Iterable[BoneLike]) -> None\n--\n\n$jointcollidealltoall: every ordered pair of "
     "different joints collide (at most 32 joints)."},
    {"joint_no_collide", PYSMDL_METHOD(Collision_joint_no_collide), PYSMDL_KW,
     "joint_no_collide(a: BoneLike, b: BoneLike) -> None\n--\n\n$jointnocollide: removes the pair in both "
     "directions."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Model

PyObject *Model_collision(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).collision());
    });
}

const PyGetSetDef model_getsets[] = {
    {"collision", Model_collision, nullptr,
     "Collision\n--\n\nThe model's collision model ($collisionmodel / $collisionjoints and the commands of their "
     "block). The same object every time; a model whose collision model is never filled writes no .phy.", nullptr},
};

int init(PyObject *module) {
    if (register_enum<smdl::JointType>(module, "CollisionJointType", {{"FREE", SMDL_COLLISION_JOINT_FREE},
                                                                      {"FIXED", SMDL_COLLISION_JOINT_FIXED},
                                                                      {"LIMIT", SMDL_COLLISION_JOINT_LIMIT}}) < 0)
        return -1;
    if (register_enum<smdl::CollisionAxis>(module, "CollisionAxis", {{"X", SMDL_COLLISION_AXIS_X},
                                                                     {"Y", SMDL_COLLISION_AXIS_Y},
                                                                     {"Z", SMDL_COLLISION_AXIS_Z}}) < 0)
        return -1;
    PyGetSetDef *source_getsets = option_getsets({convex_source_options}, {convex_source_getsets},
                                                 view_option_key<ConvexSourceRef>,
                                                 view_option_owner<ConvexSourceRef>);
    if (add_view_type<ConvexSourceRef>(module, "ConvexSource",
                                       "A convex source of the collision model (Collision.add_convex_source).",
                                       convex_source_methods, source_getsets, ConvexSource_repr) < 0)
        return -1;
    PyGetSetDef *getsets = option_getsets({collision_options}, {collision_getsets}, view_option_key<C>,
                                          view_option_owner<C>);
    return add_view_type<C>(module, "Collision",
                            "The collision model of a Model (Model.collision). Commands act in call order. Settings "
                            "are properties reading back the value last assigned (None: studiomdl's default).",
                            collision_methods, getsets);
}

} // namespace

const Part part_collision = {
    {},
    {},
    {},
    model_getsets,
    init,
};

} // namespace pysmdl
