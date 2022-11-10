//
// Created by AORUS on 13.07.2022.
//

#include "bc4.h"
#include "bc3.h"

void decode_bc4_color_to_buffer(int block_index, const BC4Block &block, uint32_t width, RGBA8888 *output_pixels) {
    decode_2value_8indices_to_channel(block_index, block.value0, block.value1, block.indices0, block.indices1, width, output_pixels, 0);
}

ERROR_CODES decode_bc4_buffer(char *input_buffer, uint32_t input_buffer_size,
                      RGBA8888 *output_buffer, uint32_t output_buffer_size,
                      uint32_t width, uint32_t height) {
    uint32_t block_count = (height * width) / 16;
    auto *data_blocks = (BC4Block *) input_buffer;

    if (input_buffer_size < sizeof(BC4Block) * block_count) {
        return  ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size < width * height * 4) {
        return  ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    for (int block_index = 0; block_index < block_count; ++block_index) {
        decode_bc4_color_to_buffer(block_index, data_blocks[block_index], width, output_buffer);
    }
    return  ERROR_CODES::NO_ERROR;

}


