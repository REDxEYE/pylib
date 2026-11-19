//
// pylib.studiomdl: the skeleton (smdl::detail::ModelSkeletonApi, smdl_skeleton.h) and per-bone settings.
//

#include "modules/studiomdl/bind.h"

namespace pysmdl {

using S = smdl::detail::ModelSkeletonApi;
using G = smdl::detail::ModelGlobalsApi;

smdl::BoneId bone_arg(ModelObject *owner, PyObject *object, const char *name, bool allow_none) {
    if (object == Py_None) {
        if (allow_none)
            return smdl::NoBone;
        raise(PyExc_TypeError, "%s must be a Bone or a bone name, not None", name);
    }
    if (PyUnicode_Check(object))
        return model_of(owner).findBone(Conv<std::string>::from(object, name));
    return view_arg<BoneRef>(owner, object, name).id;
}

PyObject *bone_view(ModelObject *owner, smdl::BoneId id) {
    if (id == smdl::NoBone)
        Py_RETURN_NONE;
    return make_view(owner, BoneRef{id});
}

namespace {

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

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

// ---- Bone

template<class>
struct bone_setter_traits;
template<class C, class R, class A>
struct bone_setter_traits<R (C::*)(smdl::BoneId, A)> {
    using arg = A;
};

template<auto Setter, class Stored = stored_t<typename bone_setter_traits<decltype(Setter)>::arg>>
inline constexpr std::string (*bone_annotation)() = &python_type<Stored>;

template<auto Setter, class Stored = stored_t<typename bone_setter_traits<decltype(Setter)>::arg>>
PyObject *bone_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    const smdl::BoneId id = view_of<BoneRef>(self).id;
    smdl::Model &model = model_of(owner_of<BoneRef>(self));
    (model.*Setter)(id, cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

PyObject *Bone_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        return Conv<std::string>::to(owner_of<BoneRef>(self)->bone_names->at(id));
    });
}

PyObject *Bone_index(PyObject *self, void *) {
    return PyLong_FromUnsignedLong(reinterpret_cast<ViewObject<BoneRef> *>(self)->value().id);
}

PyObject *Bone_parent(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        ModelObject *owner = owner_of<BoneRef>(self);
        return bone_view(owner, owner->bone_parents->at(id));
    });
}

PyObject *Bone_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const smdl::BoneId id = reinterpret_cast<ViewObject<BoneRef> *>(self)->value().id;
        const std::string &name = owner_of<BoneRef>(self)->bone_names->at(id);
        return PyUnicode_FromFormat("<Bone %u '%s'>", id, name.c_str());
    });
}

PyObject *Bone_define(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("define", args, kwargs, {"parent", "position", "rotation", "euler"}, 0, 2);
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        ModelObject *owner = owner_of<BoneRef>(self);
        const smdl::BoneId parent = a[0] ? bone_arg(owner, a[0], "parent", true) : smdl::NoBone;
        const auto position = a.get<smdl::Vec3>(1);
        rotation_args(a, 2, 3, [&](const smdl::Quat &q) { model_of(owner).defineBone(id, parent, position, q); },
                      [&](const smdl::Vec3 &e) { model_of(owner).defineBoneEuler(id, parent, position, e); });
        return none();
    });
}

PyObject *Bone_set_realign(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_realign", args, kwargs, {"position", "rotation", "euler"}, 0, 1);
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        ModelObject *owner = owner_of<BoneRef>(self);
        const auto position = a.get<smdl::Vec3>(0);
        rotation_args(a, 1, 2, [&](const smdl::Quat &q) { model_of(owner).setDefineBoneRealign(id, position, q); },
                      [&](const smdl::Vec3 &e) { model_of(owner).setDefineBoneRealignEuler(id, position, e); });
        return none();
    });
}

PyObject *Bone_force_realign(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("force_realign", args, kwargs, {"rotation", "euler"}, 0, 1);
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        ModelObject *owner = owner_of<BoneRef>(self);
        rotation_args(a, 0, 1, [&](const smdl::Quat &q) { model_of(owner).addForceRealign(id, q); },
                      [&](const smdl::Vec3 &e) { model_of(owner).addForceRealignEuler(id, e); });
        return none();
    });
}

PyObject *Bone_limit_rotation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("limit_rotation", args, kwargs, {"sequences"}, 0, 1);
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        const std::vector<std::string> sequences = a.has(0) ? string_list(a[0], "sequences")
                                                            : std::vector<std::string>();
        model_of(owner_of<BoneRef>(self)).setLimitRotation(id, std::span<const std::string>(sequences));
        return none();
    });
}

PyObject *Bone_add_controller(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_controller", args, kwargs, {"input", "type", "start", "end"}, 4, 4);
        const smdl::BoneId id = view_of<BoneRef>(self).id;
        const uint32_t index = model_of(owner_of<BoneRef>(self))
            .addBoneController(a.get<uint32_t>(0), id, a.get<smdl_bone_controller_type>(1), a.get<float>(2),
                               a.get<float>(3));
        return PyLong_FromUnsignedLong(index);
    });
}

const OptionDef bone_options[] = {
    {"surface_prop", "$jointsurfaceprop of the bone.", &bone_setter<&G::setBoneSurfaceProp>,
     bone_annotation<&G::setBoneSurfaceProp>},
    {"contents", "$jointcontents of the bone.", &bone_setter<&G::setBoneContents, Flags<ContentsFlags>>,
     &python_type<Flags<ContentsFlags>>},
    {"define_lock", "Lock of the $definebone pose.", &bone_setter<&S::setDefineBoneLock>,
     bone_annotation<&S::setDefineBoneLock>},
    {"always_collapse", "$alwayscollapse.", &bone_setter<&S::setAlwaysCollapse>,
     bone_annotation<&S::setAlwaysCollapse>},
    {"screen_align", "$screenalign.", &bone_setter<&S::setScreenAlign>, bone_annotation<&S::setScreenAlign>},
    {"world_align", "$worldalign.", &bone_setter<&S::setWorldAlign>, bone_annotation<&S::setWorldAlign>},
    {"bone_merge", "$bonemerge.", &bone_setter<&S::setBoneMerge>, bone_annotation<&S::setBoneMerge>},
    {"always_setup", "$bonealwayssetup.", &bone_setter<&S::setBoneAlwaysSetup>,
     bone_annotation<&S::setBoneAlwaysSetup>},
    {"save_frame", "$bonesaveframe.", &bone_setter<&S::setBoneSaveFrame, Flags<BoneSaveFlags>>,
     &python_type<Flags<BoneSaveFlags>>},
};

const PyGetSetDef bone_getsets[] = {
    {"name", Bone_name, nullptr, "str\n--\n\nName of the bone.", nullptr},
    {"index", Bone_index, nullptr, "int\n--\n\nIndex (id) of the bone in the model.", nullptr},
    {"parent", Bone_parent, nullptr, "Bone | None\n--\n\nParent Bone, or None for a root.", nullptr},
};

PyMethodDef bone_methods[] = {
    {"define", PYSMDL_METHOD(Bone_define), PYSMDL_KW,
     "define(parent: BoneLike | None = None, position: Vec3 = (0, 0, 0), *, rotation: Quat | None = None, "
     "euler: Vec3 | None = None) -> None\n--\n\n"
     "$definebone: the bone's pose relative to parent (a Bone defined before this one, or None). rotation: "
     "quaternion (x, y, z, w), or euler: radians (x, y, z); identity by default."},
    {"set_realign", PYSMDL_METHOD(Bone_set_realign), PYSMDL_KW,
     "set_realign(position: Vec3 = (0, 0, 0), *, rotation: Quat | None = None, euler: Vec3 | None = None) -> None"
     "\n--\n\n$definebone realign pose."},
    {"force_realign", PYSMDL_METHOD(Bone_force_realign), PYSMDL_KW,
     "force_realign(rotation: Quat | None = None, *, euler: Vec3 | None = None) -> None\n--\n\n$forcerealign."},
    {"limit_rotation", PYSMDL_METHOD(Bone_limit_rotation), PYSMDL_KW,
     "limit_rotation(sequences: StrSequence = ()) -> None\n--\n\n"
     "$limitrotation, optionally only in the named sequences."},
    {"add_controller", PYSMDL_METHOD(Bone_add_controller), PYSMDL_KW,
     "add_controller(input: int, type: BoneControllerType, start: float, end: float) -> int\n--\n\n"
     "$controller on this bone. "
     "Returns the controller index."},
    {nullptr, nullptr, 0, nullptr},
};

// ---- Model methods

PyObject *add_bone(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_bone", args, kwargs, {"name", "parent", "position", "rotation", "euler"}, 1, 3);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const smdl::BoneId parent = a[1] ? bone_arg(model, a[1], "parent", true) : smdl::NoBone;
        const auto position = a.get<smdl::Vec3>(2);
        smdl::BoneId id = smdl::NoBone;
        rotation_args(a, 3, 4, [&](const smdl::Quat &q) { id = model_of(model).addBone(name, parent, position, q); },
                      [&](const smdl::Vec3 &e) { id = model_of(model).addBoneEuler(name, parent, position, e); });
        if (model->bone_names->size() <= id) {
            model->bone_names->resize(id + 1);
            model->bone_parents->resize(id + 1, smdl::NoBone);
        }
        (*model->bone_names)[id] = name;
        (*model->bone_parents)[id] = parent;
        return make_view(model, BoneRef{id});
    });
}

PyObject *find_bone(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_bone", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, BoneRef{model_of(model).findBone(a.get<std::string>(0))});
    });
}

PyObject *add_bone_controller(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_bone_controller", args, kwargs, {"input", "bone", "type", "start", "end"}, 5, 5);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[1], "bone");
        return PyLong_FromUnsignedLong(model_of(model).addBoneController(
            a.get<uint32_t>(0), bone, a.get<smdl_bone_controller_type>(2), a.get<float>(3), a.get<float>(4)));
    });
}

PyObject *Model_bones(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        const uint32_t count = model_of(model).boneCount();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(model, BoneRef{i}));
        return tuple.release();
    });
}

PyObject *Model_get_root_bone(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        PyObject *id = PyDict_GetItemString(model->options, "root_bone");
        if (!id)
            Py_RETURN_NONE;
        return bone_view(model, (smdl::BoneId) PyLong_AsUnsignedLong(id));
    });
}

int Model_set_root_bone(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'root_bone'");
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, value, "root_bone", true);
        model_of(model).setRootBone(bone);
        // Stored as the id: a Bone in the options dict would keep the model alive (a cycle).
        if (bone == smdl::NoBone) {
            if (PyDict_GetItemString(model->options, "root_bone") && PyDict_DelItemString(model->options, "root_bone") < 0)
                throw PyError{};
        } else {
            Ref id = Ref::check(PyLong_FromUnsignedLong(bone));
            if (PyDict_SetItemString(model->options, "root_bone", id.get()) < 0)
                throw PyError{};
        }
        return 0;
    });
}

#define OPT(name, setter, doc) OptionDef{name, doc, &model_setter<&S::setter>, setter_annotation<&S::setter>}
const OptionDef model_options[] = {
    OPT("unlock_define_bones", setUnlockDefineBones, "$unlockdefinebones."),
    OPT("lock_bone_lengths", setLockBoneLengths, "$lockbonelengths."),
    OPT("realign_bones", setRealignBones, "$realignbones."),
    OPT("collapse_bones", setCollapseBones, "$collapsebones."),
    OPT("collapse_bones_aggressive", setCollapseBonesAggressive, "$collapsebonesaggressive."),
};
#undef OPT

const PyGetSetDef model_getsets[] = {
    {"bones", Model_bones, nullptr, "tuple[Bone, ...]\n--\n\nBones of the model in id order.", nullptr},
    {"root_bone", Model_get_root_bone, Model_set_root_bone, "Bone | None\n--\n\n$root: a Bone, a bone name, or None.",
     nullptr},
};

const PyMethodDef model_methods[] = {
    {"add_bone", PYSMDL_METHOD(add_bone), PYSMDL_KW,
     "add_bone(name: str, parent: BoneLike | None = None, position: Vec3 = (0, 0, 0), *, rotation: Quat | None = "
     "None, euler: Vec3 | None = None) -> Bone\n--\n\n"
     "Add a bone and return it. parent: Bone, bone name or None for a root. Rest pose relative to the parent: "
     "rotation as a quaternion (x, y, z, w), or euler as SMD angles in radians (x, y, z); identity by default."},
    {"find_bone", PYSMDL_METHOD(find_bone), PYSMDL_KW,
     "find_bone(name: str) -> Bone\n--\n\nThe Bone with this name; raises NotFoundError."},
    {"add_bone_controller", PYSMDL_METHOD(add_bone_controller), PYSMDL_KW,
     "add_bone_controller(input: int, bone: BoneLike, type: BoneControllerType, start: float, end: float) -> int"
     "\n--\n\n$controller. Returns the "
     "controller index."},
};

int init(PyObject *module) {
    if (register_enum<smdl_define_bone_lock>(module, "DefineBoneLock", {{"DEFAULT", SMDL_DEFINE_BONE_LOCK_DEFAULT},
                                                                         {"LOCKED", SMDL_DEFINE_BONE_LOCKED},
                                                                         {"UNLOCKED", SMDL_DEFINE_BONE_UNLOCKED}}) < 0)
        return -1;
    if (register_enum<smdl_bone_controller_type>(module, "BoneControllerType",
                                                 {{"X", SMDL_BONE_CONTROLLER_X}, {"Y", SMDL_BONE_CONTROLLER_Y},
                                                  {"Z", SMDL_BONE_CONTROLLER_Z}, {"XR", SMDL_BONE_CONTROLLER_XR},
                                                  {"YR", SMDL_BONE_CONTROLLER_YR}, {"ZR", SMDL_BONE_CONTROLLER_ZR}}) < 0)
        return -1;
    if (PyModule_AddIntConstant(module, "BONE_CONTROLLER_MOUTH", SMDL_BONE_CONTROLLER_MOUTH) < 0)
        return -1;
    if (register_enum<smdl_screen_align_mode>(module, "ScreenAlign", {{"NONE", SMDL_SCREEN_ALIGN_NONE},
                                                                      {"SPHERE", SMDL_SCREEN_ALIGN_SPHERE},
                                                                      {"CYLINDER", SMDL_SCREEN_ALIGN_CYLINDER}}) < 0)
        return -1;
    if (register_enum<BoneSaveFlags>(module, "BoneSaveFrame", {{"POSITION", SMDL_BONE_SAVE_POSITION},
                                                               {"ROTATION", SMDL_BONE_SAVE_ROTATION},
                                                               {"ROTATION64", SMDL_BONE_SAVE_ROTATION64}}, true) < 0)
        return -1;
    PyGetSetDef *getsets = option_getsets({bone_options}, {bone_getsets}, view_option_key<BoneRef>,
                                          view_option_owner<BoneRef>);
    return add_view_type<BoneRef>(module, "Bone",
                                  "A bone of a Model (from Model.add_bone / find_bone / bones). Settings are "
                                  "properties reading back the value last assigned (None: studiomdl's default).",
                                  bone_methods, getsets, Bone_repr);
}

} // namespace

const Part part_skeleton = {
    {},
    model_methods,
    model_options,
    model_getsets,
    init,
};

} // namespace pysmdl
