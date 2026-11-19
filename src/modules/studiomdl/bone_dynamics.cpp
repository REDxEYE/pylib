//
// pylib.studiomdl: JiggleBone, QuatInterp, AimConstraint, AxisInterp (smdl::detail::ModelBoneDynamicsApi,
// smdl_bone_dynamics.h).
//

#include "modules/studiomdl/bind.h"

namespace pysmdl {

namespace {

using B = smdl::detail::ModelBoneDynamicsApi;
using J = smdl::JiggleBone;
using Q = smdl::QuatInterp;
using A = smdl::AimConstraint;
using X = smdl::AxisInterp;

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

// A (min, max) pair of floats.
template<class V, auto Setter>
PyObject *pair_setter(PyObject *self, PyObject *value, const char *name) {
    if (!PySequence_Check(value) || PySequence_Size(value) != 2) {
        PyErr_Clear();
        raise(PyExc_TypeError, "%s must be a (min, max) pair", name);
    }
    Ref first = Ref::check(PySequence_GetItem(value, 0));
    Ref second = Ref::check(PySequence_GetItem(value, 1));
    const float low = Conv<float>::from(first.get(), name);
    const float high = Conv<float>::from(second.get(), name);
    V &view = view_of<V>(self);
    (view.*Setter)(low, high);
    Ref a = Ref::check(Conv<float>::to(low));
    Ref b = Ref::check(Conv<float>::to(high));
    return PyTuple_Pack(2, a.get(), b.get());
}

// An attachment of the model: its index, an Attachment (or any object of this model with an int `index`), or a name.
smdl::AttachmentId attachment_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (object == Py_None)
        raise(PyExc_TypeError, "%s must be an Attachment, an attachment name or an index, not None", name);
    if (PyLong_Check(object) && !PyBool_Check(object))
        return Conv<uint32_t>::from(object, name);
    Ref attachment;
    if (PyUnicode_Check(object)) {
        attachment = Ref::check(PyObject_CallMethod(reinterpret_cast<PyObject *>(owner), "find_attachment", "O", object));
        object = attachment.get();
    } else {
        Ref model(PyObject_GetAttrString(object, "model"));
        if (!model) {
            PyErr_Clear();
            raise(PyExc_TypeError, "%s must be an Attachment, an attachment name or an index, not %.100s", name,
                  type_name(object));
        }
        if (model.get() != reinterpret_cast<PyObject *>(owner))
            raise(PyExc_ValueError, "%s belongs to another model", name);
    }
    Ref index = Ref::check(PyObject_GetAttrString(object, "index"));
    return Conv<uint32_t>::from(index.get(), name);
}

// ---- JiggleBone

#define JIGGLE(name, setter, doc) OptionDef{name, doc, &view_setter<J, &J::setter>}
#define JIGGLE_PAIR(name, setter, doc) OptionDef{name, doc, &pair_setter<J, &J::setter>}

const OptionDef jiggle_options[] = {
    JIGGLE("flexible", setFlexible, "bool | None\n--\n\nFlexible section; excludes rigid."),
    JIGGLE("rigid", setRigid, "bool | None\n--\n\nRigid section; excludes flexible."),
    JIGGLE("base_spring", setBaseSpring, "bool | None\n--\n\nBase spring section."),
    JIGGLE("allow_length_flex", setAllowLengthFlex,
           "bool | None\n--\n\nallow_length_flex of the flexible section."),
    JIGGLE("length", setLength, "float | None\n--\n\nLength of the jiggle bone (default 10)."),
    JIGGLE("tip_mass", setTipMass, "float | None\n--\n\nMass of the tip."),
    JIGGLE("angle_constraint", setAngleConstraint, "float | None\n--\n\nAngle constraint, degrees."),
    JIGGLE_PAIR("yaw_constraint", setYawConstraint,
                "tuple[float, float] | None\n--\n\nYaw constraint (min, max), degrees."),
    JIGGLE("yaw_friction", setYawFriction, "float | None\n--\n\nYaw friction."),
    JIGGLE("yaw_bounce", setYawBounce, "float | None\n--\n\nYaw bounce."),
    JIGGLE_PAIR("pitch_constraint", setPitchConstraint,
                "tuple[float, float] | None\n--\n\nPitch constraint (min, max), degrees."),
    JIGGLE("pitch_friction", setPitchFriction, "float | None\n--\n\nPitch friction."),
    JIGGLE("pitch_bounce", setPitchBounce, "float | None\n--\n\nPitch bounce."),
    JIGGLE("yaw_stiffness", setYawStiffness,
           "float | None\n--\n\nFlexible yaw stiffness (clamped to 0..1000 by the compile)."),
    JIGGLE("yaw_damping", setYawDamping,
           "float | None\n--\n\nFlexible yaw damping (clamped to 0..1000 by the compile)."),
    JIGGLE("pitch_stiffness", setPitchStiffness,
           "float | None\n--\n\nFlexible pitch stiffness (clamped to 0..1000 by the compile)."),
    JIGGLE("pitch_damping", setPitchDamping,
           "float | None\n--\n\nFlexible pitch damping (clamped to 0..1000 by the compile)."),
    JIGGLE("along_stiffness", setAlongStiffness,
           "float | None\n--\n\nFlexible along stiffness (clamped to 0..1000 by the compile)."),
    JIGGLE("along_damping", setAlongDamping,
           "float | None\n--\n\nFlexible along damping (clamped to 0..1000 by the compile)."),
    JIGGLE("base_stiffness", setBaseStiffness, "float | None\n--\n\nBase spring stiffness."),
    JIGGLE("base_damping", setBaseDamping, "float | None\n--\n\nBase spring damping."),
    JIGGLE_PAIR("base_left_constraint", setBaseLeftConstraint,
                "tuple[float, float] | None\n--\n\nBase spring left constraint (min, max)."),
    JIGGLE("base_left_friction", setBaseLeftFriction, "float | None\n--\n\nBase spring left friction."),
    JIGGLE_PAIR("base_up_constraint", setBaseUpConstraint,
                "tuple[float, float] | None\n--\n\nBase spring up constraint (min, max)."),
    JIGGLE("base_up_friction", setBaseUpFriction, "float | None\n--\n\nBase spring up friction."),
    JIGGLE_PAIR("base_forward_constraint", setBaseForwardConstraint,
                "tuple[float, float] | None\n--\n\nBase spring forward constraint (min, max)."),
    JIGGLE("base_forward_friction", setBaseForwardFriction,
           "float | None\n--\n\nBase spring forward friction."),
    JIGGLE("base_mass", setBaseMass, "float | None\n--\n\nBase spring mass."),
};
#undef JIGGLE
#undef JIGGLE_PAIR

// ---- QuatInterp

PyObject *QuatInterp_set_display(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_display", args, kwargs, {"size", "distance"}, 2, 2);
        const auto size = a.get<smdl::Vec3>(0);
        const float distance = a.get<float>(1);
        view_of<Q>(self).setDisplay(size, distance);
        return none();
    });
}

PyObject *QuatInterp_add_trigger(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_trigger", args, kwargs, {"tolerance", "trigger", "angles", "position"}, 4, 4);
        const float tolerance = a.get<float>(0);
        const auto trigger = a.get<smdl::Vec3>(1);
        const auto angles = a.get<smdl::Vec3>(2);
        const auto position = a.get<smdl::Vec3>(3);
        view_of<Q>(self).addTrigger(tolerance, trigger, angles, position);
        return none();
    });
}

const OptionDef quat_interp_options[] = {
    {"base_position", "Vec3 | None\n--\n\n<basepos>: added to every trigger position (x, y, z).",
     &view_setter<Q, &Q::setBasePosition>},
    {"rotate_axis",
     "Vec3 | None\n--\n\n<rotateaxis>: degrees (x, y, z), applied to every trigger's orientation.",
     &view_setter<Q, &Q::setRotateAxis>},
    {"joint_orient",
     "Vec3 | None\n--\n\n<jointorient>: degrees (x, y, z), applied to every trigger's orientation.",
     &view_setter<Q, &Q::setJointOrient>},
};

PyMethodDef quat_interp_methods[] = {
    {"set_display", PYSMDL_METHOD(QuatInterp_set_display), PYSMDL_KW,
     "set_display(size: Vec3, distance: float) -> None\n--\n\n<display> <size x y z> <distance>: the percentage is "
     "distance / 100."},
    {"add_trigger", PYSMDL_METHOD(QuatInterp_add_trigger), PYSMDL_KW,
     "add_trigger(tolerance: float, trigger: Vec3, angles: Vec3, position: Vec3) -> None\n--\n\n"
     "<trigger> <tolerance> <trigger x y z> <angles x y z> <position x y z>: all angles in degrees; at most 32 "
     "triggers (LimitError), the tolerance must be positive."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- AimConstraint

const OptionDef aim_constraint_options[] = {
    {"aim_vector", "Vec3 | None\n--\n\n<aimvector> (x, y, z), normalized; required by the compile.",
     &view_setter<A, &A::setAimVector>},
    {"up_vector", "Vec3 | None\n--\n\n<upvector> (x, y, z), normalized; required by the compile.",
     &view_setter<A, &A::setUpVector>},
    {"base_position", "Vec3 | None\n--\n\n<basepos> (x, y, z).", &view_setter<A, &A::setBasePosition>},
};

// ---- AxisInterp

PyObject *AxisInterp_set_orientation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_orientation", args, kwargs, {"index", "position", "angles"}, 3, 3);
        const uint32_t index = a.get<uint32_t>(0);
        const auto position = a.get<smdl::Vec3>(1);
        const auto angles = a.get<smdl::Vec3>(2);
        view_of<X>(self).setOrientation(index, position, angles);
        return none();
    });
}

const OptionDef axis_interp_options[] = {
    {"base_position", "Vec3 | None\n--\n\nbasepos: added to the position of every axis entry (x, y, z).",
     &view_setter<X, &X::setBasePosition>},
};

PyMethodDef axis_interp_methods[] = {
    {"set_orientation", PYSMDL_METHOD(AxisInterp_set_orientation), PYSMDL_KW,
     "set_orientation(index: int, position: Vec3, angles: Vec3) -> None\n--\n\n"
     "axis <index 0..5> <position> <angles>: angles are a QAngle in degrees (pitch, yaw, roll)."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Model methods

PyObject *add_jiggle_bone(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_jiggle_bone", args, kwargs, {"bone"}, 1, 1);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[0], "bone");
        return make_view(model, model_of(model).addJiggleBone(bone));
    });
}

PyObject *add_quat_interp(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_quat_interp", args, kwargs, {"bone", "parent", "control_parent", "control"}, 4, 4);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[0], "bone");
        const smdl::BoneId parent = bone_arg(model, a[1], "parent");
        const smdl::BoneId control_parent = bone_arg(model, a[2], "control_parent");
        const smdl::BoneId control = bone_arg(model, a[3], "control");
        return make_view(model, model_of(model).addQuatInterp(bone, parent, control_parent, control));
    });
}

PyObject *add_aim_constraint(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_aim_constraint", args, kwargs, {"bone", "parent", "aim_bone", "aim_attachment"}, 2, 3);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[0], "bone");
        const smdl::BoneId parent = bone_arg(model, a[1], "parent");
        if (a.has(2) == a.has(3))
            raise(PyExc_TypeError, "pass exactly one of aim_bone= and aim_attachment=");
        if (a.has(2)) {
            const smdl::BoneId aim = bone_arg(model, a[2], "aim_bone");
            return make_view(model, model_of(model).addAimConstraintToBone(bone, parent, aim));
        }
        const smdl::AttachmentId attachment = attachment_arg(model, a[3], "aim_attachment");
        return make_view(model, model_of(model).addAimConstraintToAttachment(bone, parent, attachment));
    });
}

PyObject *add_axis_interp(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_axis_interp", args, kwargs, {"bone", "control", "axis"}, 3, 3);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[0], "bone");
        const smdl::BoneId control = bone_arg(model, a[1], "control");
        return make_view(model, model_of(model).addAxisInterp(bone, control, a.get<int>(2)));
    });
}

PyObject *Model_jiggle_bone_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return PyLong_FromUnsignedLong(model_of(model_ptr(self)).jiggleBoneCount());
    });
}

const PyGetSetDef model_getsets[] = {
    {"jiggle_bone_count", Model_jiggle_bone_count, nullptr, "int\n--\n\nNumber of jiggle bones added.", nullptr},
};

const PyMethodDef model_methods[] = {
    {"add_jiggle_bone", PYSMDL_METHOD(add_jiggle_bone), PYSMDL_KW,
     "add_jiggle_bone(bone: BoneLike) -> JiggleBone\n--\n\n$jigglebone: one per bone (DuplicateNameError for a "
     "second). Returns the JiggleBone to enable its sections and set its parameters."},
    {"add_quat_interp", PYSMDL_METHOD(add_quat_interp), PYSMDL_KW,
     "add_quat_interp(bone: BoneLike, parent: BoneLike, control_parent: BoneLike, control: BoneLike) -> "
     "QuatInterp\n--\n\n"
     "Procedural bones .vrd `<helper> <bone> <parent> <control parent> <control>`: a quaternion interpolation helper. "
     "Returns the QuatInterp."},
    {"add_aim_constraint", PYSMDL_METHOD(add_aim_constraint), PYSMDL_KW,
     "add_aim_constraint(bone: BoneLike, parent: BoneLike, aim_bone: BoneLike | None = None, *, "
     "aim_attachment: Attachment | str | int | None = None) -> AimConstraint\n--\n\n"
     "Procedural bones .vrd `<aimconstraint>`. The aim target is exactly one of aim_bone (a Bone or a bone name) and "
     "aim_attachment (an Attachment, its name or its index). Returns the AimConstraint; its aim_vector and up_vector "
     "are required by the compile."},
    {"add_axis_interp", PYSMDL_METHOD(add_axis_interp), PYSMDL_KW,
     "add_axis_interp(bone: BoneLike, control: BoneLike, axis: int) -> AxisInterp\n--\n\n"
     "Procedural bones file `bone` entry: axis 1..3 (x, y, z of the file). Returns the AxisInterp."},
};

int init(PyObject *module) {
    PyGetSetDef *jiggle_getsets = option_getsets({jiggle_options}, {}, view_option_key<J>, view_option_owner<J>);
    if (add_view_type<J>(module, "JiggleBone",
                         "A jiggle bone ($jigglebone) from Model.add_jiggle_bone. It has up to three sections "
                         "(flexible, rigid, base_spring); their parameters can be set once the section is enabled. "
                         "Settings are properties reading back the value last assigned (None: not assigned, the QC "
                         "default).",
                         nullptr, jiggle_getsets) < 0)
        return -1;
    PyGetSetDef *quat_getsets = option_getsets({quat_interp_options}, {}, view_option_key<Q>, view_option_owner<Q>);
    if (add_view_type<Q>(module, "QuatInterp",
                         "A quaternion interpolation helper (.vrd `<helper>`) from Model.add_quat_interp. Its "
                         "base_position, rotate_axis and joint_orient apply to all its triggers and start at zero.",
                         quat_interp_methods, quat_getsets) < 0)
        return -1;
    PyGetSetDef *aim_getsets = option_getsets({aim_constraint_options}, {}, view_option_key<A>,
                                              view_option_owner<A>);
    if (add_view_type<A>(module, "AimConstraint",
                         "An aim constraint (.vrd `<aimconstraint>`) from Model.add_aim_constraint.", nullptr,
                         aim_getsets) < 0)
        return -1;
    PyGetSetDef *axis_getsets = option_getsets({axis_interp_options}, {}, view_option_key<X>, view_option_owner<X>);
    return add_view_type<X>(module, "AxisInterp",
                            "An axis interpolation bone (procedural bones file `bone` entry) from "
                            "Model.add_axis_interp.",
                            axis_interp_methods, axis_getsets);
}

} // namespace

const Part part_bone_dynamics = {
    {},
    model_methods,
    {},
    model_getsets,
    init,
};

} // namespace pysmdl
