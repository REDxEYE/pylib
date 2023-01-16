#pragma once

#include <utils.hpp>


extern "C" {
PYLIB_DLL_EXPORT bool image_decompress(char *src, size_t src_size, char *dst, size_t dst_size, int32_t width,
                                       int32_t height, uint32_t input_fmt, uint32_t output_fmt, uint32_t flip);

}