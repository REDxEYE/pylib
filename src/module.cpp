#include <Python.h>
#include "modules/compression_module.h"
#include "modules/mesh_module.h"
#include "modules/image_module.h"
#include "modules/vtf_module.h"
#ifdef PYLIB_WITH_STUDIOMDL
#include "modules/studiomdl_module.h"
#endif
#include "classes/vpk_class.h"

#define PYLIB_MAJOR 0
#define PYLIB_MINOR 1
#define PYLIB_PATCH 0

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define PYLIB_VERSION TOSTRING(PYLIB_MAJOR) "." TOSTRING(PYLIB_MINOR) "." TOSTRING(PYLIB_PATCH)


static PyMethodDef root_methods[] = {
        {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef root_def = {
        PyModuleDef_HEAD_INIT,
        "pylib",
        "SourceIO helper library",
        -1,
        root_methods
};

PyMODINIT_FUNC PyInit_pylib(void) {
    PyObject *root = PyModule_Create(&root_def);
    if (!root)
        return nullptr;

    // An empty __path__ makes `pylib` a package; the submodules are registered
    // in sys.modules directly, so no directory needs to be searched.
    PyObject *path_list = PyList_New(0);
    if (!path_list) {
        Py_DECREF(root);
        return nullptr;
    }
    // AddObjectRef does not steal, so our reference is always ours to drop.
    int path_failed = PyModule_AddObjectRef(root, "__path__", path_list) < 0;
    Py_DECREF(path_list);
    if (path_failed) {
        Py_DECREF(root);
        return nullptr;
    }

    if(!CompressionModule_Init(root)){
        Py_DECREF(root);
        return nullptr;
    }
    if(!MeshModule_Init(root)){
        Py_DECREF(root);
        return nullptr;
    }

    if(!ImageModule_Init(root)){
        Py_DECREF(root);
        return nullptr;
    }
    if(!VTFModule_Init(root)){
        Py_DECREF(root);
        return nullptr;
    }
#ifdef PYLIB_WITH_STUDIOMDL
    if(!StudioMdlModule_Init(root)){
        Py_DECREF(root);
        return nullptr;
    }
#endif

    if (VPK_InitTypes(root) < 0) {
        Py_DECREF(root);
        return nullptr;
    }

    if (PyModule_AddStringConstant(root, "__version__", PYLIB_VERSION) < 0) {
        Py_DECREF(root);
        return nullptr;
    }

    return root;
}