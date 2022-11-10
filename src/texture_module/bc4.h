//
// Created by AORUS on 13.07.2022.
//

#pragma once


#include <cstdint>
#include "rgb.h"
#include "shared.h"

struct BC4Block {
    uint64_t value0: 8;
    uint64_t value1: 8;
    uint64_t indices0: 24;
    uint64_t indices1: 24;
};

static_assert(sizeof(BC4Block) == 8);

DLL_EXPORT void
decode_bc4_color_to_buffer(int block_index, const BC4Block &block, uint32_t width, RGBA8888 *output_pixels);

DLL_EXPORT ERROR_CODES decode_bc4_buffer(char *input_buffer, uint32_t input_buffer_size,
                                 RGBA8888 *output_buffer, uint32_t output_buffer_size,
                                 uint32_t width, uint32_t height);


