//
// pylib.studiomdl: compiling an existing QC / .mpp file (smdl::QcCompile, smdl_qc.h).
//

#include "modules/studiomdl/bind.h"

#include <exception>

namespace pysmdl {

namespace {

enum Param : size_t {
    QC_PATH,
    GAME_DIR,
    FILES,
    WRITE_TO_GAME_DIR,
    NO_WARNINGS,
    MAX_WARNINGS,
    FIT_WEIGHTS,
    FLEX_OPTIMIZE,
    FLEX_OPTIMIZE_MIN_STATIC,
    STRIP_LODS,
    MIN_LOD,
    DEFINE_BONES,
    OVERRIDE_DEFINE_BONES,
    CHECK_LENGTHS,
    PRINT_BONES,
    DUMP_MATERIALS,
    COLLAPSE_REPORT,
    ARGUMENTS,
};

void add_files(smdl::QcCompile &compile, PyObject *files) {
    if (!PyDict_Check(files))
        raise(PyExc_TypeError, "files must be a dict of path to bytes or str, not %.100s", type_name(files));
    PyObject *key;
    PyObject *value;
    Py_ssize_t position = 0;
    while (PyDict_Next(files, &position, &key, &value)) {
        const std::string path = fs_path(key, "files key", false);
        if (PyUnicode_Check(value)) {
            compile.addFile(path, std::string_view(Conv<std::string>::from(value, "files value")));
            continue;
        }
        Py_buffer view;
        if (PyObject_GetBuffer(value, &view, PyBUF_SIMPLE) < 0) {
            PyErr_Clear();
            raise(PyExc_TypeError, "files values must be bytes-like or str, not %.100s", type_name(value));
        }
        try {
            compile.addFile(path, view.buf, (size_t) view.len);  // copied
        } catch (...) {
            PyBuffer_Release(&view);
            throw;
        }
        PyBuffer_Release(&view);
    }
}

void set_fit_weights(smdl::QcCompile &compile, PyObject *value) {
    if (value == Py_True || value == Py_False) {
        compile.setFitWeights(value == Py_True);
        return;
    }
    if (!PySequence_Check(value) || PySequence_Size(value) != 2)
        raise(PyExc_TypeError, "fit_weights must be a bool or a (fitting_poses, test_poses) pair");
    Ref fitting = Ref::check(PySequence_GetItem(value, 0));
    Ref test = Ref::check(PySequence_GetItem(value, 1));
    compile.setFitWeights(true, Conv<int>::from(fitting.get(), "fit_weights"),
                          Conv<int>::from(test.get(), "fit_weights"));
}

PyObject *compile_qc(PyObject *, PyObject *args, PyObject *kwargs) {
    std::vector<std::pair<smdl::Severity, std::string>> log;
    std::exception_ptr error;
    PyObject *output = guarded([&]() -> PyObject * {
        Args a("compile_qc", args, kwargs,
               {"qc_path", "game_dir", "files", "write_to_game_dir", "no_warnings", "max_warnings", "fit_weights",
                "flex_optimize", "flex_optimize_min_static", "strip_lods", "min_lod", "define_bones",
                "override_define_bones", "check_lengths", "print_bones", "dump_materials", "collapse_report",
                "arguments"},
               1, 1);
        smdl::QcCompile compile;
        compile.setQcPath(fs_path(a[QC_PATH], "qc_path", false));
        if (a.has(GAME_DIR))
            compile.setGameDir(fs_path(a[GAME_DIR], "game_dir"));
        if (a.has(FILES))
            add_files(compile, a[FILES]);
        compile.setWriteToGameDir(a.get<bool>(WRITE_TO_GAME_DIR, false));
        if (a.has(NO_WARNINGS))
            compile.setNoWarnings(a.get<bool>(NO_WARNINGS));
        if (a.has(MAX_WARNINGS))
            compile.setMaxWarnings(a.get<int>(MAX_WARNINGS));
        if (a.has(FIT_WEIGHTS))
            set_fit_weights(compile, a[FIT_WEIGHTS]);
        if (a.has(FLEX_OPTIMIZE))
            compile.setFlexOptimize(static_cast<smdl::FlexOptimize>(a.get<uint32_t>(FLEX_OPTIMIZE)));
        if (a.has(FLEX_OPTIMIZE_MIN_STATIC))
            compile.setFlexOptimizeMinStatic(a.get<int>(FLEX_OPTIMIZE_MIN_STATIC));
        if (a.has(STRIP_LODS))
            compile.setStripLods(a.get<bool>(STRIP_LODS));
        if (a.has(MIN_LOD))
            compile.setMinLod(a.get<int>(MIN_LOD));
        if (a.has(DEFINE_BONES))
            compile.setDefineBones(a.get<bool>(DEFINE_BONES));
        if (a.has(OVERRIDE_DEFINE_BONES))
            compile.setOverrideDefineBones(a.get<bool>(OVERRIDE_DEFINE_BONES));
        if (a.has(CHECK_LENGTHS))
            compile.setCheckLengths(a.get<bool>(CHECK_LENGTHS));
        if (a.has(PRINT_BONES))
            compile.setPrintBones(a.get<bool>(PRINT_BONES));
        if (a.has(DUMP_MATERIALS))
            compile.setDumpMaterials(a.get<bool>(DUMP_MATERIALS));
        if (a.has(COLLAPSE_REPORT))
            compile.setCollapseReport(a.get<bool>(COLLAPSE_REPORT));
        if (a.has(ARGUMENTS))
            for (const std::string &argument: string_list(a[ARGUMENTS], "arguments"))
                compile.addArgument(argument);
        compile.setLog([&log](smdl::Severity severity, std::string_view text) {
            log.emplace_back(severity, std::string(text));
        });

        std::optional<smdl::Result> result;
        Py_BEGIN_ALLOW_THREADS
        try {
            result.emplace(compile.run());
        } catch (...) {
            error = std::current_exception();
        }
        Py_END_ALLOW_THREADS
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

const PyMethodDef module_methods[] = {
    {"compile_qc", PYSMDL_METHOD(compile_qc), PYSMDL_KW,
     "compile_qc(qc_path: StrPath, *, game_dir: StrPath | None = None, "
     "files: Mapping[StrPath, Buffer | str] | None = None, write_to_game_dir: bool = False, "
     "no_warnings: bool | None = None, max_warnings: int | None = None, "
     "fit_weights: bool | tuple[int, int] | None = None, flex_optimize: FlexOptimize | None = None, "
     "flex_optimize_min_static: int | None = None, strip_lods: bool | None = None, min_lod: int | None = None, "
     "define_bones: bool | None = None, override_define_bones: bool | None = None, "
     "check_lengths: bool | None = None, print_bones: bool | None = None, dump_materials: bool | None = None, "
     "collapse_report: bool | None = None, arguments: StrSequence = ()) -> Result\n--\n\n"
     "Compile a QC or .mpp file and return a Result with the files in memory.\n\n"
     "qc_path: the QC file; its directory is the base of relative paths. game_dir: game directory with "
     "gameinfo.txt (None: no game directory). files: dict of path to bytes or str read instead of the disk "
     "(paths absolute, or relative to the QC directory). write_to_game_dir: also write the outputs under game_dir. "
     "The other keywords are studiomdl's command line options (None: studiomdl's default); fit_weights is a bool "
     "or a (fitting_poses, test_poses) pair, flex_optimize a FlexOptimize. arguments: extra command line tokens "
     "(\"-maxwarnings\", \"10\"). Runs without the GIL. Raises QcError (file and line of the error, compiler "
     "messages) when the compile fails."},
};

} // namespace

const Part part_qc = {
    module_methods,
    {},
    {},
    {},
    nullptr,
};

} // namespace pysmdl
