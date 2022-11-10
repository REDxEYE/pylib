//
// Created by AORUS on 13.07.2022.
//

#pragma once


#include "rgb.h"
#include "shared.h"

struct BC1ColorBlock {
    RGB565 color0, color1;
    uint32_t indices;

};


static_assert(sizeof(BC1ColorBlock) == 8);

DLL_EXPORT void decode_bc1_color_to_buffer(int block_index, const BC1ColorBlock &block,
                                           uint32_t width, RGBA8888 *output_pixels,
                                           bool no_alpha);

DLL_EXPORT ERROR_CODES decode_bc1_buffer(char *input_buffer, uint32_t input_buffer_size,
                                 RGBA8888 *output_buffer, uint32_t output_buffer_size,
                                 uint32_t width, uint32_t height, bool no_alpha = true);

DLL_EXPORT ERROR_CODES encode_bc1_buffer(RGBA8888 *input_buffer, uint32_t input_buffer_size,
                                 BC1ColorBlock *output_buffer, uint32_t output_buffer_size,
                                 uint32_t width, uint32_t height, bool no_alpha = true);


