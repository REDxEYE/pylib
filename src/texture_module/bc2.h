//
// Created by AORUS on 13.07.2022.
//

#pragma once


#include "rgb.h"
#include "bc1.h"

struct BC2AlphaBlock {
    uint16_t alphas[4];
};
static_assert(sizeof(BC2AlphaBlock) == 8);

struct BC2Block {
    BC2AlphaBlock alpha;
    BC1ColorBlock color;
};
static_assert(sizeof(BC2Block) == 16);

DLL_EXPORT void decode_bc2_alpha_to_buffer(int block_index, const BC2AlphaBlock &block,
                                uint32_t width, RGBA8888 *output_pixels);

DLL_EXPORT ERROR_CODES decode_bc2_buffer(char *input_buffer, uint32_t input_buffer_size,
                      RGBA8888 *output_buffer, uint32_t output_buffer_size,
                      uint32_t width, uint32_t height);

