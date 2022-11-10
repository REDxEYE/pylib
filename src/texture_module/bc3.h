//
// Created by AORUS on 13.07.2022.
//

#pragma once


#include "rgb.h"
#include "bc1.h"

struct BC3AlphaBlock {
    uint64_t alpha0: 8;
    uint64_t alpha1: 8;
    uint64_t indices0: 24;
    uint64_t indices1: 24;
};
static_assert(sizeof(BC3AlphaBlock) == 8);

struct BC3Block {
    BC3AlphaBlock alpha;
    BC1ColorBlock color;
};
static_assert(sizeof(BC3Block) == 16);

DLL_EXPORT void
decode_bc3_alpha_to_buffer(int block_index, const BC3AlphaBlock &block, uint32_t width, RGBA8888 *output_pixels);

DLL_EXPORT ERROR_CODES decode_bc3_buffer(char *input_buffer, uint32_t input_buffer_size,
                                 RGBA8888 *output_buffer, uint32_t output_buffer_size,
                                 uint32_t width, uint32_t height);

void decode_2value_8indices_to_channel(int block_index,
                                      uint8_t value0, uint8_t value1,
                                      uint32_t indices0, uint32_t indices1,
                                      uint32_t width, RGBA8888 *output_pixels,
                                      uint32_t channel);


