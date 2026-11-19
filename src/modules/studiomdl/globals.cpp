//
// pylib.studiomdl: model-wide settings (smdl::detail::ModelGlobalsApi, smdl_globals.h).
//

#include "modules/studiomdl/bind.h"

namespace pysmdl {

using G = smdl::detail::ModelGlobalsApi;

namespace {

smdl::Model &self_model(PyObject *self) { return model_of(reinterpret_cast<ModelObject *>(self)); }

// (mins, maxs) pairs
template<void (G::*Setter)(const smdl::Vec3 &, const smdl::Vec3 &)>
PyObject *box_setter(PyObject *self, PyObject *value, const char *name) {
    if (!PySequence_Check(value) || PySequence_Size(value) != 2)
        raise(PyExc_TypeError, "%s must be a (mins, maxs) pair", name);
    Ref mins = Ref::check(PySequence_GetItem(value, 0));
    Ref maxs = Ref::check(PySequence_GetItem(value, 1));
    const auto low = Conv<smdl::Vec3>::from(mins.get(), name);
    const auto high = Conv<smdl::Vec3>::from(maxs.get(), name);
    (self_model(self).*Setter)(low, high);
    Ref a = Ref::check(Conv<smdl::Vec3>::to(low));
    Ref b = Ref::check(Conv<smdl::Vec3>::to(high));
    return PyTuple_Pack(2, a.get(), b.get());
}

PyObject *constant_directional_light_setter(PyObject *self, PyObject *value, const char *name) {
    if (value == Py_False) {
        self_model(self).setConstantDirectionalLight(false, 0.0f);
        Py_RETURN_FALSE;
    }
    const float dot = Conv<float>::from(value, name);
    self_model(self).setConstantDirectionalLight(true, dot);
    return PyFloat_FromDouble(dot);
}

// The property annotation ("<value type> | None") is derived from the setter's argument type.
#define OPT(name, setter, doc) OptionDef{name, doc, &model_setter<&G::setter>, setter_annotation<&G::setter>}
#define FLAG_OPT(name, setter, Tag, doc) \
    OptionDef{name, doc, &model_setter<&G::setter, Flags<Tag>>, &python_type<Flags<Tag>>}
#define BOX_TYPE "tuple[tuple[float, float, float], tuple[float, float, float]] | None\n--\n\n"

const OptionDef options[] = {
    // Identity and paths
    OPT("internal_name", setInternalName, "$internalname: the name stored in the .mdl header."),
    // Transform
    OPT("scale", setScale, "$scale: scale of the source geometry and animations."),
    OPT("origin", setOrigin, "$origin: offset subtracted from the source data (x, y, z)."),
    OPT("origin_angle", setOriginAngle, "Rotation of $origin about the up axis, degrees."),
    OPT("up_axis", setUpAxis, "$upaxis of the source data."),
    OPT("autocenter", setAutocenter, "$autocenter: center the model on its bounding box."),
    OPT("center_bones_on_verts", setCenterBonesOnVerts, "$centerbonesonverts."),
    // Surface properties and contents
    OPT("surface_prop", setSurfaceProp, "$surfaceprop of the model."),
    FLAG_OPT("contents", setContents, ContentsFlags, "$contents of the model."),
    // Flags
    OPT("static_prop", setStaticProp, "$staticprop: a model without animation, bones collapsed."),
    OPT("opaque", setOpaque, "$opaque."),
    OPT("mostly_opaque", setMostlyOpaque, "$mostlyopaque."),
    OPT("ambient_boost", setAmbientBoost, "$ambientboost."),
    OPT("no_forced_fade", setNoForcedFade, "$noforcedfade."),
    OPT("do_not_cast_shadows", setDoNotCastShadows, "$donotcastshadows."),
    OPT("cast_texture_shadows", setCastTextureShadows, "$casttextureshadows."),
    OPT("subd", setSubd, "$subd: Catmull-Clark quads model."),
    OPT("obsolete", setObsolete, "$obsolete."),
    OPT("force_phoneme_crossfade", setForcePhonemeCrossfade, "$forcephonemecrossfade."),
    OptionDef{"constant_directional_light",
              "float | bool | None\n--\n\n$constantdirectionallight: the dot value, or False for none.",
              &constant_directional_light_setter},
    // Header values
    OPT("eye_position", setEyePosition, "$eyeposition (x, y, z)."),
    OPT("max_eye_deflection", setMaxEyeDeflection, "$maxeyedeflection, degrees."),
    OPT("gamma", setGamma, "$gamma."),
    OptionDef{"bbox", BOX_TYPE "$bbox: (mins, maxs) of the bounding box.", &box_setter<&G::setBBox>},
    OptionDef{"cbox", BOX_TYPE "$cbox: (mins, maxs) of the clipping box.", &box_setter<&G::setCBox>},
    OPT("bbox_only_verts", setBBoxOnlyVerts, "$bboxonlyverts."),
    OPT("skip_bone_in_bbox", setSkipBoneInBBox, "$skipboneinbbox."),
    OPT("collision_precision", setCollisionPrecision, "$collisionprecision."),
    OPT("qc_path", setQcPath, "Value of the qc_path key value of the .mdl (empty by default for code-built models)."),
    // Animation and LOD defaults
    OPT("min_lod", setMinLod, "$minlod."),
    OPT("allow_root_lods", setAllowRootLods, "$allowrootlods."),
    OPT("default_fade_in_time", setDefaultFadeInTime, "Default sequence fade-in, seconds."),
    OPT("default_fade_out_time", setDefaultFadeOutTime, "Default sequence fade-out, seconds."),
    OPT("motion_rollback", setMotionRollback, "$motionrollback."),
    OptionDef{"clamp_worldspace", BOX_TYPE "$clampworldspace: (mins, maxs).", &box_setter<&G::setClampWorldspace>},
    OPT("error_on_sequence_remap_failure", setErrorOnSequenceRemapFailure, "$sequenceremap errors are fatal."),
    OPT("model_has_no_sequences", setModelHasNoSequences, "No sequences, not even the default one."),
    OPT("calc_transitions", setCalcTransitions, "$calctransitions."),
    OPT("anim_block_size", setAnimBlockSize, "$animblocksize."),
    OPT("anim_block_no_stall", setAnimBlockNoStall, "$animblocksize nostall."),
    OPT("anim_block_resolution", setAnimBlockResolution, "$animblocksize highres/lowres."),
    OPT("anim_block_num_frames", setAnimBlockNumFrames, "$animblocksize numframes."),
    OPT("anim_block_cache_high_res", setAnimBlockCacheHighRes, "$animblocksize cachehighres."),
    OPT("anim_block_pos_delta", setAnimBlockPosDelta, "$animblocksize posdelta."),
    // Compile options
    OPT("zbrush", setZBrush, "$zbrush."),
    OPT("preserve_triangle_order", setPreserveTriangleOrder, "$preservetriangleorder."),
    OPT("clip_to_textures", setClipToTextures, "$cliptotextures."),
    OPT("flex_optimize", setFlexOptimize, "-flexopt."),
    OPT("flex_optimize_min_static", setFlexOptimizeMinStatic, "-flexoptminstatic vertex count."),
    OPT("normal_blend_angle", setNormalBlendAngle, "$normalblendangle, degrees."),
    OPT("no_warnings", setNoWarnings, "-nowarnings."),
    OPT("max_warnings", setMaxWarnings, "-maxwarnings."),
    OPT("strip_lods", setStripLods, "-striplods."),
    OPT("build_preview", setBuildPreview, "-preview."),
};

#undef BOX_TYPE

#undef OPT
#undef FLAG_OPT

// ---- Methods

PyObject *add_material_path(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_material_path", args, kwargs, {"path"}, 1, 1);
        self_model(self).addMaterialPath(a.get<std::string>(0));
        return none();
    });
}

PyObject *add_include_model(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_include_model", args, kwargs, {"path"}, 1, 1);
        self_model(self).addIncludeModel(a.get<std::string>(0));
        return none();
    });
}

PyObject *add_skin_family(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_skin_family", args, kwargs, {"materials"}, 1, 1);
        const std::vector<std::string> materials = string_list(a[0], "materials");
        return PyLong_FromUnsignedLong(self_model(self).addSkinFamily(std::span<const std::string>(materials)));
    });
}

PyObject *override_material(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("override_material", args, kwargs, {"name"}, 1, 1);
        self_model(self).overrideMaterial(a.get<std::string>(0));
        return none();
    });
}

PyObject *add_texture_replacement(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_texture_replacement", args, kwargs, {"replacement", "replaces"}, 1, 2);
        self_model(self).addTextureReplacement(a.get<std::string>(0), a.get<std::string>(1));
        return none();
    });
}

PyObject *add_key_values(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_key_values", args, kwargs, {"text"}, 1, 1);
        self_model(self).addKeyValues(a.get<std::string>(0));
        return none();
    });
}

PyObject *add_allowed_activity_name(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_allowed_activity_name", args, kwargs, {"name"}, 1, 1);
        self_model(self).addAllowedActivityName(a.get<std::string>(0));
        return none();
    });
}

PyObject *add_skip_transition(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_skip_transition", args, kwargs, {"nodes"}, 1, 1);
        const std::vector<std::string> nodes = string_list(a[0], "nodes");
        self_model(self).addSkipTransition(std::span<const std::string>(nodes));
        return none();
    });
}

PyObject *set_illum_position(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_illum_position", args, kwargs, {"position", "bone"}, 1, 2);
        auto *model = reinterpret_cast<ModelObject *>(self);
        const auto position = a.get<smdl::Vec3>(0);
        const smdl::BoneId bone = a[1] ? bone_arg(model, a[1], "bone", true) : smdl::NoBone;
        model_of(model).setIllumPosition(position, bone);
        return none();
    });
}

PyObject *set_max_vertices(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_max_vertices", args, kwargs, {"limit", "clamp_target"}, 1, 2);
        self_model(self).setMaxVertices(a.get<int>(0), a.get<int>(1, 0));
        return none();
    });
}

PyObject *set_fit_weights(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_fit_weights", args, kwargs, {"enabled", "fitting_poses", "test_poses"}, 0, 3);
        self_model(self).setFitWeights(a.get<bool>(0, true), a.get<int>(1, 0), a.get<int>(2, 0));
        return none();
    });
}

PyObject *set_section_frames(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_section_frames", args, kwargs, {"frames", "min_frames"}, 2, 2);
        self_model(self).setSectionFrames(a.get<int>(0), a.get<int>(1));
        return none();
    });
}

const PyMethodDef methods[] = {
    {"add_material_path", PYSMDL_METHOD(add_material_path), PYSMDL_KW,
     "add_material_path(path: str) -> None\n--\n\n$cdmaterials: a material directory relative to materials/."},
    {"add_include_model", PYSMDL_METHOD(add_include_model), PYSMDL_KW,
     "add_include_model(path: str) -> None\n--\n\n$includemodel: an animation model (\"humans/male_shared.mdl\")."},
    {"add_skin_family", PYSMDL_METHOD(add_skin_family), PYSMDL_KW,
     "add_skin_family(materials: StrSequence) -> int\n--\n\n"
     "One row of $texturegroup: material names, one per material slot. The first family is the default skin. "
     "Returns the family index."},
    {"override_material", PYSMDL_METHOD(override_material), PYSMDL_KW,
     "override_material(name: str) -> None\n--\n\n$renamematerial-like override of every material with name."},
    {"add_texture_replacement", PYSMDL_METHOD(add_texture_replacement), PYSMDL_KW,
     "add_texture_replacement(replacement: str, replaces: str | None = None) -> None\n--\n\n"
     "Replace the material replaces (None: every material) with replacement."},
    {"add_key_values", PYSMDL_METHOD(add_key_values), PYSMDL_KW,
     "add_key_values(text: str) -> None\n--\n\n$keyvalues text without the outer braces (\"prop_data { base "
     "Wooden.Large }\"). "
     "Raises InvalidArgumentError on a syntax error."},
    {"add_allowed_activity_name", PYSMDL_METHOD(add_allowed_activity_name), PYSMDL_KW,
     "add_allowed_activity_name(name: str) -> None\n--\n\n$allowactivityname."},
    {"add_skip_transition", PYSMDL_METHOD(add_skip_transition), PYSMDL_KW,
     "add_skip_transition(nodes: StrSequence) -> None\n--\n\n$skiptransition between the named nodes."},
    {"set_illum_position", PYSMDL_METHOD(set_illum_position), PYSMDL_KW,
     "set_illum_position(position: Vec3, bone: BoneLike | None = None) -> None\n--\n\n"
     "$illumposition, optionally relative to a bone."},
    {"set_max_vertices", PYSMDL_METHOD(set_max_vertices), PYSMDL_KW,
     "set_max_vertices(limit: int, clamp_target: int = 0) -> None\n--\n\n$maxverts."},
    {"set_fit_weights", PYSMDL_METHOD(set_fit_weights), PYSMDL_KW,
     "set_fit_weights(enabled: bool = True, fitting_poses: int = 0, test_poses: int = 0) -> None\n--\n\n"
     "-fitweights (experimental); 0 poses keeps studiomdl's default count (24)."},
    {"set_section_frames", PYSMDL_METHOD(set_section_frames), PYSMDL_KW,
     "set_section_frames(frames: int, min_frames: int) -> None\n--\n\n$sectionframes."},
};

int init(PyObject *module) {
    if (register_enum<smdl::UpAxis>(module, "UpAxis", {{"X", SMDL_UP_AXIS_X}, {"NEG_X", SMDL_UP_AXIS_NEG_X},
                                                        {"Y", SMDL_UP_AXIS_Y}, {"NEG_Y", SMDL_UP_AXIS_NEG_Y},
                                                        {"Z", SMDL_UP_AXIS_Z}, {"NEG_Z", SMDL_UP_AXIS_NEG_Z}}) < 0)
        return -1;
    if (register_enum<ContentsFlags>(module, "Contents", {{"SOLID", SMDL_CONTENTS_SOLID},
                                                          {"GRATE", SMDL_CONTENTS_GRATE},
                                                          {"LADDER", SMDL_CONTENTS_LADDER},
                                                          {"MONSTER", SMDL_CONTENTS_MONSTER},
                                                          {"NOT_SOLID", SMDL_CONTENTS_NOTSOLID}}, true) < 0)
        return -1;
    if (register_enum<smdl::AnimBlockResolution>(module, "AnimBlockResolution",
                                                 {{"DEFAULT", SMDL_ANIM_BLOCK_DEFAULT},
                                                  {"HIGH_RES", SMDL_ANIM_BLOCK_HIGH_RES},
                                                  {"LOW_RES", SMDL_ANIM_BLOCK_LOW_RES}}) < 0)
        return -1;
    return register_enum<smdl::FlexOptimizeMode>(module, "FlexOptimize", {{"AUTO", SMDL_FLEX_OPTIMIZE_AUTO},
                                                                          {"ON", SMDL_FLEX_OPTIMIZE_ON},
                                                                          {"OFF", SMDL_FLEX_OPTIMIZE_OFF}});
}

} // namespace

const Part part_globals = {
    {},
    methods,
    options,
    {},
    init,
};

} // namespace pysmdl
