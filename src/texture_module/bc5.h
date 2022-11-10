//
// Created by AORUS on 13.07.2022.
//

#pragma once


#include <cstdint>
#include "rgb.h"
#include "shared.h"
#include "bc4.h"


struct BC5Block {
    BC4Block red;
    BC4Block green;
};

static_assert(sizeof(BC5Block) == 16);

DLL_EXPORT void
decode_bc5_color_to_buffer(int block_index, const BC5Block &block, uint32_t width, RGBA8888 *output_pixels);

DLL_EXPORT ERROR_CODES decode_bc5_buffer(char *input_buffer, uint32_t input_buffer_size,
                                 RGBA8888 *output_buffer, uint32_t output_buffer_size,
                                 uint32_t width, uint32_t height);


