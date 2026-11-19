#ifndef PYLIB_STUDIOMDL_MODULE_H
#define PYLIB_STUDIOMDL_MODULE_H

#include <Python.h>

//! Register ``pylib.studiomdl``. Only built with PYLIB_WITH_STUDIOMDL (x86_64 targets).
PyObject *StudioMdlModule_Init(PyObject *parent_module);

#endif //PYLIB_STUDIOMDL_MODULE_H
