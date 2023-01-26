#pragma once

#include "utils.hpp"
#include "vtflib/vtflib.h"
#include "texture_decompression.h"

extern "C" {
PYLIB_DLL_EXPORT void *vtf_load_vtf(char *src, size_t src_size);

PYLIB_DLL_EXPORT uint32_t vtf_width(VTFFile *vfile);

PYLIB_DLL_EXPORT uint32_t vtf_height(VTFFile *vfile);

PYLIB_DLL_EXPORT VTFImageFormat vtf_image_format(VTFFile *vfile);

PYLIB_DLL_EXPORT bool vtf_get_as_rgba8888(VTFFile *vfile, char *dst, size_t dst_size, bool flip);

PYLIB_DLL_EXPORT void vtf_destroy(VTFFile *vfile);

}
