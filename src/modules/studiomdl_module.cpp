//
// pylib.studiomdl: the studiomdl_v2 model compiler, linked statically (studiomdl::static) and used through its C++ API.
// This file defines the module, Model and Result; the parts (modules/studiomdl/*.cpp) add the rest.
//

#include "modules/studiomdl_module.h"
#include "modules/studiomdl/bind.h"
#include "utils/utils.h"

#include <exception>
#include <memory>

namespace pysmdl {

PyTypeObject *ModelType = nullptr;
static PyTypeObject *ResultType = nullptr;
static PyObject *MessageType = nullptr;  // collections.namedtuple("Message", "severity text")

static const Part *const parts[] = {
    &part_globals,
    &part_skeleton,
    &part_geometry,
    &part_flex,
    &part_rig,
    &part_animation,
    &part_collision,
    &part_bone_dynamics,
    &part_qc,
};

PyMethodDef *collected_mesh_methods() {
    static std::vector<PyMethodDef> methods;
    if (methods.empty()) {
        for (const Part *part: parts)
            methods.insert(methods.end(), part->mesh_methods.begin(), part->mesh_methods.end());
        methods.push_back({nullptr, nullptr, 0, nullptr});
    }
    return methods.data();
}

smdl::Model &model_of(ModelObject *self) {
    if (self->busy)
        raise(PyExc_RuntimeError, "the model is being compiled");
    return *self->model;
}

// ---------------------------------------------------------------------------------------------------------------------
// Messages and results

PyObject *new_message(smdl::Severity severity, std::string_view text) {
    Ref level(Conv<smdl::Severity>::to(severity));
    if (!level)
        return nullptr;
    Ref string(PyUnicode_DecodeUTF8(text.data(), (Py_ssize_t) text.size(), "replace"));
    if (!string)
        return nullptr;
    return PyObject_CallFunctionObjArgs(MessageType, level.get(), string.get(), nullptr);
}

PyObject *message_list(const std::vector<std::pair<smdl::Severity, std::string>> &messages) {
    Ref list(PyList_New(0));
    if (!list)
        return nullptr;
    for (const auto &[severity, text]: messages) {
        Ref message(new_message(severity, text));
        if (!message || PyList_Append(list.get(), message.get()) < 0)
            return nullptr;
    }
    return list.release();
}

struct ResultObject {
    PyObject_HEAD
    smdl::Result *result;
};

PyObject *new_result(smdl::Result &&result) {
    auto *self = reinterpret_cast<ResultObject *>(PyType_GenericAlloc(ResultType, 0));
    if (!self)
        throw PyError{};
    self->result = new smdl::Result(std::move(result));
    return reinterpret_cast<PyObject *>(self);
}

static void Result_dealloc(PyObject *self) {
    delete reinterpret_cast<ResultObject *>(self)->result;
    PyTypeObject *type = Py_TYPE(self);
    reinterpret_cast<freefunc>(PyType_GetSlot(type, Py_tp_free))(self);
    Py_DECREF(type);
}

static PyObject *Result_files(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        Ref dict = Ref::check(PyDict_New());
        for (const smdl::OutputFile &file: reinterpret_cast<ResultObject *>(self)->result->files()) {
            Ref data = Ref::check(PyBytes_FromStringAndSize(reinterpret_cast<const char *>(file.data.data()),
                                                             (Py_ssize_t) file.data.size()));
            Ref path = Ref::check(PyUnicode_DecodeUTF8(file.relativePath.data(),
                                                       (Py_ssize_t) file.relativePath.size(), "replace"));
            if (PyDict_SetItem(dict.get(), path.get(), data.get()) < 0)
                throw PyError{};
        }
        return dict.release();
    });
}

static PyObject *Result_messages(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        Ref list = Ref::check(PyList_New(0));
        for (const smdl::Message &message: reinterpret_cast<ResultObject *>(self)->result->messages()) {
            Ref item = Ref::check(new_message(message.severity, message.text));
            if (PyList_Append(list.get(), item.get()) < 0)
                throw PyError{};
        }
        return list.release();
    });
}

static PyObject *Result_warning_count(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        return PyLong_FromLong(reinterpret_cast<ResultObject *>(self)->result->warningCount());
    });
}

static PyObject *Result_save(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("save", args, kwargs, {"game_dir"}, 1, 1);
        const std::string dir = fs_path(a[0], "game_dir", false);
        const smdl::Result &result = *reinterpret_cast<ResultObject *>(self)->result;
        std::exception_ptr error;
        Py_BEGIN_ALLOW_THREADS
        try {
            result.save(dir);
        } catch (...) {
            error = std::current_exception();
        }
        Py_END_ALLOW_THREADS
        if (error)
            std::rethrow_exception(error);
        return none();
    });
}

static PyObject *Result_save_to(PyObject *self, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("save_to", args, kwargs, {"directory"}, 1, 1);
        const std::string dir = fs_path(a[0], "directory", false);
        const smdl::Result &result = *reinterpret_cast<ResultObject *>(self)->result;
        std::exception_ptr error;
        Py_BEGIN_ALLOW_THREADS
        try {
            result.saveTo(dir);
        } catch (...) {
            error = std::current_exception();
        }
        Py_END_ALLOW_THREADS
        if (error)
            std::rethrow_exception(error);
        return none();
    });
}

static PyObject *Result_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const smdl::Result &result = *reinterpret_cast<ResultObject *>(self)->result;
        return PyUnicode_FromFormat("<Result: %zu files, %d warnings>", result.files().size(), result.warningCount());
    });
}

static PyGetSetDef Result_getsets[] = {
    {"files", Result_files, nullptr,
     "dict[str, bytes]\n--\n\nCompiled files: path relative to the game directory (\"models/props/crate.mdl\") to "
     "contents.", nullptr},
    {"messages", Result_messages, nullptr, "list[Message]\n--\n\nCompiler output.", nullptr},
    {"warning_count", Result_warning_count, nullptr, "int\n--\n\nNumber of warnings.", nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr},
};

static PyMethodDef Result_methods[] = {
    {"save", PYSMDL_METHOD(Result_save), PYSMDL_KW,
     "save(game_dir: StrPath) -> None\n--\n\nWrite the files under game_dir (game_dir/models/...). Raises "
     "StudioMdlIOError."},
    {"save_to", PYSMDL_METHOD(Result_save_to), PYSMDL_KW,
     "save_to(directory: StrPath) -> None\n--\n\nWrite the files as directory/models/<name>.*. Raises "
     "StudioMdlIOError."},
    {nullptr, nullptr, 0, nullptr},
};

static PyType_Slot Result_slots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(Result_dealloc)},
    {Py_tp_repr, reinterpret_cast<void *>(Result_repr)},
    {Py_tp_getset, Result_getsets},
    {Py_tp_methods, Result_methods},
    {Py_tp_doc, const_cast<char *>("Output of a successful compile: the files in memory and the compiler messages.")},
    {0, nullptr},
};

static PyType_Spec Result_spec = {"pylib.studiomdl.Result", sizeof(ResultObject), 0,
                                  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, Result_slots};

// ---------------------------------------------------------------------------------------------------------------------
// Model

static PyObject *Model_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    return guarded([&]() -> PyObject * {
        Args a("Model", args, kwargs, {"name"}, 1, 1);
        const std::string name = a.get<std::string>(0);
        Ref self(PyType_GenericAlloc(type, 0));
        if (!self)
            throw PyError{};
        auto *model = reinterpret_cast<ModelObject *>(self.get());
        model->options = PyDict_New();
        if (!model->options)
            throw PyError{};
        model->bone_names = new std::vector<std::string>();
        model->bone_parents = new std::vector<smdl::BoneId>();
        model->model = new smdl::Model(name);
        return self.release();
    });
}

static void Model_dealloc(PyObject *self) {
    auto *model = reinterpret_cast<ModelObject *>(self);
    delete model->model;
    delete model->bone_names;
    delete model->bone_parents;
    Py_XDECREF(model->options);
    PyTypeObject *type = Py_TYPE(self);
    reinterpret_cast<freefunc>(PyType_GetSlot(type, Py_tp_free))(self);
    Py_DECREF(type);
}

static PyObject *Model_repr(PyObject *self) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = model_of(reinterpret_cast<ModelObject *>(self)).name();
        return PyUnicode_FromFormat("<Model '%s'>", std::string(name).c_str());
    });
}

static PyObject *Model_get_name(PyObject *self, void *) {
    return guarded([&]() -> PyObject * {
        const std::string_view name = model_of(reinterpret_cast<ModelObject *>(self)).name();
        return PyUnicode_DecodeUTF8(name.data(), (Py_ssize_t) name.size(), "replace");
    });
}

static int Model_set_name(PyObject *self, PyObject *value, void *) {
    return guarded([&]() -> int {
        if (!value)
            raise(PyExc_AttributeError, "can't delete attribute 'name'");
        model_of(reinterpret_cast<ModelObject *>(self)).setName(Conv<std::string>::from(value, "name"));
        return 0;
    });
}

static PyObject *Model_compile(PyObject *self, PyObject *args, PyObject *kwargs) {
    auto *model = reinterpret_cast<ModelObject *>(self);
    std::vector<std::pair<smdl::Severity, std::string>> log;
    std::exception_ptr error;
    std::optional<smdl::Result> result;
    PyObject *output = guarded([&]() -> PyObject * {
        Args a("compile", args, kwargs, {"game_dir"}, 0, 1);
        const std::string game_dir = a.has(0) ? fs_path(a[0], "game_dir") : std::string();
        const smdl::Model &cpp = model_of(model);
        model->busy = true;
        Py_BEGIN_ALLOW_THREADS
        try {
            result.emplace(cpp.compile(game_dir, [&log](smdl::Severity severity, std::string_view text) {
                log.emplace_back(severity, std::string(text));
            }));
        } catch (...) {
            error = std::current_exception();
        }
        Py_END_ALLOW_THREADS
        model->busy = false;
        if (error)
            return nullptr;
        return new_result(std::move(*result));
    });
    if (!error)
        return output;
    Ref messages(message_list(log));
    if (!messages)
        return nullptr;
    try {
        std::rethrow_exception(error);
    } catch (...) {
        translate_current_exception(messages.get());
    }
    return nullptr;
}

static const PyMethodDef Model_core_methods[] = {
    {"compile", PYSMDL_METHOD(Model_compile), PYSMDL_KW,
     "compile(game_dir: StrPath | None = None) -> Result\n--\n\n"
     "Compile the model in memory and return a Result (files and messages); nothing is written. game_dir: a game "
     "directory with gameinfo.txt, used to look up materials and $includemodel files (None: no game directory). "
     "Runs without the GIL; the model can't be used from other threads meanwhile. Raises CompileError (with the "
     "compiler messages) when the compile fails."},
};

static const PyGetSetDef Model_core_getsets[] = {
    {"name", Model_get_name, Model_set_name, "str\n--\n\nPath under models/ without extension (\"props/crate\").",
     nullptr},
};

static int init_model_type(PyObject *module) {
    static std::vector<PyMethodDef> methods;
    std::vector<std::span<const OptionDef>> options;
    std::vector<std::span<const PyGetSetDef>> getsets = {Model_core_getsets};
    methods.assign(std::begin(Model_core_methods), std::end(Model_core_methods));
    for (const Part *part: parts) {
        methods.insert(methods.end(), part->model_methods.begin(), part->model_methods.end());
        options.push_back(part->model_options);
        getsets.push_back(part->model_getsets);
    }
    methods.push_back({nullptr, nullptr, 0, nullptr});

    // option_getsets takes initializer lists; fold the per-part lists into one each.
    static std::vector<OptionDef> all_options;
    static std::vector<PyGetSetDef> all_getsets;
    for (const auto &list: options)
        all_options.insert(all_options.end(), list.begin(), list.end());
    for (const auto &list: getsets)
        all_getsets.insert(all_getsets.end(), list.begin(), list.end());
    PyGetSetDef *getset_defs = option_getsets({std::span<const OptionDef>(all_options)},
                                              {std::span<const PyGetSetDef>(all_getsets)}, model_option_key,
                                              model_option_owner);
    if (typed_members("Model", methods.data(), getset_defs) < 0)
        return -1;
    static PyType_Slot slots[] = {
        {Py_tp_new, reinterpret_cast<void *>(Model_new)},
        {Py_tp_dealloc, reinterpret_cast<void *>(Model_dealloc)},
        {Py_tp_repr, reinterpret_cast<void *>(Model_repr)},
        {Py_tp_methods, nullptr},
        {Py_tp_getset, nullptr},
        {Py_tp_doc, const_cast<char *>(
            "Model(name)\n--\n\n"
            "A model built in memory and compiled with compile(). name: path under models/ without extension "
            "(\"props/crate\").\n\n"
            "Settings are properties; studiomdl can't report them back, so reading one returns the value last "
            "assigned (None: not assigned, studiomdl's default). Bones, meshes, body groups and the other parts are "
            "objects created by the add_*/create_* methods; they keep the model alive.")},
        {0, nullptr},
    };
    slots[3].pfunc = methods.data();
    slots[4].pfunc = getset_defs;
    static PyType_Spec spec = {"pylib.studiomdl.Model", sizeof(ModelObject), 0, Py_TPFLAGS_DEFAULT, slots};
    return add_type(module, "Model", &spec, &ModelType);
}

// Typing for members CPython can't document, and the aliases the annotations use.
static int add_extra_typing(PyObject *module) {
    static const char header[] =
        "import collections.abc as _abc\n"
        "import os\n"
        "from collections.abc import Buffer, Iterable, Mapping\n"
        "\n"
        "# `Sequence` below is the animation sequence class; abstract sequences use _abc.Sequence.\n"
        "StrPath = str | os.PathLike[str]\n"
        "StrSequence = _abc.Sequence[str]\n"
        "# Fixed-size vectors: any sequence of numbers (tuple, list, mathutils.Vector, numpy array)\n"
        "Vec2 = _abc.Sequence[float]\n"
        "Vec3 = _abc.Sequence[float]\n"
        "Vec4 = _abc.Sequence[float]\n"
        "Quat = _abc.Sequence[float]  # (x, y, z, w)\n"
        "# Rows of numbers: float32 / 32 bit integer buffers (numpy) are read without copying; other buffers and\n"
        "# sequences (of rows or flat) are copied\n"
        "FloatArray = Buffer | Iterable[_abc.Sequence[float]] | Iterable[float]\n"
        "IntArray = Buffer | Iterable[_abc.Sequence[int]] | Iterable[int]\n"
        "BoneLike = Bone | str\n";
    if (PyModule_AddStringConstant(module, "__pyi_header__", header) < 0)
        return -1;
    try {
        add_typing("StudioMdlError.status", "int");
        add_typing("StudioMdlError.object", "str");
        add_typing("StudioMdlError.messages", "list[Message]");
        add_typing("QcError.file", "str");
        add_typing("QcError.line", "int");
        add_typing("Message.severity", "Severity");
        add_typing("Message.text", "str");
        add_typing("API_VERSION", "int");
        add_typing("BONE_CONTROLLER_MOUTH", "int");
        add_typing("Model.__new__", "(name: str) -> Model");
    } catch (const PyError &) {
        return -1;
    }
    return 0;
}

static int init_module(PyObject *module) {
    if (init_typing(module) < 0 || init_errors(module) < 0)
        return -1;
    if (register_enum<smdl::Severity>(module, "Severity", {{"MESSAGE", SMDL_SEVERITY_MESSAGE},
                                                            {"WARNING", SMDL_SEVERITY_WARNING},
                                                            {"ERROR", SMDL_SEVERITY_ERROR}}) < 0)
        return -1;
    Ref collections(PyImport_ImportModule("collections"));
    if (!collections)
        return -1;
    Ref kwargs(Py_BuildValue("{ss}", "module", "pylib.studiomdl"));
    Ref fields(Py_BuildValue("(ss)", "severity", "text"));
    Ref factory(PyObject_GetAttrString(collections.get(), "namedtuple"));
    if (!kwargs || !fields || !factory)
        return -1;
    Ref call_args(Py_BuildValue("(sO)", "Message", fields.get()));
    if (!call_args)
        return -1;
    Py_XDECREF(MessageType);
    MessageType = PyObject_Call(factory.get(), call_args.get(), kwargs.get());
    if (!MessageType || PyModule_AddObjectRef(module, "Message", MessageType) < 0)
        return -1;
    if (typed_members("Result", Result_methods, Result_getsets) < 0 ||
        add_type(module, "Result", &Result_spec, &ResultType) < 0)
        return -1;

    static std::vector<PyMethodDef> module_methods;
    for (const Part *part: parts) {
        if (part->init && part->init(module) < 0)
            return -1;
        module_methods.insert(module_methods.end(), part->module_methods.begin(), part->module_methods.end());
    }
    module_methods.push_back({nullptr, nullptr, 0, nullptr});
    if (typed_members("", module_methods.data(), nullptr) < 0 ||
        PyModule_AddFunctions(module, module_methods.data()) < 0)
        return -1;
    if (init_model_type(module) < 0 || add_extra_typing(module) < 0)
        return -1;
    return PyModule_AddIntConstant(module, "API_VERSION", SMDL_API_VERSION);
}

} // namespace pysmdl

static struct PyModuleDef studiomdl_module_def = {
    PyModuleDef_HEAD_INIT,
    "pylib.studiomdl",
    "Compile Source 1 models (MDL/VVD/VTX/PHY) in process with studiomdl_v2.\n\n"
    "Build a Model in code and compile() it, or compile an existing QC with compile_qc(). Both return a Result with "
    "the files in memory.",
    -1,
    nullptr
};

PyObject *StudioMdlModule_Init(PyObject *parent_module) {
    PyObject *module = add_submodule(parent_module, "studiomdl", &studiomdl_module_def);
    if (!module || pysmdl::init_module(module) < 0)
        return nullptr;
    return module;
}
