//
// pylib.studiomdl: animations, IK rules and sequences (smdl_animation.h, smdl_sequence.h).
//
// Animation, IkRule and Sequence are views of the C++ handles (smdl::Animation, smdl::IkRule, smdl::Sequence). The C++
// API has no getters for most settings, so properties read back the value last assigned through Python (see bind.h);
// object-valued settings (a bone, an animation, a pose parameter) are kept as ids/names in the model's options dict and
// turned back into views when read.
//

#include "modules/studiomdl/rig.h"

#include <optional>

namespace pysmdl {

namespace {

using A = smdl::Animation;
using I = smdl::IkRule;
using S = smdl::Sequence;

struct SequenceFlags {};
struct LayerFlags {};

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

PyObject *to_py(std::string_view text) {
    return PyUnicode_DecodeUTF8(text.data(), (Py_ssize_t) text.size(), "replace");
}

// ---- arguments

smdl::Animation animation_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findAnimation(Conv<std::string>::from(object, name));
    return view_arg<A>(owner, object, name);
}

smdl::Sequence sequence_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findSequence(Conv<std::string>::from(object, name));
    return view_arg<S>(owner, object, name);
}

// Four frames (start, peak, tail, end); None stands for ".." (unset, -1).
std::array<int, 4> frame_range(PyObject *object, const char *name) {
    if (PyUnicode_Check(object) || !PySequence_Check(object))
        raise(PyExc_TypeError, "%s must be a sequence of 4 frames (start, peak, tail, end)", name);
    if (PySequence_Size(object) != 4) {
        if (PyErr_Occurred())
            throw PyError{};
        raise(PyExc_ValueError, "%s must have 4 values (start, peak, tail, end)", name);
    }
    std::array<int, 4> frames{};
    for (int i = 0; i < 4; ++i) {
        Ref item = Ref::check(PySequence_GetItem(object, i));
        frames[i] = item.get() == Py_None ? -1 : Conv<int>::from(item.get(), name);
    }
    return frames;
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

#define VIEW_FN(V, fn, pyname, required, positional, ...)                                                        \
    PyObject *fn(PyObject *self, PyObject *args, PyObject *kwargs) {                                            \
        return guarded([&]() -> PyObject * {                                                                    \
            Args a(pyname, args, kwargs, {__VA_ARGS__}, required, positional);                                  \
            V &object = view_of<V>(self);                                                                       \
            ModelObject *owner = owner_of<V>(self);                                                             \
            (void) object;                                                                                      \
            (void) owner;

#define FN_NONE                                                                                                  \
            return none();                                                                                       \
        });                                                                                                      \
    }

#define FN_VALUE                                                                                                 \
        });                                                                                                      \
    }

// ---------------------------------------------------------------------------------------------------------------------
// Animation

PyObject *Animation_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return to_py(view_of<A>(self).name()); });
}

PyObject *Animation_get_fps(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return Conv<float>::to(view_of<A>(self).fps()); });
}

int Animation_set_fps(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'fps'");
        const float fps = Conv<float>::from(value, "fps");
        view_of<A>(self).setFps(fps);
        return 0;
    });
}

PyObject *Animation_frame_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<A>(self).frameCount()); });
}

PyObject *Animation_command_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<A>(self).commandCount()); });
}

PyObject *Animation_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = reinterpret_cast<ViewObject<A> *>(self)->value().name();
        return PyUnicode_FromFormat("<Animation '%s'>", std::string(name).c_str());
    });
}

const OptionDef animation_options[] = {
    {"origin", "tuple[float, float, float] | None\n--\n\n$animation origin (x, y, z).",
     &view_setter<A, &A::setOrigin>},
    {"rotate", "float | None\n--\n\n$animation rotate: yaw in degrees (the QC option adds the 90 degrees of the "
               "default up axis).", &view_setter<A, &A::setRotate>},
    {"scale", "float | None\n--\n\n$animation scale.", &view_setter<A, &A::setScale>},
    {"loop", "bool | None\n--\n\n$animation loop.", &view_setter<A, &A::setLoop>},
    {"no_force_loop", "bool | None\n--\n\n$animation noforceloop.", &view_setter<A, &A::setNoForceLoop>},
    {"start_loop", "int | None\n--\n\n$animation startloop: the frame the loop restarts at (implies loop). Excludes "
                   "percent_start_loop.", &view_setter<A, &A::setStartLoop>},
    {"percent_start_loop", "float | None\n--\n\n$animation percentstartloop: percent (implies loop). Excludes "
                           "start_loop.", &view_setter<A, &A::setPercentStartLoop>},
    {"fudge_loop", "bool | None\n--\n\n$animation fudgeloop.", &view_setter<A, &A::setFudgeLoop>},
    {"snap", "bool | None\n--\n\n$animation snap.", &view_setter<A, &A::setSnap>},
    {"post", "bool | None\n--\n\n$animation post.", &view_setter<A, &A::setPost>},
    {"auto_ik", "bool | None\n--\n\n$animation autoik (True) / noautoik (False).", &view_setter<A, &A::setAutoIk>},
    {"motion_rollback", "float | None\n--\n\n$animation motionrollback.", &view_setter<A, &A::setMotionRollback>},
    {"no_anim_block", "bool | None\n--\n\n$animation noanimblock.", &view_setter<A, &A::setNoAnimBlock>},
    {"no_anim_block_stall", "bool | None\n--\n\n$animation noanimblockstall.",
     &view_setter<A, &A::setNoAnimBlockStall>},
    {"no_stall_frames", "float | None\n--\n\n$animation nostallframes.", &view_setter<A, &A::setNoStallFrames>},
    {"frame_start", "int | None\n--\n\n$animation framestart: first frame, up to the last frame.",
     &view_setter<A, &A::setFrameStart>},
    {"block_name", "str | None\n--\n\n$animation blockname: the name of the animation's block of frames (default "
                   "\"BindPose\").", &view_setter<A, &A::setBlockName>},
    {"motion_extraction", "Motion | None\n--\n\nMotion extraction: a combination of Motion flags (the QC tokens X Y Z "
                          "XR ... LQ).", &view_setter<A, &A::setMotionExtraction, Flags<MotionFlags>>},
};

const PyGetSetDef animation_getsets[] = {
    {"name", Animation_name, nullptr, "str\n--\n\nName of the animation.", nullptr},
    {"fps", Animation_get_fps, Animation_set_fps, "float\n--\n\nFrames per second.", nullptr},
    {"frame_count", Animation_frame_count, nullptr, "int\n--\n\nNumber of frames.", nullptr},
    {"command_count", Animation_command_count, nullptr, "int\n--\n\nNumber of commands added so far.", nullptr},
};

// frames of one bone: positions with rotations (quaternions) or euler (radians)
VIEW_FN(A, Animation_set_bone_frames, "set_bone_frames", 1, 3, "bone", "positions", "rotations", "euler")
    if (a.has(2) && a.has(3))
        raise(PyExc_TypeError, "pass rotations= (quaternions) or euler= (radians), not both");
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    const uint32_t frames = object.frameCount();
    std::optional<Array<float>> positions;
    std::optional<Array<float>> rotations;
    const bool euler = a.has(3);
    if (a.has(1))
        positions.emplace(a[1], 3, "positions");
    if (a.has(2))
        rotations.emplace(a[2], 4, "rotations");
    if (euler)
        rotations.emplace(a[3], 3, "euler");
    const char *rotation_name = euler ? "euler" : "rotations";
    if (positions && positions->rows() != frames)
        raise(PyExc_ValueError, "positions must have %u rows (one per frame), not %zu", frames, positions->rows());
    if (rotations && rotations->rows() != frames)
        raise(PyExc_ValueError, "%s must have %u rows (one per frame), not %zu", rotation_name, frames,
              rotations->rows());
    const float *position_data = positions ? positions->data() : nullptr;
    const size_t position_rows = positions ? positions->rows() : 0;
    const size_t position_stride = positions ? positions->stride() : 0;
    const float *rotation_data = rotations ? rotations->data() : nullptr;
    const size_t rotation_rows = rotations ? rotations->rows() : 0;
    const size_t rotation_stride = rotations ? rotations->stride() : 0;
    if (euler)
        object.setBoneFramesEuler(bone, position_data, position_rows, position_stride, rotation_data, rotation_rows,
                                  rotation_stride);
    else
        object.setBoneFrames(bone, position_data, position_rows, position_stride, rotation_data, rotation_rows,
                             rotation_stride);
FN_NONE

VIEW_FN(A, Animation_set_rest_pose, "set_rest_pose", 1, 2, "bone", "position", "rotation", "euler")
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    const auto position = a.get<smdl::Vec3>(1);
    rotation_args(a, 2, 3, [&](const smdl::Quat &q) { object.setRestPose(bone, position, q); },
                  [&](const smdl::Vec3 &e) { object.setRestPoseEuler(bone, position, e); });
FN_NONE

VIEW_FN(A, Animation_set_angles, "set_angles", 3, 3, "x", "y", "z")
    object.setAngles(a.get<float>(0), a.get<float>(1), a.get<float>(2));
FN_NONE

VIEW_FN(A, Animation_set_frame_range, "set_frame_range", 2, 2, "first_frame", "last_frame")
    object.setFrameRange(a.get<int>(0), a.get<int>(1));
FN_NONE

VIEW_FN(A, Animation_add_fixup_loop, "add_fixup_loop", 2, 2, "first_frame", "last_frame")
    object.addFixupLoop(a.get<int>(0), a.get<int>(1));
FN_NONE

VIEW_FN(A, Animation_add_weight_list, "add_weight_list", 1, 1, "weight_list")
    object.addWeightList(weight_list_arg(owner, a[0], "weight_list"));
FN_NONE

VIEW_FN(A, Animation_add_subtract, "add_subtract", 2, 3, "reference", "frame", "post")
    object.addSubtract(animation_arg(owner, a[0], "reference"), a.get<int>(1), a.has(2) ? a.get<bool>(2) : true);
FN_NONE

VIEW_FN(A, Animation_add_align, "add_align", 4, 4, "reference", "motion", "src_frame", "dest_frame")
    object.addAlign(animation_arg(owner, a[0], "reference"), a.get<Flags<MotionFlags>>(1).value, a.get<int>(2),
                    a.get<int>(3));
FN_NONE

VIEW_FN(A, Animation_add_align_to, "add_align_to", 1, 1, "reference")
    object.addAlignTo(animation_arg(owner, a[0], "reference"));
FN_NONE

VIEW_FN(A, Animation_add_align_bone, "add_align_bone", 5, 5, "bone", "reference", "motion", "src_frame",
        "dest_frame")
    object.addAlignBone(bone_arg(owner, a[0], "bone"), animation_arg(owner, a[1], "reference"),
                        a.get<Flags<MotionFlags>>(2).value, a.get<int>(3), a.get<int>(4));
FN_NONE

VIEW_FN(A, Animation_add_align_bone_to, "add_align_bone_to", 2, 2, "bone", "reference")
    object.addAlignBoneTo(bone_arg(owner, a[0], "bone"), animation_arg(owner, a[1], "reference"));
FN_NONE

VIEW_FN(A, Animation_add_match, "add_match", 1, 1, "reference")
    object.addMatch(animation_arg(owner, a[0], "reference"));
FN_NONE

VIEW_FN(A, Animation_add_match_blend, "add_match_blend", 5, 5, "reference", "src_frame", "dest_frame", "dest_pre",
        "dest_post")
    object.addMatchBlend(animation_arg(owner, a[0], "reference"), a.get<int>(1), a.get<int>(2), a.get<int>(3),
                         a.get<int>(4));
FN_NONE

VIEW_FN(A, Animation_add_world_space_blend, "add_world_space_blend", 1, 1, "reference")
    object.addWorldSpaceBlend(animation_arg(owner, a[0], "reference"));
FN_NONE

VIEW_FN(A, Animation_add_world_space_blend_loop, "add_world_space_blend_loop", 2, 2, "reference", "start_frame")
    object.addWorldSpaceBlendLoop(animation_arg(owner, a[0], "reference"), a.get<int>(1));
FN_NONE

VIEW_FN(A, Animation_add_rotate_to, "add_rotate_to", 1, 1, "degrees")
    object.addRotateTo(a.get<float>(0));
FN_NONE

PyObject *new_ik_rule(ModelObject *owner, const smdl::IkRule &rule, smdl::IkChainId chain, smdl::IkRuleType type) {
    Ref view(make_view(owner, rule));
    store_value<I>(view.get(), "type", type);
    store_value<I>(view.get(), "chain", (uint32_t) chain);
    return view.release();
}

VIEW_FN(A, Animation_add_ik_rule, "add_ik_rule", 2, 2, "chain", "type")
    const smdl::IkChainId chain = ik_chain_arg(owner, a[0], "chain");
    const auto type = a.get<smdl::IkRuleType>(1);
    return new_ik_rule(owner, object.addIkRule(chain, type), chain, type);
FN_VALUE

VIEW_FN(A, Animation_add_ik_fixup, "add_ik_fixup", 2, 2, "chain", "type")
    const smdl::IkChainId chain = ik_chain_arg(owner, a[0], "chain");
    const auto type = a.get<smdl::IkRuleType>(1);
    return new_ik_rule(owner, object.addIkFixup(chain, type), chain, type);
FN_VALUE

VIEW_FN(A, Animation_add_walk_frame, "add_walk_frame", 2, 2, "end_frame", "motion")
    object.addWalkFrame(a.get<int>(0), a.get<Flags<MotionFlags>>(1).value);
FN_NONE

VIEW_FN(A, Animation_add_walk_align, "add_walk_align", 5, 5, "end_frame", "reference", "motion", "ref_frame",
        "src_frame")
    object.addWalkAlign(a.get<int>(0), animation_arg(owner, a[1], "reference"), a.get<Flags<MotionFlags>>(2).value,
                        a.get<int>(3), a.get<int>(4));
FN_NONE

VIEW_FN(A, Animation_add_walk_align_to, "add_walk_align_to", 3, 3, "end_frame", "reference", "motion")
    object.addWalkAlignTo(a.get<int>(0), animation_arg(owner, a[1], "reference"), a.get<Flags<MotionFlags>>(2).value);
FN_NONE

VIEW_FN(A, Animation_add_derivative, "add_derivative", 1, 1, "scale")
    object.addDerivative(a.get<float>(0));
FN_NONE

VIEW_FN(A, Animation_add_no_animation, "add_no_animation", 0, 1, "keep_duration")
    if (a.has(0) && a.get<bool>(0))
        object.addNoAnimationKeepDuration();
    else
        object.addNoAnimation();
FN_NONE

VIEW_FN(A, Animation_add_linear_delta, "add_linear_delta", 0, 0)
    object.addLinearDelta();
FN_NONE

VIEW_FN(A, Animation_add_spline_delta, "add_spline_delta", 0, 0)
    object.addSplineDelta();
FN_NONE

VIEW_FN(A, Animation_add_compress, "add_compress", 1, 1, "frames")
    object.addCompress(a.get<int>(0));
FN_NONE

VIEW_FN(A, Animation_add_num_frames, "add_num_frames", 1, 1, "frames")
    object.addNumFrames(a.get<int>(0));
FN_NONE

VIEW_FN(A, Animation_add_counter_rotate, "add_counter_rotate", 1, 2, "bone", "angles")
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    if (a.has(1)) {
        const auto angles = a.get<smdl::Vec3>(1);
        object.addCounterRotateTo(bone, angles[0], angles[1], angles[2]);
    } else {
        object.addCounterRotate(bone);
    }
FN_NONE

VIEW_FN(A, Animation_add_local_hierarchy, "add_local_hierarchy", 1, 2, "bone", "parent", "range")
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    const smdl::BoneId parent = a[1] ? bone_arg(owner, a[1], "parent", true) : smdl::NoBone;
    std::array<int, 4> range{-1, -1, -1, -1};
    if (a.has(2))
        range = frame_range(a[2], "range");
    object.addLocalHierarchy(bone, parent, range[0], range[1], range[2], range[3]);
FN_NONE

VIEW_FN(A, Animation_add_force_bone_pos_rot, "add_force_bone_pos_rot", 1, 4, "bone", "position", "angles", "local")
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    std::optional<smdl::Vec3> position;
    std::optional<smdl::Vec3> angles;
    if (a.has(1))
        position = a.get<smdl::Vec3>(1);
    if (a.has(2))
        angles = a.get<smdl::Vec3>(2);
    object.addForceBonePosRot(bone, position, angles, a.has(3) && a.get<bool>(3));
FN_NONE

VIEW_FN(A, Animation_add_bone_driver, "add_bone_driver", 3, 3, "bone", "axis", "value", "range")
    const smdl::BoneId bone = bone_arg(owner, a[0], "bone");
    const int axis = a.get<int>(1);
    const float value = a.get<float>(2);
    if (a.has(3)) {
        const std::array<int, 4> range = frame_range(a[3], "range");
        object.addBoneDriver(bone, axis, value, range[0], range[1], range[2], range[3]);
    } else {
        object.addBoneDriver(bone, axis, value);
    }
FN_NONE

VIEW_FN(A, Animation_add_reverse, "add_reverse", 0, 0)
    object.addReverse();
FN_NONE

VIEW_FN(A, Animation_add_append_animation, "add_append_animation", 1, 1, "reference")
    object.addAppendAnimation(animation_arg(owner, a[0], "reference"));
FN_NONE

VIEW_FN(A, Animation_move_command, "move_command", 2, 2, "from_index", "to_index")
    object.moveCommand(a.get<uint32_t>(0), a.get<uint32_t>(1));
FN_NONE

PyMethodDef animation_methods[] = {
    {"set_bone_frames", PYSMDL_METHOD(Animation_set_bone_frames), PYSMDL_KW,
     "set_bone_frames(bone: BoneLike, positions: FloatArray | None = None, rotations: FloatArray | None = None, *, "
     "euler: FloatArray | None = None) -> None\n--\n\nThe pose of bone (Bone or name) in "
     "every frame; calling it again for the bone replaces its data. positions: float32 array of shape "
     "(frame_count, 3) (units, before $scale); rotations: (frame_count, 4) quaternions (x, y, z, w), or euler: "
     "(frame_count, 3) SMD angles in radians. float32 numpy arrays are used without copying. A part left out keeps "
     "the rest pose. Raises ValueError when the row count is not frame_count."},
    {"set_rest_pose", PYSMDL_METHOD(Animation_set_rest_pose), PYSMDL_KW,
     "set_rest_pose(bone: BoneLike, position: Vec3 = (0, 0, 0), *, rotation: Quat | None = None, "
     "euler: Vec3 | None = None) -> None\n--\n\nThe rest pose of bone for this "
     "animation, used for frames of a bone without data instead of the skeleton's pose. rotation: quaternion (x, y, "
     "z, w), or euler: radians; identity by default."},
    {"set_angles", PYSMDL_METHOD(Animation_set_angles), PYSMDL_KW,
     "set_angles(x: float, y: float, z: float) -> None\n--\n\n$animation angles, in degrees (z gets the same +90 as "
     "rotate)."},
    {"set_frame_range", PYSMDL_METHOD(Animation_set_frame_range), PYSMDL_KW,
     "set_frame_range(first_frame: int, last_frame: int) -> None\n--\n\n$animation frame: inclusive, clamped to the "
     "animation's frames."},
    {"add_fixup_loop", PYSMDL_METHOD(Animation_add_fixup_loop), PYSMDL_KW,
     "add_fixup_loop(first_frame: int, last_frame: int) -> None\n--\n\nfixuploop command."},
    {"add_weight_list", PYSMDL_METHOD(Animation_add_weight_list), PYSMDL_KW,
     "add_weight_list(weight_list: WeightList | str | int) -> None\n--\n\nweightlist command: the bone weights from "
     "here on (WeightList, name or id)."},
    {"add_subtract", PYSMDL_METHOD(Animation_add_subtract), PYSMDL_KW,
     "add_subtract(reference: Animation | str, frame: int, post: bool = True) -> None\n--\n\nsubtract (post) / "
     "presubtract: subtract frame `frame` of reference (an Animation or name, a delta animation created earlier)."},
    {"add_align", PYSMDL_METHOD(Animation_add_align), PYSMDL_KW,
     "add_align(reference: Animation | str, motion: Motion, src_frame: int, dest_frame: int) -> None\n--\n\n"
     "align command: aligns the root motion (Motion flags, e.g. Motion.X | Motion.Y) of frame dest_frame with frame "
     "src_frame of reference."},
    {"add_align_to", PYSMDL_METHOD(Animation_add_align_to), PYSMDL_KW,
     "add_align_to(reference: Animation | str) -> None\n--\n\nalignto command: X and Y at frame 0 of both."},
    {"add_align_bone", PYSMDL_METHOD(Animation_add_align_bone), PYSMDL_KW,
     "add_align_bone(bone: BoneLike, reference: Animation | str, motion: Motion, src_frame: int, dest_frame: int) -> "
     "None\n--\n\nalignbone command: align for a bone other than the root."},
    {"add_align_bone_to", PYSMDL_METHOD(Animation_add_align_bone_to), PYSMDL_KW,
     "add_align_bone_to(bone: BoneLike, reference: Animation | str) -> None\n--\n\nalignboneto command."},
    {"add_match", PYSMDL_METHOD(Animation_add_match), PYSMDL_KW,
     "add_match(reference: Animation | str) -> None\n--\n\nmatch command."},
    {"add_match_blend", PYSMDL_METHOD(Animation_add_match_blend), PYSMDL_KW,
     "add_match_blend(reference: Animation | str, src_frame: int, dest_frame: int, dest_pre: int, dest_post: int) -> "
     "None\n--\n\nmatchblend command: match frame dest_frame to frame src_frame of reference, blending in from "
     "dest_pre to dest_post."},
    {"add_world_space_blend", PYSMDL_METHOD(Animation_add_world_space_blend), PYSMDL_KW,
     "add_world_space_blend(reference: Animation | str) -> None\n--\n\nworldspaceblend command."},
    {"add_world_space_blend_loop", PYSMDL_METHOD(Animation_add_world_space_blend_loop), PYSMDL_KW,
     "add_world_space_blend_loop(reference: Animation | str, start_frame: int) -> None\n--\n\n"
     "worldspaceblendloop command."},
    {"add_rotate_to", PYSMDL_METHOD(Animation_add_rotate_to), PYSMDL_KW,
     "add_rotate_to(degrees: float) -> None\n--\n\nrotateto command."},
    {"add_ik_rule", PYSMDL_METHOD(Animation_add_ik_rule), PYSMDL_KW,
     "add_ik_rule(chain: IkChain | str | int, type: IkRuleType) -> IkRule\n--\n\nikrule command on chain (IkChain, "
     "name or id), type: IkRuleType. Returns the IkRule to set its parameters on."},
    {"add_ik_fixup", PYSMDL_METHOD(Animation_add_ik_fixup), PYSMDL_KW,
     "add_ik_fixup(chain: IkChain | str | int, type: IkRuleType) -> IkRule\n--\n\nikfixup command; like "
     "add_ik_rule."},
    {"add_walk_frame", PYSMDL_METHOD(Animation_add_walk_frame), PYSMDL_KW,
     "add_walk_frame(end_frame: int, motion: Motion) -> None\n--\n\nwalkframe command: motion (Motion flags) over "
     "the first end_frame frames."},
    {"add_walk_align", PYSMDL_METHOD(Animation_add_walk_align), PYSMDL_KW,
     "add_walk_align(end_frame: int, reference: Animation | str, motion: Motion, ref_frame: int, src_frame: int) -> "
     "None\n--\n\nwalkalign command."},
    {"add_walk_align_to", PYSMDL_METHOD(Animation_add_walk_align_to), PYSMDL_KW,
     "add_walk_align_to(end_frame: int, reference: Animation | str, motion: Motion) -> None\n--\n\n"
     "walkalignto command."},
    {"add_derivative", PYSMDL_METHOD(Animation_add_derivative), PYSMDL_KW,
     "add_derivative(scale: float) -> None\n--\n\nderivative command."},
    {"add_no_animation", PYSMDL_METHOD(Animation_add_no_animation), PYSMDL_KW,
     "add_no_animation(keep_duration: bool = False) -> None\n--\n\nnoanimation (noanim_keepduration with "
     "keep_duration=True) command."},
    {"add_linear_delta", PYSMDL_METHOD(Animation_add_linear_delta), PYSMDL_KW,
     "add_linear_delta() -> None\n--\n\nlineardelta command."},
    {"add_spline_delta", PYSMDL_METHOD(Animation_add_spline_delta), PYSMDL_KW,
     "add_spline_delta() -> None\n--\n\nsplinedelta command."},
    {"add_compress", PYSMDL_METHOD(Animation_add_compress), PYSMDL_KW,
     "add_compress(frames: int) -> None\n--\n\ncompress command."},
    {"add_num_frames", PYSMDL_METHOD(Animation_add_num_frames), PYSMDL_KW,
     "add_num_frames(frames: int) -> None\n--\n\nnumframes command."},
    {"add_counter_rotate", PYSMDL_METHOD(Animation_add_counter_rotate), PYSMDL_KW,
     "add_counter_rotate(bone: BoneLike, angles: Vec3 | None = None) -> None\n--\n\ncounterrotate command; with "
     "angles (degrees pitch, yaw, roll) counterrotateto."},
    {"add_local_hierarchy", PYSMDL_METHOD(Animation_add_local_hierarchy), PYSMDL_KW,
     "add_local_hierarchy(bone: BoneLike, parent: BoneLike | None = None, *, "
     "range: tuple[int | None, int | None, int | None, int | None] | None = None) -> None\n--\n\n"
     "localhierarchy command. parent: Bone, name or None for none. range: (start, peak, tail, end) frames, None "
     "entries are unset (\"..\"); no range when omitted."},
    {"add_force_bone_pos_rot", PYSMDL_METHOD(Animation_add_force_bone_pos_rot), PYSMDL_KW,
     "add_force_bone_pos_rot(bone: BoneLike, position: Vec3 | None = None, angles: Vec3 | None = None, "
     "local: bool = False) -> None\n--\n\nforceboneposrot command. position "
     "in units, angles in degrees; None leaves one out; local: the rotation is local."},
    {"add_bone_driver", PYSMDL_METHOD(Animation_add_bone_driver), PYSMDL_KW,
     "add_bone_driver(bone: BoneLike, axis: int, value: float, *, "
     "range: tuple[int | None, int | None, int | None, int | None] | None = None) -> None\n--\n\n"
     "bonedriver command. axis 0..2. Without range the value "
     "applies to every frame, else it is blended in over range = (start, peak, tail, end)."},
    {"add_reverse", PYSMDL_METHOD(Animation_add_reverse), PYSMDL_KW,
     "add_reverse() -> None\n--\n\nreverse command."},
    {"add_append_animation", PYSMDL_METHOD(Animation_add_append_animation), PYSMDL_KW,
     "add_append_animation(reference: Animation | str) -> None\n--\n\nappendanim command."},
    {"move_command", PYSMDL_METHOD(Animation_move_command), PYSMDL_KW,
     "move_command(from_index: int, to_index: int) -> None\n--\n\n$prepend: moves the command at from_index to "
     "to_index."},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------------------------------
// IkRule

PyObject *IkRule_get_bone(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<I>(self, "bone");
        if (!value)
            Py_RETURN_NONE;
        return bone_view(owner_of<I>(self), (smdl::BoneId) PyLong_AsUnsignedLong(value));
    });
}

int IkRule_set_bone(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'bone'");
        const smdl::BoneId bone = bone_arg(owner_of<I>(self), value, "bone");
        view_of<I>(self).setBone(bone);
        store_value<I>(self, "bone", (uint32_t) bone);
        return 0;
    });
}

PyObject *IkRule_get_attachment(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<I>(self, "attachment");
        if (!value)
            Py_RETURN_NONE;
        return attachment_view(owner_of<I>(self), (smdl::AttachmentId) PyLong_AsUnsignedLong(value));
    });
}

int IkRule_set_attachment(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'attachment'");
        const smdl::AttachmentId attachment = attachment_arg(owner_of<I>(self), value, "attachment");
        view_of<I>(self).setAttachment(attachment);
        store_value<I>(self, "attachment", (uint32_t) attachment);
        return 0;
    });
}

PyObject *IkRule_get_chain(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<I>(self, "chain");
        if (!value)
            Py_RETURN_NONE;
        return make_view(owner_of<I>(self), IkChainRef{(smdl::IkChainId) PyLong_AsUnsignedLong(value)});
    });
}

VIEW_FN(I, IkRule_set_range, "set_range", 0, 4, "start", "peak", "tail", "end")
    int frames[4];
    for (size_t i = 0; i < 4; ++i)
        frames[i] = a.has(i) ? a.get<int>(i) : -1;
    object.setRange(frames[0], frames[1], frames[2], frames[3]);
    Ref tuple = Ref::check(Py_BuildValue("(iiii)", frames[0], frames[1], frames[2], frames[3]));
    store<I>(self, "range", tuple.get());
FN_NONE

VIEW_FN(I, IkRule_set_fake_rotation, "set_fake_rotation", 0, 0, "rotation", "euler", "angles")
    const int given = a.has(0) + a.has(1) + a.has(2);
    if (given != 1)
        raise(PyExc_TypeError, "set_fake_rotation() takes exactly one of rotation=, euler=, angles=");
    if (a.has(0))
        object.setFakeRotation(a.get<smdl::Quat>(0));
    else if (a.has(1))
        object.setFakeRotationEuler(a.get<smdl::Vec3>(1));
    else
        object.setFakeAngles(a.get<smdl::Vec3>(2));
FN_NONE

PyMethodDef ik_rule_methods[] = {
    {"set_range", PYSMDL_METHOD(IkRule_set_range), PYSMDL_KW,
     "set_range(start: int | None = None, peak: int | None = None, tail: int | None = None, "
     "end: int | None = None) -> None\n--\n\nrange: animation frames; None stands for \"..\" "
     "(unset, stored as -1)."},
    {"set_fake_rotation", PYSMDL_METHOD(IkRule_set_fake_rotation), PYSMDL_KW,
     "set_fake_rotation(*, rotation: Quat | None = None, euler: Vec3 | None = None, angles: Vec3 | None = None) -> "
     "None\n--\n\nfakerotate: pass exactly one of rotation "
     "(quaternion x, y, z, w), euler (radians) or angles (the QC's degrees pitch, yaw, roll)."},
    {nullptr, nullptr, 0, nullptr},
};

const OptionDef ik_rule_options[] = {
    {"autosteps_end", "int | None\n--\n\nautosteps end frame.", &view_setter<I, &I::setAutoStepsEnd>},
    {"height", "float | None\n--\n\nik rule height.", &view_setter<I, &I::setHeight>},
    {"target", "int | None\n--\n\ntarget slot.", &view_setter<I, &I::setTarget>},
    {"floor", "float | None\n--\n\nfloor.", &view_setter<I, &I::setFloor>},
    {"pad", "float | None\n--\n\npad: a diameter (the radius is half).", &view_setter<I, &I::setPad>},
    {"radius", "float | None\n--\n\nradius.", &view_setter<I, &I::setRadius>},
    {"contact", "int | None\n--\n\ncontact.", &view_setter<I, &I::setContact>},
    {"use_sequence", "bool | None\n--\n\nusesequence.", &view_setter<I, &I::setUseSequence>},
    {"use_source", "bool | None\n--\n\nusesource.", &view_setter<I, &I::setUseSource>},
    {"fake_origin", "tuple[float, float, float] | None\n--\n\nfakeorigin (x, y, z).",
     &view_setter<I, &I::setFakeOrigin>},
};

const PyGetSetDef ik_rule_getsets[] = {
    {"bone", IkRule_get_bone, IkRule_set_bone, "Bone | None\n--\n\ntouch / autosteps bone: a Bone or a bone name.",
     nullptr},
    {"attachment", IkRule_get_attachment, IkRule_set_attachment,
     "Attachment | None\n--\n\nattachment rule: an Attachment, its name or its id (reads back an Attachment).",
     nullptr},
    {"type", stored_get<I>, nullptr, "IkRuleType\n--\n\nIkRuleType of the rule.", const_cast<char *>("type")},
    {"chain", IkRule_get_chain, nullptr, "IkChain\n--\n\nThe IkChain of the rule.", nullptr},
    {"range", stored_get<I>, nullptr,
     "tuple[int, int, int, int] | None\n--\n\n(start, peak, tail, end) of the last set_range() call (-1: unset).",
     const_cast<char *>("range")},
};

// ---------------------------------------------------------------------------------------------------------------------
// Sequence

PyObject *Sequence_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return to_py(view_of<S>(self).name()); });
}

PyObject *Sequence_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = reinterpret_cast<ViewObject<S> *>(self)->value().name();
        return PyUnicode_FromFormat("<Sequence '%s'>", std::string(name).c_str());
    });
}

// Animation-valued settings: the animation's name is stored (names are unique), the view is rebuilt on read.
template<auto Setter>
int sequence_set_animation(PyObject *self, PyObject *value, void *closure) {
    const char *name = static_cast<const char *>(closure);
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute '%s'", name);
        S &sequence = view_of<S>(self);
        const smdl::Animation animation = animation_arg(owner_of<S>(self), value, name);
        (sequence.*Setter)(animation);
        store_value<S>(self, name, std::string(animation.name()));
        return 0;
    });
}

PyObject *sequence_get_animation(PyObject *self, void *closure) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<S>(self, static_cast<const char *>(closure));
        if (!value)
            Py_RETURN_NONE;
        ModelObject *owner = owner_of<S>(self);
        return make_view(owner, model_of(owner).findAnimation(Conv<std::string>::from(value, "name")));
    });
}

PyObject *Sequence_get_pose_cycle(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<S>(self, "pose_cycle");
        if (!value)
            Py_RETURN_NONE;
        return pose_param_view(owner_of<S>(self), (smdl::PoseParamId) PyLong_AsUnsignedLong(value));
    });
}

int Sequence_set_pose_cycle(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'pose_cycle'");
        const smdl::PoseParamId pose = pose_param_arg(owner_of<S>(self), value, "pose_cycle");
        view_of<S>(self).setPoseCycle(pose);
        store_value<S>(self, "pose_cycle", (uint32_t) pose);
        return 0;
    });
}

PyObject *Sequence_get_root_driver(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        PyObject *value = load<S>(self, "root_driver");
        if (!value)
            Py_RETURN_NONE;
        return bone_view(owner_of<S>(self), (smdl::BoneId) PyLong_AsUnsignedLong(value));
    });
}

int Sequence_set_root_driver(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'root_driver'");
        const smdl::BoneId bone = bone_arg(owner_of<S>(self), value, "root_driver");
        view_of<S>(self).setRootDriver(bone);
        store_value<S>(self, "root_driver", (uint32_t) bone);
        return 0;
    });
}

VIEW_FN(S, Sequence_add_animation, "add_animation", 1, 1, "animation")
    object.addAnimation(animation_arg(owner, a[0], "animation"));
FN_NONE

VIEW_FN(S, Sequence_add_animations, "add_animations", 1, 1, "animations")
    if (PyUnicode_Check(a[0]))
        raise(PyExc_TypeError, "animations must be a sequence of Animation or names, not a single str");
    Ref iterator(PyObject_GetIter(a[0]));
    if (!iterator)
        raise(PyExc_TypeError, "animations must be an iterable of Animation or names, not %.100s", type_name(a[0]));
    std::vector<smdl::Animation> animations;
    while (PyObject *item = PyIter_Next(iterator.get())) {
        Ref owned(item);
        animations.push_back(animation_arg(owner, item, "animations"));
    }
    if (PyErr_Occurred())
        throw PyError{};
    object.addAnimations(animations);
FN_NONE

VIEW_FN(S, Sequence_add_blend, "add_blend", 3, 3, "pose_parameter", "start", "end")
    object.addBlend(pose_param_arg(owner, a[0], "pose_parameter"), a.get<float>(1), a.get<float>(2));
FN_NONE

VIEW_FN(S, Sequence_add_calc_blend, "add_calc_blend", 3, 3, "pose_parameter", "attachment", "control")
    object.addCalcBlend(pose_param_arg(owner, a[0], "pose_parameter"), attachment_arg(owner, a[1], "attachment"),
                        a.get<Flags<MotionFlags>>(2).value);
FN_NONE

VIEW_FN(S, Sequence_set_activity, "set_activity", 2, 2, "name", "weight")
    object.setActivity(a.get<std::string>(0), a.get<int>(1));
FN_NONE

VIEW_FN(S, Sequence_add_activity_modifier, "add_activity_modifier", 1, 1, "name")
    object.addActivityModifier(a.get<std::string>(0));
FN_NONE

VIEW_FN(S, Sequence_add_event, "add_event", 1, 3, "name", "frame", "options", "cycle")
    // cycle is keyword-only: Args allows 3 positional
    if (a.has(1) == a.has(3))
        raise(PyExc_TypeError, "add_event() takes exactly one of frame and cycle=");
    const std::string name = a.get<std::string>(0);
    const std::string options = a.get<std::string>(2);
    if (a.has(1))
        object.addEvent(name, a.get<int>(1), options);
    else
        object.addEventCycle(name, a.get<float>(3), options);
FN_NONE

VIEW_FN(S, Sequence_add_anim_tag, "add_anim_tag", 2, 2, "name", "cycle")
    object.addAnimTag(a.get<std::string>(0), a.get<float>(1));
FN_NONE

VIEW_FN(S, Sequence_set_flags, "set_flags", 1, 2, "flags", "enabled")
    object.setFlags(a.get<Flags<SequenceFlags>>(0).value, a.has(1) ? a.get<bool>(1) : true);
FN_NONE

VIEW_FN(S, Sequence_set_transition, "set_transition", 2, 3, "from_node", "to_node", "reverse")
    object.setTransition(a.get<std::string>(0), a.get<std::string>(1), a.has(2) && a.get<bool>(2));
FN_NONE

VIEW_FN(S, Sequence_add_layer, "add_layer", 1, 2, "layer", "local")
    object.addLayer(sequence_arg(owner, a[0], "layer"), a.has(1) && a.get<bool>(1));
FN_NONE

VIEW_FN(S, Sequence_add_blend_layer, "add_blend_layer", 5, 7, "layer", "start", "peak", "tail", "end", "flags",
        "pose_parameter")
    const smdl::Sequence layer = sequence_arg(owner, a[0], "layer");
    const uint32_t flags = a.has(5) ? a.get<Flags<LayerFlags>>(5).value : 0;
    if ((flags & SMDL_LAYER_POSE) && !a.has(6))
        raise(PyExc_TypeError, "LayerFlag.POSE needs pose_parameter");
    if (!(flags & SMDL_LAYER_POSE) && a.has(6))
        raise(PyExc_TypeError, "pose_parameter is only used with LayerFlag.POSE");
    const smdl::PoseParamId pose = a.has(6) ? pose_param_arg(owner, a[6], "pose_parameter") : 0;
    object.addBlendLayer(layer, a.get<float>(1), a.get<float>(2), a.get<float>(3), a.get<float>(4), flags, pose);
FN_NONE

VIEW_FN(S, Sequence_add_ik_lock, "add_ik_lock", 3, 3, "chain", "position_weight", "local_rotation_weight")
    object.addIkLock(ik_chain_arg(owner, a[0], "chain"), a.get<float>(1), a.get<float>(2));
FN_NONE

VIEW_FN(S, Sequence_add_key_values, "add_key_values", 1, 1, "text")
    object.addKeyValues(a.get<std::string>(0));
FN_NONE

PyMethodDef sequence_methods[] = {
    {"add_animation", PYSMDL_METHOD(Sequence_add_animation), PYSMDL_KW,
     "add_animation(animation: Animation | str) -> None\n--\n\nThe next animation of the sequence (an Animation or "
     "its name); blend animations go in the order of the QC list."},
    {"add_animations", PYSMDL_METHOD(Sequence_add_animations), PYSMDL_KW,
     "add_animations(animations: Iterable[Animation | str]) -> None\n--\n\nSeveral animations at once (Animations or "
     "names), e.g. a blend grid."},
    {"add_blend", PYSMDL_METHOD(Sequence_add_blend), PYSMDL_KW,
     "add_blend(pose_parameter: PoseParameter | str | int, start: float, end: float) -> None\n--\n\nblend: drives a "
     "blend axis; the first call is the first axis, the second call the second. pose_parameter: PoseParameter, name "
     "or id."},
    {"add_calc_blend", PYSMDL_METHOD(Sequence_add_calc_blend), PYSMDL_KW,
     "add_calc_blend(pose_parameter: PoseParameter | str | int, attachment: Attachment | str | int, control: Motion) "
     "-> None\n--\n\ncalcblend: the pose parameter is calculated from the motion of the attachment; control is one "
     "Motion flag."},
    {"set_activity", PYSMDL_METHOD(Sequence_set_activity), PYSMDL_KW,
     "set_activity(name: str, weight: int) -> None\n--\n\nactivity: weight is an integer > 0."},
    {"add_activity_modifier", PYSMDL_METHOD(Sequence_add_activity_modifier), PYSMDL_KW,
     "add_activity_modifier(name: str) -> None\n--\n\nactivitymodifier."},
    {"add_event", PYSMDL_METHOD(Sequence_add_event), PYSMDL_KW,
     "add_event(name: str, frame: int | None = None, options: str = '', *, cycle: float | None = None) -> "
     "None\n--\n\nevent: placed at `frame` of the sequence's first "
     "animation, or at `cycle` (0..1 over the first animation, converted to the nearest frame when compiled). Pass "
     "exactly one of frame and cycle. name: event name or number (\"AE_...\", \"5004\")."},
    {"add_anim_tag", PYSMDL_METHOD(Sequence_add_anim_tag), PYSMDL_KW,
     "add_anim_tag(name: str, cycle: float) -> None\n--\n\nanimtag."},
    {"set_flags", PYSMDL_METHOD(Sequence_set_flags), PYSMDL_KW,
     "set_flags(flags: SequenceFlag, enabled: bool = True) -> None\n--\n\nSets (or with enabled=False clears) every "
     "SequenceFlag in flags."},
    {"set_transition", PYSMDL_METHOD(Sequence_set_transition), PYSMDL_KW,
     "set_transition(from_node: str, to_node: str, reverse: bool = False) -> None\n--\n\ntransition (rtransition "
     "with reverse=True)."},
    {"add_layer", PYSMDL_METHOD(Sequence_add_layer), PYSMDL_KW,
     "add_layer(layer: Sequence | str, local: bool = False) -> None\n--\n\naddlayer: layer is a Sequence or its name "
     "(it may be created later). Layers apply in the order added."},
    {"add_blend_layer", PYSMDL_METHOD(Sequence_add_blend_layer), PYSMDL_KW,
     "add_blend_layer(layer: Sequence | str, start: float, peak: float, tail: float, end: float, "
     "flags: LayerFlag | int = 0, pose_parameter: PoseParameter | str | int | None = None) -> None\n--\n\n"
     "blendlayer: frames of this sequence's first animation; flags: LayerFlag combination; pose_parameter "
     "(PoseParameter, name or id) is needed with LayerFlag.POSE and only then."},
    {"add_ik_lock", PYSMDL_METHOD(Sequence_add_ik_lock), PYSMDL_KW,
     "add_ik_lock(chain: IkChain | str | int, position_weight: float, local_rotation_weight: float) -> "
     "None\n--\n\niklock."},
    {"add_key_values", PYSMDL_METHOD(Sequence_add_key_values), PYSMDL_KW,
     "add_key_values(text: str) -> None\n--\n\nkeyvalues: the text between the braces of the QC block; calls append. "
     "Raises ExpressionSyntaxError for a syntax error."},
    {nullptr, nullptr, 0, nullptr},
};

const OptionDef sequence_options[] = {
    {"blend_width", "int | None\n--\n\nblendwidth: the size of the first axis of the blend grid.",
     &view_setter<S, &S::setBlendWidth>},
    {"fade_in", "float | None\n--\n\nfadein in seconds (default $defaultfadein).", &view_setter<S, &S::setFadeIn>},
    {"fade_out", "float | None\n--\n\nfadeout in seconds (default $defaultfadeout).",
     &view_setter<S, &S::setFadeOut>},
    {"exit_phase", "float | None\n--\n\nexitphase.", &view_setter<S, &S::setExitPhase>},
    {"node", "str | None\n--\n\nnode: the transition graph node name.", &view_setter<S, &S::setNode>},
};

const PyGetSetDef sequence_getsets[] = {
    {"name", Sequence_name, nullptr, "str\n--\n\nName of the sequence.", nullptr},
    {"blend_reference", sequence_get_animation, sequence_set_animation<&S::setBlendReference>,
     "Animation | None\n--\n\nblendref: the reference animation (an Animation or its name).",
     const_cast<char *>("blend_reference")},
    {"blend_compare", sequence_get_animation, sequence_set_animation<&S::setBlendCompare>,
     "Animation | None\n--\n\nblendcomp: the compare animation (an Animation or its name).",
     const_cast<char *>("blend_compare")},
    {"blend_center", sequence_get_animation, sequence_set_animation<&S::setBlendCenter>,
     "Animation | None\n--\n\nblendcenter: the center animation (an Animation or its name).",
     const_cast<char *>("blend_center")},
    {"pose_cycle", Sequence_get_pose_cycle, Sequence_set_pose_cycle,
     "PoseParameter | None\n--\n\nposecycle: the pose parameter the cycle is taken from (PoseParameter, name or id).",
     nullptr},
    {"root_driver", Sequence_get_root_driver, Sequence_set_root_driver,
     "Bone | None\n--\n\nrootdriver: a Bone or a bone name.", nullptr},
};

// ---------------------------------------------------------------------------------------------------------------------
// Model

PyObject *Model_create_animation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("create_animation", args, kwargs, {"name", "fps", "frame_count"}, 3, 3);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).createAnimation(a.get<std::string>(0), a.get<float>(1),
                                                                a.get<uint32_t>(2)));
    });
}

PyObject *Model_find_animation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_animation", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).findAnimation(a.get<std::string>(0)));
    });
}

PyObject *Model_create_sequence(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("create_sequence", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).createSequence(a.get<std::string>(0)));
    });
}

PyObject *Model_find_sequence(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_sequence", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, model_of(model).findSequence(a.get<std::string>(0)));
    });
}

PyObject *Model_animations(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        smdl::Model &cpp = model_of(model);
        const uint32_t count = cpp.animationCount();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(model, cpp.animation(i)));
        return tuple.release();
    });
}

PyObject *Model_sequences(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        ModelObject *model = model_ptr(self);
        smdl::Model &cpp = model_of(model);
        const uint32_t count = cpp.sequenceCount();
        Ref tuple = Ref::check(PyTuple_New(count));
        for (uint32_t i = 0; i < count; ++i)
            PyTuple_SetItem(tuple.get(), i, make_view(model, cpp.sequence(i)));
        return tuple.release();
    });
}

const PyMethodDef model_methods[] = {
    {"create_animation", PYSMDL_METHOD(Model_create_animation), PYSMDL_KW,
     "create_animation(name: str, fps: float, frame_count: int) -> Animation\n--\n\n$animation: a new Animation of "
     "frame_count (1..5000) frames at fps. name must be unique among animations and sequences; \"@<sequence>\" is "
     "the animation a $sequence with an SMD creates. Animations apply in creation order: create the ones a command "
     "refers to first."},
    {"find_animation", PYSMDL_METHOD(Model_find_animation), PYSMDL_KW,
     "find_animation(name: str) -> Animation\n--\n\nThe Animation with this name; raises NotFoundError."},
    {"create_sequence", PYSMDL_METHOD(Model_create_sequence), PYSMDL_KW,
     "create_sequence(name: str) -> Sequence\n--\n\n$sequence: a new Sequence (name unique among sequences and "
     "animations). Add its animations with Sequence.add_animation."},
    {"find_sequence", PYSMDL_METHOD(Model_find_sequence), PYSMDL_KW,
     "find_sequence(name: str) -> Sequence\n--\n\nThe Sequence with this name; raises NotFoundError."},
};

const PyGetSetDef model_getsets[] = {
    {"animations", Model_animations, nullptr, "tuple[Animation, ...]\n--\n\nAnimations in creation order.", nullptr},
    {"sequences", Model_sequences, nullptr, "tuple[Sequence, ...]\n--\n\nSequences in creation order.", nullptr},
};

int init(PyObject *module) {
    if (register_enum<MotionFlags>(module, "Motion",
                                   {{"X", SMDL_MOTION_X}, {"Y", SMDL_MOTION_Y}, {"Z", SMDL_MOTION_Z},
                                    {"XR", SMDL_MOTION_XR}, {"YR", SMDL_MOTION_YR}, {"ZR", SMDL_MOTION_ZR},
                                    {"LX", SMDL_MOTION_LX}, {"LY", SMDL_MOTION_LY}, {"LZ", SMDL_MOTION_LZ},
                                    {"LXR", SMDL_MOTION_LXR}, {"LYR", SMDL_MOTION_LYR}, {"LZR", SMDL_MOTION_LZR},
                                    {"LINEAR", SMDL_MOTION_LINEAR}, {"QUADRATIC", SMDL_MOTION_QUADRATIC}},
                                   true) < 0)
        return -1;
    if (register_enum<SequenceFlags>(module, "SequenceFlag",
                                     {{"SNAP", SMDL_SEQUENCE_SNAP}, {"DELTA", SMDL_SEQUENCE_DELTA},
                                      {"PRE_DELTA", SMDL_SEQUENCE_PRE_DELTA},
                                      {"WORLD_SPACE", SMDL_SEQUENCE_WORLD_SPACE},
                                      {"WORLD_RELATIVE", SMDL_SEQUENCE_WORLD_RELATIVE}, {"POST", SMDL_SEQUENCE_POST},
                                      {"AUTOPLAY", SMDL_SEQUENCE_AUTOPLAY}, {"REALTIME", SMDL_SEQUENCE_REALTIME},
                                      {"HIDDEN", SMDL_SEQUENCE_HIDDEN}},
                                     true) < 0)
        return -1;
    if (register_enum<LayerFlags>(module, "LayerFlag",
                                  {{"XFADE", SMDL_LAYER_XFADE}, {"SPLINE", SMDL_LAYER_SPLINE},
                                   {"NO_BLEND", SMDL_LAYER_NO_BLEND}, {"LOCAL", SMDL_LAYER_LOCAL},
                                   {"POSE", SMDL_LAYER_POSE}},
                                  true) < 0)
        return -1;
    if (register_enum<smdl::IkRuleType>(module, "IkRuleType",
                                        {{"AUTOSTEPS", SMDL_IK_RULE_AUTOSTEPS}, {"TOUCH", SMDL_IK_RULE_TOUCH},
                                         {"FOOTSTEP", SMDL_IK_RULE_FOOTSTEP},
                                         {"ATTACHMENT", SMDL_IK_RULE_ATTACHMENT}, {"RELEASE", SMDL_IK_RULE_RELEASE},
                                         {"UNLATCH", SMDL_IK_RULE_UNLATCH}}) < 0)
        return -1;

    PyGetSetDef *animation_defs = option_getsets({animation_options}, {animation_getsets}, view_option_key<A>,
                                                 view_option_owner<A>);
    if (add_view_type<A>(module, "Animation",
                         "An animation of a Model ($animation): frames of bone poses, options and commands. Options "
                         "are properties reading back the value last assigned (None: the default); commands are "
                         "add_* methods run in the order added.",
                         animation_methods, animation_defs, Animation_repr) < 0)
        return -1;
    PyGetSetDef *ik_rule_defs = option_getsets({ik_rule_options}, {ik_rule_getsets}, view_option_key<I>,
                                               view_option_owner<I>);
    if (add_view_type<I>(module, "IkRule",
                         "The rule of an ikrule / ikfixup command (Animation.add_ik_rule / add_ik_fixup); set its "
                         "parameters after adding it. Unset parameters keep the rule type's default.",
                         ik_rule_methods, ik_rule_defs) < 0)
        return -1;
    PyGetSetDef *sequence_defs = option_getsets({sequence_options}, {sequence_getsets}, view_option_key<S>,
                                                view_option_owner<S>);
    return add_view_type<S>(module, "Sequence",
                            "A sequence of a Model ($sequence): one animation or a blend grid, with activity, events, "
                            "layers and flags. Settings are properties reading back the value last assigned (None: "
                            "the default).",
                            sequence_methods, sequence_defs, Sequence_repr);
}

} // namespace

const Part part_animation = {
    {},
    model_methods,
    {},
    model_getsets,
    init,
};

} // namespace pysmdl
