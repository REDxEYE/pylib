//
// Created by RED on 13.08.2025.
//

#ifndef PYLIB_VTF_UTLS_H
#define PYLIB_VTF_UTLS_H

#include "VTFLib.h"

inline bool is_float_storage(VTFImageFormat format){
    switch (format) {
        case VTFImageFormat::IMAGE_FORMAT_RGBA32323232F:
        case VTFImageFormat::IMAGE_FORMAT_RGB323232F:
        case VTFImageFormat::IMAGE_FORMAT_RGBA16161616F:
        case VTFImageFormat::IMAGE_FORMAT_RGBA16161616:
        case VTFImageFormat::IMAGE_FORMAT_R32F:
            return true;
        default:
            break;
    }
    return false;
}


#endif //PYLIB_VTF_UTLS_H
