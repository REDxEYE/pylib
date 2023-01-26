#pragma once

#include <bcdec.h>
#include <utils.hpp>

enum BCnMode : uint32_t {
    BC1 = 1,
    BC1A,
    BC2,
    BC3,
    BC4,
    BC5,
    BC6,
    BC7,

};

extern "C" {
PYLIB_DLL_EXPORT bool image_decode_bcn(char *src, size_t src_size, char *dst, size_t dst_size,
                                       int32_t width, int32_t height, BCnMode bc_mode, uint32_t flip);

PYLIB_DLL_EXPORT bool image_decompress(char *src, size_t src_size, char *dst, size_t dst_size,
                                       int32_t width, int32_t height, uint32_t input_fmt, uint32_t output_fmt,
                                       uint32_t flip);

}