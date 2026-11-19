//
// pylib.studiomdl: the rig (smdl::detail::ModelRigApi, smdl_rig.h): attachments, hitbox sets and hitboxes, pose
// parameters, IK chains and autoplay locks, weight lists.
//
// The C++ API refers to every object by id and has no getters, so each object is a view of its id; what the Python
// side was told (names, bones, ranges) is kept in the model's options dict under the view's option keys, which makes
// the read-only properties `name`, `bone`, ... work.
//

#include "modules/studiomdl/rig.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace pysmdl {

using R = smdl::detail::ModelRigApi;

namespace {

struct HitboxRef {
    smdl::HitboxSetId set;
    uint32_t index;
};

} // namespace

template<>
inline uintptr_t view_key(const HitboxRef &value) { return ((uintptr_t) value.set << 32) | value.index; }

namespace {

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

// ---- stored (read-back) values

template<class V>
void store(PyObject *self, const char *name, PyObject *value) {
    Ref key = Ref::check(view_option_key<V>(self, name));
    if (PyDict_SetItem(owner_of<V>(self)->options, key.get(), value) < 0)
        throw PyError{};
}

template<class V, class T>
void store_value(PyObject *self, const char *name, const T &value) {
    Ref object = Ref::check(Conv<T>::to(value));
    store<V>(self, name, object.get());
}

// Borrowed, or null when nothing was stored.
template<class V>
PyObject *load(PyObject *self, const char *name) {
    Ref key = Ref::check(view_option_key<V>(self, name));
    PyObject *value = PyDict_GetItemWithError(owner_of<V>(self)->options, key.get());
    if (!value && PyErr_Occurred())
        throw PyError{};
    return value;
}

// Getter of a stored value; the getset closure is the key.
template<class V>
PyObject *stored_get(PyObject *self, void *key) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<V>(self, static_cast<const char *>(key));
        if (!value)
            Py_RETURN_NONE;
        Py_INCREF(value);
        return value;
    });
}

// Getter of a stored bone id (as a Bone)
template<class V>
PyObject *stored_bone_get(PyObject *self, void *key) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<V>(self, static_cast<const char *>(key));
        if (!value)
            Py_RETURN_NONE;
        return bone_view(owner_of<V>(self), (smdl::BoneId) PyLong_AsUnsignedLong(value));
    });
}

#define STORED(name, doc) PyGetSetDef{name, stored_get<V>, nullptr, doc, const_cast<char *>(name)}

// ---- id arguments

template<class V, class Find>
uint32_t id_arg(ModelObject *owner, PyObject *object, const char *name, Find find) {
    if (PyUnicode_Check(object))
        return find(model_of(owner), Conv<std::string>::from(object, name));
    if (PyObject_TypeCheck(object, ViewRegistry<V>::type))
        return view_arg<V>(owner, object, name).id;
    if (PyLong_Check(object))
        return Conv<uint32_t>::from(object, name);
    raise(PyExc_TypeError, "%s must be a %s, its name or its id, not %.100s", name,
          type_name_of(ViewRegistry<V>::type), type_name(object));
}

// ---- setters of properties of id views

template<class>
struct id_setter_traits;
template<class C, class Rt, class I, class A>
struct id_setter_traits<Rt (C::*)(I, A)> {
    using arg = A;
};

template<class V, auto Setter, class Stored = stored_t<typename id_setter_traits<decltype(Setter)>::arg>>
PyObject *id_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    const uint32_t id = view_of<V>(self).id;
    smdl::Model &model = model_of(owner_of<V>(self));
    (model.*Setter)(id, cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

template<class>
struct hitbox_setter_traits;
template<class C, class Rt, class I, class J, class A>
struct hitbox_setter_traits<Rt (C::*)(I, J, A)> {
    using arg = A;
};

template<auto Setter, class Stored = stored_t<typename hitbox_setter_traits<decltype(Setter)>::arg>>
PyObject *hitbox_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    const HitboxRef ref = view_of<HitboxRef>(self);
    smdl::Model &model = model_of(owner_of<HitboxRef>(self));
    (model.*Setter)(ref.set, ref.index, cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

// ---- views shared by all id types

template<class V>
PyObject *named_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const uint32_t id = reinterpret_cast<ViewObject<V> *>(self)->value().id;
        PyObject *name = load<V>(self, "name");
        if (name && PyUnicode_Check(name))
            return PyUnicode_FromFormat("<%s %u '%U'>", type_name(self), id, name);
        return PyUnicode_FromFormat("<%s %u>", type_name(self), id);
    });
}

template<class V>
PyObject *id_index(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(reinterpret_cast<ViewObject<V> *>(self)->value().id);
}

// A view of `id` remembering its name.
template<class V>
PyObject *named_view(ModelObject *owner, uint32_t id, const std::string &name) {
    Ref view(make_view(owner, V{id}));
    store_value<V>(view.get(), "name", name);
    return view.release();
}

// find_*(name): the view, with the name remembered when it isn't yet
template<class V, auto Find>
PyObject *find_named(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const uint32_t id = (model_of(model).*Find)(name);
        Ref view(make_view(model, V{id}));
        if (!load<V>(view.get(), "name"))
            store_value<V>(view.get(), "name", name);
        return view.release();
    });
}

// add_*(name) for objects that are only a name
template<class V, auto Add>
PyObject *add_named(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const uint32_t id = (model_of(model).*Add)(name);
        return named_view<V>(model, id, name);
    });
}

// Tuple of the views of ids 0..count-1
template<class V, auto Count>
PyObject *view_tuple(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        const uint32_t count = (model_of(model).*Count)();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(model, V{i}));
        return tuple.release();
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Attachment

PyObject *Attachment_redefine(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("redefine", args, kwargs, {"bone", "position"}, 1, 2);
        ModelObject *owner = owner_of<AttachmentRef>(self);
        const uint32_t id = view_of<AttachmentRef>(self).id;
        const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
        const auto position = a.get<smdl::Vec3>(1);
        model_of(owner).redefineAttachment(id, bone, position);
        store_value<AttachmentRef>(self, "bone", (uint32_t) bone);
        store_value<AttachmentRef>(self, "position", position);
        return none();
    });
}

PyObject *Attachment_set_orientation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_orientation", args, kwargs, {"angles", "euler", "rotation", "x_axis", "z_axis", "absolute"}, 0,
               0);
        const uint32_t id = view_of<AttachmentRef>(self).id;
        smdl::Model &model = model_of(owner_of<AttachmentRef>(self));
        const bool absolute = a.has(5) && a.get<bool>(5);
        if (a.has(3) != a.has(4))
            raise(PyExc_TypeError, "x_axis and z_axis must be given together");
        const int given = absolute + a.has(0) + a.has(1) + a.has(2) + a.has(3);
        if (given != 1)
            raise(PyExc_TypeError,
                  "set_orientation() takes exactly one of angles=, euler=, rotation=, x_axis= with z_axis=, absolute=True");
        if (absolute)
            model.setAttachmentAbsolute(id);
        else if (a.has(0))
            model.setAttachmentAngles(id, a.get<smdl::Vec3>(0));
        else if (a.has(1))
            model.setAttachmentEuler(id, a.get<smdl::Vec3>(1));
        else if (a.has(2))
            model.setAttachmentQuaternion(id, a.get<smdl::Quat>(2));
        else
            model.setAttachmentAxes(id, a.get<smdl::Vec3>(3), a.get<smdl::Vec3>(4));
        return none();
    });
}

PyMethodDef attachment_methods[] = {
    {"redefine", PYSMDL_METHOD(Attachment_redefine), PYSMDL_KW,
     "redefine(bone: BoneLike, position: Vec3 = (0, 0, 0)) -> None\n--\n\n$redefineattachment: starts a new revision "
     "of the attachment with a new bone (Bone or name) and position (units, before $scale). set_orientation() and "
     "the flags then describe that revision."},
    {"set_orientation", PYSMDL_METHOD(Attachment_set_orientation), PYSMDL_KW,
     "set_orientation(*, angles: Vec3 | None = None, euler: Vec3 | None = None, rotation: Quat | None = None, "
     "x_axis: Vec3 | None = None, z_axis: Vec3 | None = None, absolute: bool = False) -> None\n--\n\n"
     "Orientation of the attachment; each call replaces the previous one. Pass exactly one of: angles (QC rotate: "
     "degrees pitch, yaw, roll), euler (radians x, y, z), rotation (quaternion x, y, z, w), x_axis together with "
     "z_axis (non-zero vectors), or absolute=True."},
    {nullptr, nullptr, 0, nullptr},
};

const OptionDef attachment_options[] = {
    {"rigid", "bool | None\n--\n\n$attachment `rigid`.", &id_setter<AttachmentRef, &R::setAttachmentRigid>},
    {"world_align", "bool | None\n--\n\n$attachment `world_align`.",
     &id_setter<AttachmentRef, &R::setAttachmentWorldAlign>},
};

PyObject *Model_add_attachment(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_attachment", args, kwargs, {"name", "bone", "position"}, 2, 3);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const smdl::BoneId bone = bone_arg(model, a[1], "bone");
        const auto position = a.get<smdl::Vec3>(2);
        const uint32_t id = model_of(model).addAttachment(name, bone, position);
        Ref view(named_view<AttachmentRef>(model, id, name));
        store_value<AttachmentRef>(view.get(), "bone", (uint32_t) bone);
        store_value<AttachmentRef>(view.get(), "position", position);
        return view.release();
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Hitbox sets and hitboxes

PyObject *Hitbox_set(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return make_view(owner_of<HitboxRef>(self), HitboxSetRef{view_of<HitboxRef>(self).set});
    });
}

PyObject *Hitbox_index(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(reinterpret_cast<ViewObject<HitboxRef> *>(self)->value().index);
}

PyObject *Hitbox_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const HitboxRef ref = reinterpret_cast<ViewObject<HitboxRef> *>(self)->value();
        return PyUnicode_FromFormat("<Hitbox %u of set %u>", ref.index, ref.set);
    });
}

const OptionDef hitbox_options[] = {
    {"angles", "tuple[float, float, float] | None\n--\n\nRotation of the box in degrees (QC `$hbox ... <angles>`).",
     &hitbox_setter<&R::setHitboxAngles>},
    {"capsule_radius", "float | None\n--\n\nCapsule radius; >= 0 makes the box a capsule.",
     &hitbox_setter<&R::setHitboxCapsuleRadius>},
    {"name", "str | None\n--\n\nName of the box.", &hitbox_setter<&R::setHitboxName>},
};

PyObject *HitboxSet_hitboxes(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *owner = owner_of<HitboxSetRef>(self);
        const smdl::HitboxSetId set = view_of<HitboxSetRef>(self).id;
        PyObject *stored = load<HitboxSetRef>(self, "count");
        const uint32_t count = stored ? (uint32_t) PyLong_AsUnsignedLong(stored) : 0;
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(owner, HitboxRef{set, i}));
        return tuple.release();
    });
}

PyObject *HitboxSet_add_hitbox(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_hitbox", args, kwargs, {"group", "bone", "mins", "maxs", "angles", "capsule_radius", "name"}, 4, 4);
        ModelObject *owner = owner_of<HitboxSetRef>(self);
        const smdl::HitboxSetId set = view_of<HitboxSetRef>(self).id;
        const int group = a.get<int>(0);
        const smdl::BoneId bone = bone_arg(owner, a[1], "bone");
        const auto mins = a.get<smdl::Vec3>(2);
        const auto maxs = a.get<smdl::Vec3>(3);
        smdl::Model &model = model_of(owner);
        const uint32_t index = model.addHitbox(set, group, bone, mins, maxs);
        PyObject *stored = load<HitboxSetRef>(self, "count");
        store_value<HitboxSetRef>(self, "count", std::max(index + 1, stored ? (uint32_t) PyLong_AsUnsignedLong(stored) : 0u));
        Ref view(make_view(owner, HitboxRef{set, index}));
        store_value<HitboxRef>(view.get(), "group", (uint32_t) group);
        store_value<HitboxRef>(view.get(), "bone", (uint32_t) bone);
        store_value<HitboxRef>(view.get(), "mins", mins);
        store_value<HitboxRef>(view.get(), "maxs", maxs);
        // Optional settings go through the properties so they are read back like the others
        if (a.has(4) && PyObject_SetAttrString(view.get(), "angles", a[4]) < 0)
            throw PyError{};
        if (a.has(5) && PyObject_SetAttrString(view.get(), "capsule_radius", a[5]) < 0)
            throw PyError{};
        if (a.has(6) && PyObject_SetAttrString(view.get(), "name", a[6]) < 0)
            throw PyError{};
        return view.release();
    });
}

PyMethodDef hitbox_set_methods[] = {
    {"add_hitbox", PYSMDL_METHOD(HitboxSet_add_hitbox), PYSMDL_KW,
     "add_hitbox(group: int, bone: BoneLike, mins: Vec3, maxs: Vec3, *, angles: Vec3 | None = None, "
     "capsule_radius: float | None = None, name: str | None = None) -> Hitbox\n--\n\n"
     "$hbox: a box of hit group `group` on bone (Bone or name), corners mins/maxs in bone space (units, before "
     "$scale). Returns the Hitbox; angles (degrees), capsule_radius and name set the matching properties."},
    {nullptr, nullptr, 0, nullptr},
};

PyObject *Model_add_hit_group(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_hit_group", args, kwargs, {"group", "bone"}, 2, 2);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[1], "bone");
        model_of(model).addHitGroup(a.get<int>(0), bone);
        return none();
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Pose parameters

PyObject *Model_add_pose_parameter(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_pose_parameter", args, kwargs, {"name", "min", "max"}, 3, 3);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const float min = a.get<float>(1);
        const float max = a.get<float>(2);
        const uint32_t id = model_of(model).addPoseParameter(name, min, max);
        Ref view(named_view<PoseParamRef>(model, id, name));
        store_value<PoseParamRef>(view.get(), "min", min);
        store_value<PoseParamRef>(view.get(), "max", max);
        return view.release();
    });
}

PyObject *PoseParameter_set_loop(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_loop", args, kwargs, {"mode", "range"}, 1, 2);
        const uint32_t id = view_of<PoseParamRef>(self).id;
        smdl::Model &model = model_of(owner_of<PoseParamRef>(self));
        const auto mode = a.get<smdl::PoseLoopMode>(0);
        float range = 0.0f;
        switch (mode) {
            case SMDL_POSE_LOOP_NONE:
                if (a.has(1))
                    raise(PyExc_TypeError, "range is only used with PoseLoopMode.RANGE");
                model.clearPoseParameterLoop(id);
                break;
            case SMDL_POSE_LOOP_WRAP:
                if (a.has(1))
                    raise(PyExc_TypeError, "range is only used with PoseLoopMode.RANGE");
                model.setPoseParameterWrap(id);
                break;
            case SMDL_POSE_LOOP_RANGE:
                if (!a.has(1))
                    raise(PyExc_TypeError, "PoseLoopMode.RANGE needs range");
                range = a.get<float>(1);
                model.setPoseParameterLoop(id, range);
                break;
            default:
                raise(PyExc_ValueError, "mode must be a PoseLoopMode");
        }
        store_value<PoseParamRef>(self, "loop_mode", mode);
        if (mode == SMDL_POSE_LOOP_RANGE)
            store_value<PoseParamRef>(self, "loop_range", range);
        else
            store<PoseParamRef>(self, "loop_range", Py_None);
        return none();
    });
}

PyMethodDef pose_parameter_methods[] = {
    {"set_loop", PYSMDL_METHOD(PoseParameter_set_loop), PYSMDL_KW,
     "set_loop(mode: PoseLoopMode, range: float | None = None) -> None\n--\n\nThe loop of the pose parameter; the "
     "last call wins. mode: PoseLoopMode.NONE (no loop), WRAP (the loop range is max - min) or RANGE "
     "(`loop <range>`; range required)."},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------------------------------
// IK chains

PyObject *IkChain_set_axis(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_axis", args, kwargs, {"motion", "value"}, 2, 2);
        const uint32_t id = view_of<IkChainRef>(self).id;
        const auto motion = a.get<Flags<MotionFlags>>(0);
        const float value = a.get<float>(1);
        model_of(owner_of<IkChainRef>(self)).setIkChainAxis(id, motion.value, value);
        Ref pair = Ref::check(Py_BuildValue("(Nf)", Conv<Flags<MotionFlags>>::to(motion), (double) value));
        store<IkChainRef>(self, "axis", pair.get());
        return none();
    });
}

PyMethodDef ik_chain_methods[] = {
    {"set_axis", PYSMDL_METHOD(IkChain_set_axis), PYSMDL_KW,
     "set_axis(motion: Motion, value: float) -> None\n--\n\nThe motion type (one Motion flag, X..LZR) and value the "
     "QC writes after the end bone (\"Z -1\"). Default: Motion.Z, 0."},
    {nullptr, nullptr, 0, nullptr},
};

const OptionDef ik_chain_options[] = {
    {"height", "float | None\n--\n\n$ikchain height (default 18).", &id_setter<IkChainRef, &R::setIkChainHeight>},
    {"floor", "float | None\n--\n\n$ikchain floor (default 0).", &id_setter<IkChainRef, &R::setIkChainFloor>},
    {"pad", "float | None\n--\n\n$ikchain pad: a diameter (default 0).", &id_setter<IkChainRef, &R::setIkChainPad>},
    {"knee", "tuple[float, float, float] | None\n--\n\n$ikchain knee direction (x, y, z).",
     &id_setter<IkChainRef, &R::setIkChainKnee>},
    {"center", "tuple[float, float, float] | None\n--\n\n$ikchain center (x, y, z).",
     &id_setter<IkChainRef, &R::setIkChainCenter>},
};

PyObject *Model_add_ik_chain(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_ik_chain", args, kwargs, {"name", "end_bone"}, 2, 2);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const smdl::BoneId bone = bone_arg(model, a[1], "end_bone");
        const uint32_t id = model_of(model).addIkChain(name, bone);
        Ref view(named_view<IkChainRef>(model, id, name));
        store_value<IkChainRef>(view.get(), "end_bone", (uint32_t) bone);
        return view.release();
    });
}

PyObject *Model_add_ik_autoplay_lock(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_ik_autoplay_lock", args, kwargs, {"chain", "position_weight", "local_rotation_weight"}, 3, 3);
        ModelObject *model = model_ptr(self);
        const smdl::IkChainId chain = ik_chain_arg(model, a[0], "chain");
        return PyLong_FromUnsignedLong(
            model_of(model).addIkAutoplayLock(chain, a.get<float>(1), a.get<float>(2)));
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Weight lists

PyObject *WeightList_set_bone(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_bone", args, kwargs, {"bone", "weight", "position_weight"}, 2, 3);
        ModelObject *owner = owner_of<WeightListRef>(self);
        const uint32_t id = view_of<WeightListRef>(self).id;
        const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
        const float weight = a.get<float>(1);
        if (a.has(2))
            model_of(owner).setWeightListBone(id, bone, weight, a.get<float>(2));
        else
            model_of(owner).setWeightListBone(id, bone, weight);
        return none();
    });
}

PyMethodDef weight_list_methods[] = {
    {"set_bone", PYSMDL_METHOD(WeightList_set_bone), PYSMDL_KW,
     "set_bone(bone: BoneLike, weight: float, position_weight: float | None = None) -> None\n--\n\nThe rotation "
     "weight and position weight of a bone (Bone or name) in the list; position_weight defaults to weight (a QC "
     "entry without posweight). A second call for the same bone replaces its weights."},
    {nullptr, nullptr, 0, nullptr},
};

PyObject *Model_default_weight_list(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        model_of(model_ptr(self));
        return make_view(model_ptr(self), WeightListRef{smdl::DefaultWeightList});
    });
}

// ---------------------------------------------------------------------------------------------------------------------
// Model

const PyMethodDef model_methods[] = {
    {"add_attachment", PYSMDL_METHOD(Model_add_attachment), PYSMDL_KW,
     "add_attachment(name: str, bone: BoneLike, position: Vec3 = (0, 0, 0)) -> Attachment\n--\n\n$attachment: a new "
     "Attachment on bone (Bone or name) at position (units, before $scale). Orientation and flags are set on the "
     "Attachment."},
    {"find_attachment", PYSMDL_METHOD((find_named<AttachmentRef, &R::findAttachment>)), PYSMDL_KW,
     "find_attachment(name: str) -> Attachment\n--\n\nThe Attachment with this name; raises NotFoundError."},
    {"add_hitbox_set", PYSMDL_METHOD((add_named<HitboxSetRef, &R::addHitboxSet>)), PYSMDL_KW,
     "add_hitbox_set(name: str) -> HitboxSet\n--\n\n$hboxset: a new HitboxSet. A model without sets gets the "
     "compiler's own; to name the default set add one called \"default\"."},
    {"find_hitbox_set", PYSMDL_METHOD((find_named<HitboxSetRef, &R::findHitboxSet>)), PYSMDL_KW,
     "find_hitbox_set(name: str) -> HitboxSet\n--\n\nThe HitboxSet with this name; raises NotFoundError."},
    {"add_hit_group", PYSMDL_METHOD(Model_add_hit_group), PYSMDL_KW,
     "add_hit_group(group: int, bone: BoneLike) -> None\n--\n\n$hgroup: names bone (Bone or name) as the bone of hit "
     "group `group`."},
    {"add_pose_parameter", PYSMDL_METHOD(Model_add_pose_parameter), PYSMDL_KW,
     "add_pose_parameter(name: str, min: float, max: float) -> PoseParameter\n--\n\n$poseparameter: a new "
     "PoseParameter. A sequence's blend widens the range to cover its own."},
    {"find_pose_parameter", PYSMDL_METHOD((find_named<PoseParamRef, &R::findPoseParameter>)), PYSMDL_KW,
     "find_pose_parameter(name: str) -> PoseParameter\n--\n\nThe PoseParameter with this name; raises "
     "NotFoundError."},
    {"add_ik_chain", PYSMDL_METHOD(Model_add_ik_chain), PYSMDL_KW,
     "add_ik_chain(name: str, end_bone: BoneLike) -> IkChain\n--\n\n$ikchain: a chain ending at end_bone (Bone or "
     "name; its two parents are the knee and the hip). Returns the IkChain."},
    {"find_ik_chain", PYSMDL_METHOD((find_named<IkChainRef, &R::findIkChain>)), PYSMDL_KW,
     "find_ik_chain(name: str) -> IkChain\n--\n\nThe IkChain with this name; raises NotFoundError."},
    {"add_ik_autoplay_lock", PYSMDL_METHOD(Model_add_ik_autoplay_lock), PYSMDL_KW,
     "add_ik_autoplay_lock(chain: IkChain | str | int, position_weight: float, local_rotation_weight: float) -> "
     "int\n--\n\n$ikautoplaylock: the chain (IkChain, name or id) is locked in every sequence that doesn't override "
     "it. Returns the index of the lock."},
    {"add_weight_list", PYSMDL_METHOD((add_named<WeightListRef, &R::addWeightList>)), PYSMDL_KW,
     "add_weight_list(name: str) -> WeightList\n--\n\n$weightlist: a new WeightList."},
    {"find_weight_list", PYSMDL_METHOD((find_named<WeightListRef, &R::findWeightList>)), PYSMDL_KW,
     "find_weight_list(name: str) -> WeightList\n--\n\nThe WeightList with this name; raises NotFoundError."},
};

const PyGetSetDef model_getsets[] = {
    {"attachments", view_tuple<AttachmentRef, &R::attachmentCount>, nullptr,
     "tuple[Attachment, ...]\n--\n\nAttachments in creation order.", nullptr},
    {"hitbox_sets", view_tuple<HitboxSetRef, &R::hitboxSetCount>, nullptr,
     "tuple[HitboxSet, ...]\n--\n\nHitbox sets in creation order.", nullptr},
    {"pose_parameters", view_tuple<PoseParamRef, &R::poseParameterCount>, nullptr,
     "tuple[PoseParameter, ...]\n--\n\nPose parameters in creation order.", nullptr},
    {"ik_chains", view_tuple<IkChainRef, &R::ikChainCount>, nullptr,
     "tuple[IkChain, ...]\n--\n\nIK chains in creation order.",
     nullptr},
    {"weight_lists", view_tuple<WeightListRef, &R::weightListCount>, nullptr,
     "tuple[WeightList, ...]\n--\n\nWeight lists, the default list first.", nullptr},
    {"default_weight_list", Model_default_weight_list, nullptr,
     "WeightList\n--\n\nThe default weight list ($defaultweightlist; id 0), whose name is None.", nullptr},
};

template<class V>
int add_id_view(PyObject *module, const char *name, const char *doc, PyMethodDef *methods,
                std::initializer_list<std::span<const OptionDef>> options, std::vector<PyGetSetDef> extra) {
    static std::vector<std::unique_ptr<std::vector<PyGetSetDef>>> keep;
    extra.insert(extra.begin(), PyGetSetDef{"index", id_index<V>, nullptr,
                                            "int\n--\n\nIndex (id) of the object in the model.",
                                            nullptr});
    extra.insert(extra.begin(), PyGetSetDef{"name", stored_get<V>, nullptr,
                                            "str | None\n--\n\nName of the object (None when unknown).",
                                            const_cast<char *>("name")});
    keep.push_back(std::make_unique<std::vector<PyGetSetDef>>(std::move(extra)));
    PyGetSetDef *getsets = option_getsets(options, {std::span<const PyGetSetDef>(*keep.back())},
                                          view_option_key<V>, view_option_owner<V>);
    return add_view_type<V>(module, name, doc, methods, getsets, named_repr<V>);
}

int init(PyObject *module) {
    if (register_enum<smdl::PoseLoopMode>(module, "PoseLoopMode", {{"NONE", SMDL_POSE_LOOP_NONE},
                                                                   {"WRAP", SMDL_POSE_LOOP_WRAP},
                                                                   {"RANGE", SMDL_POSE_LOOP_RANGE}}) < 0)
        return -1;

    {
        using V = AttachmentRef;
        std::vector<PyGetSetDef> extra = {
            STORED("bone", "Bone | None\n--\n\nBone of the latest definition (add_attachment / redefine)."),
            STORED("position", "tuple[float, float, float] | None\n--\n\n"
                               "Position (x, y, z) of the latest definition, in units."),
        };
        extra[0].get = stored_bone_get<V>;
        if (add_id_view<V>(module, "Attachment",
                           "An attachment of a Model ($attachment). Flags are properties reading back the value last "
                           "assigned (None: the default).",
                           attachment_methods, {attachment_options}, std::move(extra)) < 0)
            return -1;
    }
    {
        using V = HitboxSetRef;
        std::vector<PyGetSetDef> extra = {
            {"hitboxes", HitboxSet_hitboxes, nullptr,
             "tuple[Hitbox, ...]\n--\n\nHitboxes of the set in creation order.",
             nullptr},
        };
        if (add_id_view<V>(module, "HitboxSet", "A hitbox set of a Model ($hboxset).", hitbox_set_methods, {},
                           std::move(extra)) < 0)
            return -1;
    }
    {
        using V = HitboxRef;
        std::vector<PyGetSetDef> extra = {
            {"set", Hitbox_set, nullptr, "HitboxSet\n--\n\nThe HitboxSet of the box.", nullptr},
            {"index", Hitbox_index, nullptr, "int\n--\n\nIndex of the box inside its set.", nullptr},
            STORED("group", "int\n--\n\nHit group of the box."),
            STORED("mins", "tuple[float, float, float]\n--\n\nMinimum corner in bone space (x, y, z)."),
            STORED("maxs", "tuple[float, float, float]\n--\n\nMaximum corner in bone space (x, y, z)."),
            {"bone", stored_bone_get<V>, nullptr, "Bone\n--\n\nBone of the box.", const_cast<char *>("bone")},
        };
        static std::vector<PyMethodDef> none_methods = {{nullptr, nullptr, 0, nullptr}};
        static std::vector<std::vector<PyGetSetDef>> keep;
        keep.push_back(std::move(extra));
        PyGetSetDef *getsets = option_getsets({hitbox_options}, {std::span<const PyGetSetDef>(keep.back())},
                                              view_option_key<V>, view_option_owner<V>);
        if (add_view_type<V>(module, "Hitbox",
                             "A box of a HitboxSet ($hbox). Angles, capsule radius and name are properties reading "
                             "back the value last assigned.",
                             none_methods.data(), getsets, Hitbox_repr) < 0)
            return -1;
    }
    {
        using V = PoseParamRef;
        std::vector<PyGetSetDef> extra = {
            STORED("min", "float | None\n--\n\nMinimum given to add_pose_parameter."),
            STORED("max", "float | None\n--\n\nMaximum given to add_pose_parameter."),
            STORED("loop_mode", "PoseLoopMode | None\n--\n\nPoseLoopMode of the last set_loop() call (None before)."),
            STORED("loop_range", "float | None\n--\n\nRange of the last set_loop() call (None unless "
                                 "PoseLoopMode.RANGE)."),
        };
        if (add_id_view<V>(module, "PoseParameter", "A pose parameter of a Model ($poseparameter).",
                           pose_parameter_methods, {}, std::move(extra)) < 0)
            return -1;
    }
    {
        using V = IkChainRef;
        std::vector<PyGetSetDef> extra = {
            {"end_bone", stored_bone_get<V>, nullptr, "Bone | None\n--\n\nEnd bone of the chain.",
             const_cast<char *>("end_bone")},
            STORED("axis", "tuple[Motion, float] | None\n--\n\n(motion, value) of the last set_axis() call (None "
                           "before)."),
        };
        if (add_id_view<V>(module, "IkChain",
                           "An IK chain of a Model ($ikchain). Settings are properties reading back the value last "
                           "assigned (None: the default).",
                           ik_chain_methods, {ik_chain_options}, std::move(extra)) < 0)
            return -1;
    }
    {
        using V = WeightListRef;
        if (add_id_view<V>(module, "WeightList", "A bone weight list of a Model ($weightlist).", weight_list_methods, {},
                           {}) < 0)
            return -1;
    }
    return 0;
}

} // namespace

smdl::AttachmentId attachment_arg(ModelObject *owner, PyObject *object, const char *name) {
    return id_arg<AttachmentRef>(owner, object, name,
                                 [](smdl::Model &m, const std::string &n) { return m.findAttachment(n); });
}

smdl::PoseParamId pose_param_arg(ModelObject *owner, PyObject *object, const char *name) {
    return id_arg<PoseParamRef>(owner, object, name,
                                [](smdl::Model &m, const std::string &n) { return m.findPoseParameter(n); });
}

smdl::IkChainId ik_chain_arg(ModelObject *owner, PyObject *object, const char *name) {
    return id_arg<IkChainRef>(owner, object, name,
                              [](smdl::Model &m, const std::string &n) { return m.findIkChain(n); });
}

smdl::WeightListId weight_list_arg(ModelObject *owner, PyObject *object, const char *name) {
    return id_arg<WeightListRef>(owner, object, name,
                                 [](smdl::Model &m, const std::string &n) { return m.findWeightList(n); });
}

PyObject *attachment_view(ModelObject *owner, smdl::AttachmentId id) { return make_view(owner, AttachmentRef{id}); }

PyObject *pose_param_view(ModelObject *owner, smdl::PoseParamId id) { return make_view(owner, PoseParamRef{id}); }

const Part part_rig = {
    {},
    model_methods,
    {},
    model_getsets,
    init,
};

} // namespace pysmdl
