#include <Python.h>
#include "modules/compression_module.h"
#include "modules/mesh_module.h"
#include "modules/image_module.h"
#include "modules/vtf_module.h"
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

    PyObject *path_list = Py_BuildValue("[s]", "pylib");
    if (path_list) {
        PyModule_AddObject(root, "__path__", path_list);
        Py_DECREF(path_list);
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

    PyObject* t = PyType_FromSpec(&VPKFile_class_spec);
    if (!t) return nullptr;
    if (PyModule_AddObject(root, "VPKFile", t) < 0) {
        Py_DECREF(t);
        return nullptr;
    }

    if (PyModule_AddStringConstant(root, "__version__", PYLIB_VERSION) < 0) {
        Py_DECREF(root);
        return nullptr;
    }

    return root;
}