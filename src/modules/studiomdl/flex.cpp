//
// pylib.studiomdl: flexes (smdl::detail::ModelFlexApi on Model, smdl::detail::MeshFlexApi on Mesh, smdl_flex.h).
//

#include "geometry_shared.h"

namespace pysmdl {

namespace {

using F = smdl::detail::MeshFlexApi;

ModelObject *model_ptr(PyObject *self) { return reinterpret_cast<ModelObject *>(self); }

// ---------------------------------------------------------------------------------------------------------------------
// Id views

// A flex of the model (the C++ API's FlexId).
struct FlexRef {
    uint32_t id;
    uintptr_t handle() const { return id; }
};

// A flex controller of the model (FlexControllerId).
struct FlexControllerRef {
    uint32_t id;
    uintptr_t handle() const { return id; }
};

// An item of a mesh (eyeball, flex key, eyelid, ...), numbered per mesh in call order.
template<class Tag>
struct MeshItem {
    using tag = Tag;
    smdl::Mesh mesh;
    uint32_t id;
    uintptr_t handle() const { return reinterpret_cast<uintptr_t>(mesh.handle()) * 65536u + id; }
};

struct EyeballTag { static constexpr const char *kind = "name:eyeball"; static constexpr const char *type = "Eyeball"; };
struct FlexKeyTag { static constexpr const char *kind = "flexkey"; static constexpr const char *type = "FlexKey"; };
struct EyelidTag { static constexpr const char *kind = "name:eyelid"; static constexpr const char *type = "Eyelid"; };
struct DmxEyelidTag { static constexpr const char *kind = nullptr; static constexpr const char *type = "DmxEyelid"; };
struct ControlTag {
    static constexpr const char *kind = "name:control";
    static constexpr const char *type = "CombinationControl";
};

using Eyeball = MeshItem<EyeballTag>;
using FlexKey = MeshItem<FlexKeyTag>;
using Eyelid = MeshItem<EyelidTag>;
using DmxEyelid = MeshItem<DmxEyelidTag>;
using CombinationControl = MeshItem<ControlTag>;

uintptr_t scope_of(const smdl::Mesh &mesh) { return reinterpret_cast<uintptr_t>(mesh.handle()); }

PyObject *utf8(const std::string &text) { return Conv<std::string>::to(text); }

void store_text(ModelObject *owner, const char *kind, uintptr_t scope, uint32_t id, const std::string &text) {
    Ref value = Ref::check(utf8(text));
    store_value(owner, kind, scope, id, value.get());
}

template<class Item>
PyObject *make_item(ModelObject *owner, const smdl::Mesh &mesh, uint32_t id, const std::string *name = nullptr) {
    if (name)
        store_text(owner, Item::tag::kind, scope_of(mesh), id, *name);
    return make_view(owner, Item{mesh, id});
}

PyObject *flex_view(ModelObject *owner, uint32_t id, const std::string &name) {
    Ref text = Ref::check(utf8(name));
    store_value_default(owner, "name:flex", 0, id, text.get());
    return make_view(owner, FlexRef{id});
}

// An item of `mesh` from a view, an int, a stored name (named items) or None (smdl::NoId when allow_none).
template<class Item>
uint32_t item_arg(ModelObject *owner, const smdl::Mesh &mesh, PyObject *object, const char *name, bool allow_none) {
    if (object == Py_None) {
        if (allow_none)
            return smdl::NoId;
        raise(PyExc_TypeError, "%s must be a %s or its index, not None", name, Item::tag::type);
    }
    if (PyLong_Check(object))
        return Conv<uint32_t>::from(object, name);
    if (PyUnicode_Check(object)) {
        if (!Item::tag::kind)
            raise(PyExc_TypeError, "%s must be a %s or its index, not str", name, Item::tag::type);
        const std::string text = Conv<std::string>::from(object, name);
        uint32_t id = 0;
        if (!find_stored_name(owner, Item::tag::kind, scope_of(mesh), text, id))
            raise(errors.not_found, "mesh has no %s named '%s'", Item::tag::type, text.c_str());
        return id;
    }
    const Item &item = view_arg<Item>(owner, object, name);
    if (item.mesh.handle() != mesh.handle())
        raise(PyExc_ValueError, "%s belongs to another mesh", name);
    return item.id;
}

// ---- generic getters

template<class Item>
PyObject *Item_index(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<Item>(self).id); });
}

template<class Item>
PyObject *Item_mesh(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return make_view(owner_of<Item>(self), smdl::Mesh(view_of<Item>(self).mesh)); });
}

template<class Item>
PyObject *Item_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const Item &item = view_of<Item>(self);
        return stored_value(owner_of<Item>(self), Item::tag::kind, scope_of(item.mesh), item.id);
    });
}

template<class Item>
PyObject *Item_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const Item &item = reinterpret_cast<ViewObject<Item> *>(self)->value();
        if (!Item::tag::kind)
            return PyUnicode_FromFormat("<%s %u>", Item::tag::type, item.id);
        Ref name(stored_value(owner_of<Item>(self), Item::tag::kind, scope_of(item.mesh), item.id));
        if (name.get() == Py_None)
            return PyUnicode_FromFormat("<%s %u>", Item::tag::type, item.id);
        if (PyTuple_Check(name.get()))  // a flex key: (name, is pair)
            return PyUnicode_FromFormat("<%s %u %R>", Item::tag::type, item.id, PyTuple_GetItem(name.get(), 0));
        return PyUnicode_FromFormat("<%s %u %R>", Item::tag::type, item.id, name.get());
    });
}

template<class Item>
std::span<const PyGetSetDef> item_getsets(bool named) {
    static const PyGetSetDef with_name[] = {
        {"name", Item_name<Item>, nullptr, "str | None\n--\n\nName it was created with.", nullptr},
        {"index", Item_index<Item>, nullptr, "int\n--\n\nIndex in the mesh (call order).", nullptr},
        {"mesh", Item_mesh<Item>, nullptr, "Mesh\n--\n\nThe Mesh it belongs to.", nullptr},
    };
    return named ? std::span<const PyGetSetDef>(with_name, 3) : std::span<const PyGetSetDef>(with_name + 1, 2);
}

template<class>
struct item_setter_traits;
template<class C, class R, class A>
struct item_setter_traits<R (C::*)(uint32_t, A) const> {
    using arg = A;
};

// Option setter calling a mesh member taking (item id, value).
template<class Item, auto Setter, class Stored = stored_t<typename item_setter_traits<decltype(Setter)>::arg>>
PyObject *item_setter(PyObject *self, PyObject *value, const char *name) {
    Stored converted = Conv<Stored>::from(value, name);
    const Item &item = view_of<Item>(self);
    (item.mesh.*Setter)(item.id, cpp_arg(converted));
    return Conv<Stored>::to(converted);
}

// ---- Flex / FlexController

smdl::FlexId flex_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findFlex(Conv<std::string>::from(object, name));
    return view_arg<FlexRef>(owner, object, name).id;
}

smdl::FlexControllerId controller_arg(ModelObject *owner, PyObject *object, const char *name) {
    if (PyUnicode_Check(object))
        return model_of(owner).findFlexController(Conv<std::string>::from(object, name));
    if (PyLong_Check(object))
        return Conv<uint32_t>::from(object, name);
    return view_arg<FlexControllerRef>(owner, object, name).id;
}

PyObject *Flex_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return stored_value(owner_of<FlexRef>(self), "name:flex", 0, view_of<FlexRef>(self).id);
    });
}

PyObject *Flex_index(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<FlexRef>(self).id); });
}

PyObject *Flex_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const uint32_t id = reinterpret_cast<ViewObject<FlexRef> *>(self)->value().id;
        Ref name(stored_value(owner_of<FlexRef>(self), "name:flex", 0, id));
        if (name.get() == Py_None)
            return PyUnicode_FromFormat("<Flex %u>", id);
        return PyUnicode_FromFormat("<Flex %u %R>", id, name.get());
    });
}

const PyGetSetDef flex_getsets[] = {
    {"name", Flex_name, nullptr,
     "str | None\n--\n\nName of the flex (None when it was not named through this API).", nullptr},
    {"index", Flex_index, nullptr, "int\n--\n\nIndex (id) of the flex in the model.", nullptr},
};

// The stored (name, group, min, max) of a controller: field `index` of the tuple.
PyObject *controller_field(PyObject *self, Py_ssize_t index) {
    return guarded([&]() -> PyObject * {
        Ref info(stored_value(owner_of<FlexControllerRef>(self), "controller", 0, view_of<FlexControllerRef>(self).id));
        if (info.get() == Py_None)
            Py_RETURN_NONE;
        PyObject *item = PyTuple_GetItem(info.get(), index);
        Py_XINCREF(item);
        return item;
    });
}

PyObject *Controller_name(PyObject *self, void *) { return controller_field(self, 0); }
PyObject *Controller_group(PyObject *self, void *) { return controller_field(self, 1); }
PyObject *Controller_min(PyObject *self, void *) { return controller_field(self, 2); }
PyObject *Controller_max(PyObject *self, void *) { return controller_field(self, 3); }

PyObject *Controller_index(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(view_of<FlexControllerRef>(self).id); });
}

PyObject *Controller_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        Ref name(controller_field(self, 0));
        if (!name)
            throw PyError{};
        if (name.get() == Py_None)
            return PyUnicode_FromFormat("<FlexController %u>",
                                        reinterpret_cast<ViewObject<FlexControllerRef> *>(self)->value().id);
        return PyUnicode_FromFormat("<FlexController %R>", name.get());
    });
}

const PyGetSetDef controller_getsets[] = {
    {"name", Controller_name, nullptr, "str | None\n--\n\nName of the controller.", nullptr},
    {"group", Controller_group, nullptr, "str | None\n--\n\nGroup (type) of the controller.", nullptr},
    {"min", Controller_min, nullptr, "float | None\n--\n\nLower end of the slider range.", nullptr},
    {"max", Controller_max, nullptr, "float | None\n--\n\nUpper end of the slider range.", nullptr},
    {"index", Controller_index, nullptr, "int\n--\n\nIndex (id) of the controller in the model.", nullptr},
};

// ---- FlexKey

PyObject *FlexKey_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const FlexKey &key = view_of<FlexKey>(self);
        Ref info(stored_value(owner_of<FlexKey>(self), "flexkey", scope_of(key.mesh), key.id));
        if (info.get() == Py_None)
            Py_RETURN_NONE;
        PyObject *name = PyTuple_GetItem(info.get(), 0);
        Py_XINCREF(name);
        return name;
    });
}

PyObject *FlexKey_flexes(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const FlexKey &key = view_of<FlexKey>(self);
        ModelObject *owner = owner_of<FlexKey>(self);
        Ref info(stored_value(owner, "flexkey", scope_of(key.mesh), key.id));
        if (info.get() == Py_None || !PyTuple_Check(info.get()))
            raise(PyExc_RuntimeError, "flex key is unknown");
        const std::string name = Conv<std::string>::from(PyTuple_GetItem(info.get(), 0), "name");
        if (PyObject_IsTrue(PyTuple_GetItem(info.get(), 1))) {
            const auto [right, left] = key.mesh.flexPairIds(key.id);
            Ref tuple = Ref::check(PyTuple_New(2));
            PyTuple_SetItem(tuple.get(), 0, flex_view(owner, right, name + "R"));
            PyTuple_SetItem(tuple.get(), 1, flex_view(owner, left, name + "L"));
            return tuple.release();
        }
        Ref tuple = Ref::check(PyTuple_New(1));
        PyTuple_SetItem(tuple.get(), 0, flex_view(owner, key.mesh.flexId(key.id), name));
        return tuple.release();
    });
}

const OptionDef flex_key_options[] = {
    {"position", "float | None\n--\n\n`position` of the flex key (default 1).",
     &item_setter<FlexKey, &F::setFlexPosition>},
    {"decay", "float | None\n--\n\n`decay` of the flex key (default 1).", &item_setter<FlexKey, &F::setFlexDecay>},
    {"split", "float | None\n--\n\n`split` of the flex key, replacing the split of a pair.",
     &item_setter<FlexKey, &F::setFlexSplit>},
};

const PyGetSetDef flex_key_getsets[] = {
    {"name", FlexKey_name, nullptr, "str | None\n--\n\nName of the flex (or pair) the key belongs to.", nullptr},
    {"index", Item_index<FlexKey>, nullptr, "int\n--\n\nKey index in the mesh (call order).", nullptr},
    {"mesh", Item_mesh<FlexKey>, nullptr, "Mesh\n--\n\nThe Mesh it belongs to.", nullptr},
    {"flexes", FlexKey_flexes, nullptr,
     "tuple[Flex, ...]\n--\n\nThe flexes the key defines: (Flex,) for a flex, (right, left) = (<name>R, <name>L) for "
     "a pair.",
     nullptr},
};

// ---- Eyelid / DmxEyelid / CombinationControl

PyObject *Eyelid_set_state(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_state", args, kwargs, {"state", "shape", "target"}, 3, 3);
        const Eyelid &eyelid = view_of<Eyelid>(self);
        const smdl::EyelidState state = a.get<smdl::EyelidState>(0);
        const uint32_t shape = shape_arg(owner_of<Eyelid>(self), eyelid.mesh.geometry(), a[1], "shape", true);
        eyelid.mesh.setEyelidState(eyelid.id, state, shape, a.get<float>(2));
        return none();
    });
}

PyObject *Eyelid_set_eyeball(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_eyeball", args, kwargs, {"eyeball"}, 0, 1);
        const Eyelid &eyelid = view_of<Eyelid>(self);
        const uint32_t eyeball = a[0] ? item_arg<Eyeball>(owner_of<Eyelid>(self), eyelid.mesh, a[0], "eyeball", true)
                                      : smdl::NoId;
        eyelid.mesh.setEyelidEyeball(eyelid.id, eyeball);
        return none();
    });
}

const OptionDef eyelid_options[] = {
    {"split", "float | None\n--\n\n`split` of the eyelid.", &item_setter<Eyelid, &F::setEyelidSplit>},
};

PyMethodDef eyelid_methods[] = {
    {"set_state", PYSMDL_METHOD(Eyelid_set_state), PYSMDL_KW,
     "set_state(state: EyelidState, shape: Shape | str | int | None, target: float) -> None\n--\n\nOne of the three "
     "states (EyelidState) of the eyelid: the shape (Shape, name, index, or None for the base shape) and the lid's "
     "target height, within +-radius of the eyeball. All three states are required."},
    {"set_eyeball", PYSMDL_METHOD(Eyelid_set_eyeball), PYSMDL_KW,
     "set_eyeball(eyeball: Eyeball | str | int | None = None) -> None\n--\n\n`eyeball <name>`: only this eyeball "
     "(Eyeball, its name or index); None: every eyeball (the default)."},
    {nullptr, nullptr, 0, nullptr},
};

PyObject *DmxEyelid_set_state(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_state", args, kwargs, {"state", "shape", "target"}, 3, 3);
        const DmxEyelid &eyelid = view_of<DmxEyelid>(self);
        const smdl::EyelidState state = a.get<smdl::EyelidState>(0);
        const uint32_t shape = shape_arg(owner_of<DmxEyelid>(self), eyelid.mesh.geometry(), a[1], "shape", false);
        eyelid.mesh.setDmxEyelidState(eyelid.id, state, shape, a.get<float>(2));
        return none();
    });
}

PyMethodDef dmx_eyelid_methods[] = {
    {"set_state", PYSMDL_METHOD(DmxEyelid_set_state), PYSMDL_KW,
     "set_state(state: EyelidState, shape: Shape | str | int, target: float) -> None\n--\n\nOne of the three states "
     "(EyelidState): a shape (Shape, name or index; no base shape) built as a DMX delta state, and the lid's target "
     "height. All three are required."},
    {nullptr, nullptr, 0, nullptr},
};

PyObject *Control_set_eyelid(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_eyelid", args, kwargs, {"eyes_up_down_flex"}, 0, 1);
        const CombinationControl &control = view_of<CombinationControl>(self);
        control.mesh.setCombinationControlEyelid(control.id, a.get<std::string>(0));
        return none();
    });
}

PyObject *Control_set_range(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_range", args, kwargs, {"min", "max"}, 2, 2);
        const CombinationControl &control = view_of<CombinationControl>(self);
        control.mesh.setCombinationControlRange(control.id, a.get<float>(0), a.get<float>(1));
        return none();
    });
}

PyMethodDef control_methods[] = {
    {"set_eyelid", PYSMDL_METHOD(Control_set_eyelid), PYSMDL_KW,
     "set_eyelid(eyes_up_down_flex: str | None = None) -> None\n--\n\nMake the control an eyelid control (exactly "
     "two raw controls: the lower lid's, then the upper lid's). The flex controller eyes_up_down_flex (default "
     "\"eyes_updown\") moves the lids with the eyes."},
    {"set_range", PYSMDL_METHOD(Control_set_range), PYSMDL_KW,
     "set_range(min: float, max: float) -> None\n--\n\nRange of the control's flex controller (default 0..1); only "
     "used together with combination flex rules (add_combination_expression and friends)."},
    {nullptr, nullptr, 0, nullptr},
};

// ---------------------------------------------------------------------------------------------------------------------
// Mesh methods

PyObject *Mesh_add_eyeball(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_eyeball", args, kwargs, {"name", "bone", "position", "material", "diameter", "z_angle", "pupil_scale"},
               5, 5);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        ModelObject *owner = owner_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        const smdl::BoneId bone = bone_arg(owner, a[1], "bone");
        const smdl::EyeballId id = mesh.addEyeball(name, bone, a.get<smdl::Vec3>(2), a.get<std::string>(3),
                                                   a.get<float>(4), a.get<float>(5, 0.0f), a.get<float>(6, 1.0f));
        return make_item<Eyeball>(owner, mesh, id, &name);
    });
}

PyObject *Mesh_add_flex(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_flex", args, kwargs, {"name", "shape"}, 2, 2);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        ModelObject *owner = owner_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        const uint32_t shape = shape_arg(owner, mesh.geometry(), a[1], "shape", true);
        const smdl::FlexKeyId key = mesh.addFlex(name, shape);
        Ref info = Ref::check(Py_BuildValue("(sO)", name.c_str(), Py_False));
        store_value(owner, FlexKeyTag::kind, scope_of(mesh), key, info.get());
        return make_item<FlexKey>(owner, mesh, key);
    });
}

PyObject *Mesh_add_flex_pair(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_flex_pair", args, kwargs, {"name", "split", "shape"}, 3, 3);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        ModelObject *owner = owner_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        const float split = a.get<float>(1);
        const uint32_t shape = shape_arg(owner, mesh.geometry(), a[2], "shape", true);
        const smdl::FlexKeyId key = mesh.addFlexPair(name, split, shape);
        Ref info = Ref::check(Py_BuildValue("(sO)", name.c_str(), Py_True));
        store_value(owner, FlexKeyTag::kind, scope_of(mesh), key, info.get());
        return make_item<FlexKey>(owner, mesh, key);
    });
}

PyObject *Mesh_set_default_flex(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_default_flex", args, kwargs, {"shape"}, 0, 1);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        const uint32_t shape = a[0] ? shape_arg(owner_of<smdl::Mesh>(self), mesh.geometry(), a[0], "shape", true)
                                    : smdl::NoId;
        mesh.setDefaultFlex(shape);
        return none();
    });
}

PyObject *Mesh_add_eyelid(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_eyelid", args, kwargs, {"name", "kind"}, 2, 2);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        const smdl::EyelidId id = mesh.addEyelid(name, a.get<smdl::EyelidKind>(1));
        return make_item<Eyelid>(owner_of<smdl::Mesh>(self), mesh, id, &name);
    });
}

PyObject *Mesh_add_dmx_eyelid(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_dmx_eyelid", args, kwargs, {"kind", "right_eyeball", "left_eyeball"}, 3, 3);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        ModelObject *owner = owner_of<smdl::Mesh>(self);
        const smdl::EyelidKind kind = a.get<smdl::EyelidKind>(0);
        const uint32_t right = item_arg<Eyeball>(owner, mesh, a[1], "right_eyeball", false);
        const uint32_t left = item_arg<Eyeball>(owner, mesh, a[2], "left_eyeball", false);
        return make_item<DmxEyelid>(owner, mesh, mesh.addDmxEyelid(kind, right, left));
    });
}

PyObject *Mesh_add_sphere_normals(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_sphere_normals", args, kwargs, {"material", "position"}, 2, 2);
        view_of<smdl::Mesh>(self).addSphereNormals(a.get<std::string>(0), a.get<smdl::Vec3>(1));
        return none();
    });
}

PyObject *Mesh_set_no_auto_dmx_rules(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_no_auto_dmx_rules", args, kwargs, {"enabled"}, 0, 1);
        view_of<smdl::Mesh>(self).setNoAutoDmxRules(a.get<bool>(0, true));
        return none();
    });
}

std::vector<std::string_view> views_of(const std::vector<std::string> &strings) {
    return std::vector<std::string_view>(strings.begin(), strings.end());
}

PyObject *Mesh_add_combination_control(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_combination_control", args, kwargs, {"name", "raw_controls", "stereo"}, 2, 3);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        const std::vector<std::string> raw = string_list(a[1], "raw_controls");
        const std::vector<std::string_view> raw_views = views_of(raw);
        const smdl::CombinationControlId id = mesh.addCombinationControl(
            name, std::span<const std::string_view>(raw_views), a.get<bool>(2, false));
        return make_item<CombinationControl>(owner_of<smdl::Mesh>(self), mesh, id, &name);
    });
}

PyObject *Mesh_add_combination_dominator(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_combination_dominator", args, kwargs, {"dominators", "suppressed"}, 2, 2);
        const std::vector<std::string> dominators = string_list(a[0], "dominators");
        const std::vector<std::string> suppressed = string_list(a[1], "suppressed");
        const std::vector<std::string_view> dominator_views = views_of(dominators);
        const std::vector<std::string_view> suppressed_views = views_of(suppressed);
        view_of<smdl::Mesh>(self).addCombinationDominator(std::span<const std::string_view>(dominator_views),
                                                          std::span<const std::string_view>(suppressed_views));
        return none();
    });
}

PyObject *Mesh_add_combination_expression(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_combination_expression", args, kwargs, {"flex", "expression"}, 2, 2);
        view_of<smdl::Mesh>(self).addCombinationExpression(a.get<std::string>(0), a.get<std::string>(1));
        return none();
    });
}

PyObject *Mesh_add_combination_pass_through(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_combination_pass_through", args, kwargs, {"flex"}, 1, 1);
        view_of<smdl::Mesh>(self).addCombinationPassThrough(a.get<std::string>(0));
        return none();
    });
}

PyObject *Mesh_add_combination_local_var(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_combination_local_var", args, kwargs, {"name"}, 1, 1);
        view_of<smdl::Mesh>(self).addCombinationLocalVar(a.get<std::string>(0));
        return none();
    });
}

PyObject *Mesh_add_vertex_cache_animation(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_vertex_cache_animation", args, kwargs, {"name", "shapes"}, 2, 2);
        smdl::Mesh &mesh = view_of<smdl::Mesh>(self);
        ModelObject *owner = owner_of<smdl::Mesh>(self);
        const std::string name = a.get<std::string>(0);
        if (PyUnicode_Check(a[1]))
            raise(PyExc_TypeError, "shapes must be a sequence of shapes, not a single str");
        Ref iterator(PyObject_GetIter(a[1]));
        if (!iterator) {
            PyErr_Clear();
            raise(PyExc_TypeError, "shapes must be a sequence of Shape, names or indices, not %.100s", type_name(a[1]));
        }
        const smdl::Geometry geometry = mesh.geometry();
        std::vector<smdl::ShapeId> shapes;
        while (PyObject *item = PyIter_Next(iterator.get())) {
            Ref owned(item);
            shapes.push_back(shape_arg(owner, geometry, item, "shapes", false));
        }
        if (PyErr_Occurred())
            throw PyError{};
        mesh.addVertexCacheAnimation(name, std::span<const smdl::ShapeId>(shapes));
        return none();
    });
}

const PyMethodDef mesh_methods[] = {
    {"add_eyeball", PYSMDL_METHOD(Mesh_add_eyeball), PYSMDL_KW,
     "add_eyeball(name: str, bone: BoneLike, position: Vec3, material: str, diameter: float, *, z_angle: float = "
     "0.0, pupil_scale: float = 1.0) -> Eyeball\n--\n\n`eyeball`: bone (Bone or name), position in model space (x, "
     "y, z), material = a material of the geometry, diameter, z_angle in degrees. At most 4 per mesh. Returns the "
     "Eyeball."},
    {"add_flex", PYSMDL_METHOD(Mesh_add_flex), PYSMDL_KW,
     "add_flex(name: str, shape: Shape | str | int | None) -> FlexKey\n--\n\n`flex <name> frame <shape>`: a flex "
     "from a shape (Shape, name, index, or None for the base shape). Consecutive calls with the same name make one "
     "multi-position flex. Returns the FlexKey; its position / decay / split properties set the matching options."},
    {"add_flex_pair", PYSMDL_METHOD(Mesh_add_flex_pair), PYSMDL_KW,
     "add_flex_pair(name: str, split: float, shape: Shape | str | int | None) -> FlexKey\n--\n\n`flexpair <name> "
     "<split> frame <shape>`: the stereo pair <name>R / <name>L split at x = split (non-zero). Returns the "
     "FlexKey."},
    {"set_default_flex", PYSMDL_METHOD(Mesh_set_default_flex), PYSMDL_KW,
     "set_default_flex(shape: Shape | str | int | None = None) -> None\n--\n\n`defaultflex frame <shape>`: the "
     "model's default pose is this shape (Shape, name, index; None: the base shape). Once per model."},
    {"add_eyelid", PYSMDL_METHOD(Mesh_add_eyelid), PYSMDL_KW,
     "add_eyelid(name: str, kind: EyelidKind) -> Eyelid\n--\n\n`eyelid`: kind is an EyelidKind. Set the three "
     "states with Eyelid.set_state. Returns the Eyelid."},
    {"add_dmx_eyelid", PYSMDL_METHOD(Mesh_add_dmx_eyelid), PYSMDL_KW,
     "add_dmx_eyelid(kind: EyelidKind, right_eyeball: Eyeball | str | int, left_eyeball: Eyeball | str | int) -> "
     "DmxEyelid\n--\n\n`dmxeyelid`: kind is an EyelidKind, the eyeballs Eyeballs (or their names / indices). Set the "
     "three states with DmxEyelid.set_state. Returns the DmxEyelid."},
    {"add_sphere_normals", PYSMDL_METHOD(Mesh_add_sphere_normals), PYSMDL_KW,
     "add_sphere_normals(material: str, position: Vec3) -> None\n--\n\n`spherenormals`: the normals of the vertices "
     "of that material point away from position (x, y, z in model space)."},
    {"set_no_auto_dmx_rules", PYSMDL_METHOD(Mesh_set_no_auto_dmx_rules), PYSMDL_KW,
     "set_no_auto_dmx_rules(enabled: bool = True) -> None\n--\n\n`noautodmxrules`: no automatic controllers and "
     "rules for the DMX flexes of the source."},
    {"add_combination_control", PYSMDL_METHOD(Mesh_add_combination_control), PYSMDL_KW,
     "add_combination_control(name: str, raw_controls: StrSequence, stereo: bool = False) -> "
     "CombinationControl\n--\n\nDMX combination operator: a control (flex controller `name`) driven by the raw "
     "controls (names) the geometry's shapes are named after (a shape `a_b_c` combines the raw controls a, b, c). "
     "stereo makes right_<name> / left_<name> controllers and L/R flexes. Without raw controls the control only adds "
     "its controller for combination flex rules (the compile refuses it on a mesh without rules). Returns the "
     "CombinationControl."},
    {"add_combination_dominator", PYSMDL_METHOD(Mesh_add_combination_dominator), PYSMDL_KW,
     "add_combination_dominator(dominators: StrSequence, suppressed: StrSequence) -> None\n--\n\nDomination "
     "rule: while the dominators (raw control names) are on, the combined shapes containing all suppressed raw "
     "controls are suppressed."},
    {"add_combination_expression", PYSMDL_METHOD(Mesh_add_combination_expression), PYSMDL_KW,
     "add_combination_expression(flex: str, expression: str) -> None\n--\n\n\"Advanced flex\" (DmeFlexRules): the "
     "flex's rule is this expression (the text of a QC `%flex = <expression>`)."},
    {"add_combination_pass_through", PYSMDL_METHOD(Mesh_add_combination_pass_through), PYSMDL_KW,
     "add_combination_pass_through(flex: str) -> None\n--\n\nThe flex's rule is the value of the control of the "
     "same name."},
    {"add_combination_local_var", PYSMDL_METHOD(Mesh_add_combination_local_var), PYSMDL_KW,
     "add_combination_local_var(name: str) -> None\n--\n\nA flex without vertex data that expressions can read."},
    {"add_vertex_cache_animation", PYSMDL_METHOD(Mesh_add_vertex_cache_animation), PYSMDL_KW,
     "add_vertex_cache_animation(name: str, shapes: Iterable[Shape | str | int]) -> None\n--\n\n`vcafile`: shapes "
     "(Shape, name or index each, in frame order) are the frames after frame 0 (the base pose) of a vertex cache "
     "animation played by one n-way flex controller `name`; it must be the only flex option of the model."},
};

// ---------------------------------------------------------------------------------------------------------------------
// Model methods

PyObject *add_flex_controller(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_flex_controller", args, kwargs, {"name", "group", "min", "max"}, 1, 4);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        const std::string group = a.get<std::string>(1, "default");
        const float min = a.get<float>(2, 0.0f);
        const float max = a.get<float>(3, 1.0f);
        const smdl::FlexControllerId id = model_of(model).addFlexController(group, name, min, max);
        Ref info = Ref::check(Py_BuildValue("(ssff)", name.c_str(), group.c_str(), (double) min, (double) max));
        store_value(model, "controller", 0, id, info.get());
        return make_view(model, FlexControllerRef{id});
    });
}

PyObject *find_flex_controller(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_flex_controller", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        return make_view(model, FlexControllerRef{model_of(model).findFlexController(a.get<std::string>(0))});
    });
}

PyObject *add_local_var(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_local_var", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        return flex_view(model, model_of(model).addLocalVar(name), name);
    });
}

PyObject *find_flex(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("find_flex", args, kwargs, {"name"}, 1, 1);
        ModelObject *model = model_ptr(self);
        const std::string name = a.get<std::string>(0);
        return flex_view(model, model_of(model).findFlex(name), name);
    });
}

PyObject *add_flex_rule(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_flex_rule", args, kwargs, {"flex", "expression"}, 2, 2);
        ModelObject *model = model_ptr(self);
        const smdl::FlexId flex = flex_arg(model, a[0], "flex");
        model_of(model).addFlexRule(flex, a.get<std::string>(1));
        return none();
    });
}

PyObject *set_mouth(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("set_mouth", args, kwargs, {"index", "flex", "bone", "forward"}, 4, 4);
        ModelObject *model = model_ptr(self);
        std::string flex;
        if (PyUnicode_Check(a[1])) {
            flex = Conv<std::string>::from(a[1], "flex");
        } else {
            const smdl::FlexId id = view_arg<FlexRef>(model, a[1], "flex").id;
            Ref name(stored_value(model, "name:flex", 0, id));
            if (name.get() == Py_None)
                raise(PyExc_ValueError, "flex has no known name: pass the flex name");
            flex = Conv<std::string>::from(name.get(), "flex");
        }
        const smdl::BoneId bone = bone_arg(model, a[2], "bone");
        model_of(model).setMouth(a.get<uint32_t>(0), flex, bone, a.get<smdl::Vec3>(3));
        return none();
    });
}

PyObject *add_bone_flex_driver(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("add_bone_flex_driver", args, kwargs, {"bone", "component", "controller", "min", "max"}, 5, 5);
        ModelObject *model = model_ptr(self);
        const smdl::BoneId bone = bone_arg(model, a[0], "bone");
        const auto component = a.get<smdl::BoneFlexComponent>(1);
        const smdl::FlexControllerId controller = controller_arg(model, a[2], "controller");
        model_of(model).addBoneFlexDriver(bone, component, controller, a.get<float>(3), a.get<float>(4));
        return none();
    });
}

PyObject *Model_flex_controller_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return PyLong_FromUnsignedLong(model_of(model_ptr(self)).flexControllerCount());
    });
}

PyObject *Model_flex_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * { return PyLong_FromUnsignedLong(model_of(model_ptr(self)).flexCount()); });
}

const PyGetSetDef model_getsets[] = {
    {"flex_controller_count", Model_flex_controller_count, nullptr, "int\n--\n\nNumber of flex controllers.",
     nullptr},
    {"flex_count", Model_flex_count, nullptr,
     "int\n--\n\nNumber of flexes known to the model (including local variables).", nullptr},
};

const PyMethodDef model_methods[] = {
    {"add_flex_controller", PYSMDL_METHOD(add_flex_controller), PYSMDL_KW,
     "add_flex_controller(name: str, group: str = 'default', min: float = 0.0, max: float = 1.0) -> "
     "FlexController\n--\n\n`flexcontroller <group> range <min> <max> <name>`: a slider. Names are unique. Returns "
     "the FlexController."},
    {"find_flex_controller", PYSMDL_METHOD(find_flex_controller), PYSMDL_KW,
     "find_flex_controller(name: str) -> FlexController\n--\n\nThe FlexController with this name; raises "
     "NotFoundError."},
    {"add_local_var", PYSMDL_METHOD(add_local_var), PYSMDL_KW,
     "add_local_var(name: str) -> Flex\n--\n\n`localvar`: a flex without vertex data that rules can read. Returns "
     "the Flex."},
    {"find_flex", PYSMDL_METHOD(find_flex), PYSMDL_KW,
     "find_flex(name: str) -> Flex\n--\n\nThe Flex with this name; raises NotFoundError."},
    {"add_flex_rule", PYSMDL_METHOD(add_flex_rule), PYSMDL_KW,
     "add_flex_rule(flex: Flex | str, expression: str) -> None\n--\n\n`%flex = expression`: flex is a Flex or its "
     "name; expression is the right-hand side of the QC line (numbers, controller names, %flex, + - * /, min(), "
     "max()). Raises ExpressionSyntaxError / NotFoundError."},
    {"set_mouth", PYSMDL_METHOD(set_mouth), PYSMDL_KW,
     "set_mouth(index: int, flex: Flex | str, bone: BoneLike, forward: Vec3) -> None\n--\n\n`mouth <index> <flex> "
     "<bone> <x> <y> <z>`: the flex (a name, or a Flex) opens the mouth by the bone's direction forward (x, y, z in "
     "the bone's space). index < 64."},
    {"add_bone_flex_driver", PYSMDL_METHOD(add_bone_flex_driver), PYSMDL_KW,
     "add_bone_flex_driver(bone: BoneLike, component: BoneFlexComponent, controller: FlexController | str | int, "
     "min: float, max: float) -> None\n--\n\n$boneflexdriver: the bone's translation (component: "
     "BoneFlexComponent) follows the controller (a FlexController, its name or index) over min..max."},
};

int init(PyObject *module) {
    if (register_enum<smdl::BoneFlexComponent>(module, "BoneFlexComponent",
                                               {{"TX", SMDL_BONE_FLEX_TX}, {"TY", SMDL_BONE_FLEX_TY},
                                                {"TZ", SMDL_BONE_FLEX_TZ}}) < 0)
        return -1;
    if (register_enum<smdl::EyelidKind>(module, "EyelidKind",
                                        {{"UPPER", SMDL_EYELID_UPPER}, {"LOWER", SMDL_EYELID_LOWER}}) < 0)
        return -1;
    if (register_enum<smdl::EyelidState>(module, "EyelidState",
                                         {{"LOWERER", SMDL_EYELID_LOWERER}, {"NEUTRAL", SMDL_EYELID_NEUTRAL},
                                          {"RAISER", SMDL_EYELID_RAISER}}) < 0)
        return -1;

    if (add_view_type<FlexRef>(module, "Flex",
                               "A flex of the model (from FlexKey.flexes / Model.add_local_var / find_flex): the "
                               "target of a flex rule.",
                               nullptr,
                               option_getsets({}, {flex_getsets}, view_option_key<FlexRef>, view_option_owner<FlexRef>),
                               Flex_repr) < 0)
        return -1;
    if (add_view_type<FlexControllerRef>(module, "FlexController",
                                         "A flex controller (slider) of the model (from Model.add_flex_controller / "
                                         "find_flex_controller).",
                                         nullptr,
                                         option_getsets({}, {controller_getsets}, view_option_key<FlexControllerRef>,
                                                        view_option_owner<FlexControllerRef>),
                                         Controller_repr) < 0)
        return -1;

    if (add_view_type<Eyeball>(module, "Eyeball", "An eyeball of a Mesh (from Mesh.add_eyeball).", nullptr,
                               option_getsets({}, {item_getsets<Eyeball>(true)}, view_option_key<Eyeball>,
                                              view_option_owner<Eyeball>),
                               Item_repr<Eyeball>) < 0)
        return -1;
    if (add_view_type<FlexKey>(module, "FlexKey",
                               "A flex or flex pair of a Mesh (from Mesh.add_flex / add_flex_pair). Properties set the "
                               "`position`, `decay` and `split` options and read back the value last assigned.",
                               nullptr,
                               option_getsets({flex_key_options}, {flex_key_getsets}, view_option_key<FlexKey>,
                                              view_option_owner<FlexKey>),
                               Item_repr<FlexKey>) < 0)
        return -1;
    if (add_view_type<Eyelid>(module, "Eyelid", "An eyelid of a Mesh (from Mesh.add_eyelid).", eyelid_methods,
                              option_getsets({eyelid_options}, {item_getsets<Eyelid>(true)}, view_option_key<Eyelid>,
                                             view_option_owner<Eyelid>),
                              Item_repr<Eyelid>) < 0)
        return -1;
    if (add_view_type<DmxEyelid>(module, "DmxEyelid", "A DMX eyelid of a Mesh (from Mesh.add_dmx_eyelid).",
                                 dmx_eyelid_methods,
                                 option_getsets({}, {item_getsets<DmxEyelid>(false)}, view_option_key<DmxEyelid>,
                                                view_option_owner<DmxEyelid>),
                                 Item_repr<DmxEyelid>) < 0)
        return -1;
    return add_view_type<CombinationControl>(
        module, "CombinationControl", "A control of a Mesh's DMX combination operator (from Mesh.add_combination_control).",
        control_methods,
        option_getsets({}, {item_getsets<CombinationControl>(true)}, view_option_key<CombinationControl>,
                       view_option_owner<CombinationControl>),
        Item_repr<CombinationControl>);
}

} // namespace

const Part part_flex = {
    {},
    model_methods,
    {},
    model_getsets,
    init,
    mesh_methods,
};

} // namespace pysmdl
