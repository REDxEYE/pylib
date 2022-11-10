//
// Created by AORUS on 13.07.2022.
//

#include "bc5.h"
#include "bc3.h"

void decode_bc5_color_to_buffer(int block_index, const BC5Block &block, uint32_t width, RGBA8888 *output_pixels) {
    decode_2value_8indices_to_channel(block_index, block.red.value0, block.red.value1, block.red.indices0,
                                     block.red.indices1, width, output_pixels, 0);
    decode_2value_8indices_to_channel(block_index, block.green.value0, block.green.value1, block.green.indices0,
                                     block.green.indices1, width, output_pixels, 1);
}

ERROR_CODES decode_bc5_buffer(char *input_buffer, uint32_t input_buffer_size,
                      RGBA8888 *output_buffer, uint32_t output_buffer_size,
                      uint32_t width, uint32_t height) {
    uint32_t block_count = (height * width) / 16;
    auto *data_blocks = (BC5Block *) input_buffer;

    if (input_buffer_size < sizeof(BC5Block) * block_count) {
        return  ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size < width * height * 4) {
        return  ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    for (int block_index = 0; block_index < block_count; ++block_index) {
        decode_bc5_color_to_buffer(block_index, data_blocks[block_index], width, output_buffer);
    }
    return  ERROR_CODES::NO_ERROR;

}
