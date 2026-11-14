// Created by RED on 03.12.2025.
#ifndef PYLIB_SMD_CLASS_H
#define PYLIB_SMD_CLASS_H
#include <Python.h>

#include "utils/smd_parser.h"

// Set by MeshModule_Init via add_type(); used to allocate child wrappers.
extern PyTypeObject *SMDModelType;
extern PyTypeObject *SMDNodeType;
extern PyTypeObject *SMDSkeletonType;
extern PyTypeObject *SMDBoneDefType;
extern PyTypeObject *SMDVertexType;
extern PyTypeObject *SMDTriangleType;

namespace smd::model {
    extern PyType_Spec class_spec;
}

namespace smd::node {
    extern PyType_Spec class_spec;
}

namespace smd::skeleton {
    extern PyType_Spec class_spec;
}

namespace smd::bonedef {
    extern PyType_Spec class_spec;
}

namespace smd::triangle {
    extern PyType_Spec class_spec;
}

namespace smd::vertex {
    extern PyType_Spec class_spec;
}

#endif //PYLIB_SMD_CLASS_H
