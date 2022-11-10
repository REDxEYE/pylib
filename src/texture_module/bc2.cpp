//
// Created by AORUS on 13.07.2022.
//

#include "bc2.h"


void decode_bc2_alpha_to_buffer(int block_index, const BC2AlphaBlock &block,
                                uint32_t width, RGBA8888 *output_pixels) {
    uint32_t x = block_index % (width / 4) * 4;
    uint32_t y = (block_index / (width / 4)) * 4;
    for (int by = 0; by < 4; ++by) {
        uint16_t alpha_data = block.alphas[by];
        for (int bx = 0; bx < 4; ++bx) {
            uint8_t alpha = alpha_data & 0b1111;
            alpha_data >>= 4;
            output_pixels[(y + by) * width + (x + bx)].a = alpha * 255 / 15;
        }
    }
}

ERROR_CODES decode_bc2_buffer(char *input_buffer, uint32_t input_buffer_size,
                      RGBA8888 *output_buffer, uint32_t output_buffer_size,
                      uint32_t width, uint32_t height) {
    uint32_t block_count = (height * width) / 16;
    auto *data_blocks = (BC2Block *) input_buffer;

    if (input_buffer_size < sizeof(BC2Block) * block_count) {
        return ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size < width * height * 4) {
        return  ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    for (int block_index = 0; block_index < block_count; ++block_index) {
        decode_bc2_alpha_to_buffer(block_index, data_blocks[block_index].alpha, width, output_buffer);
        decode_bc1_color_to_buffer(block_index, data_blocks[block_index].color, width, output_buffer, true);
    }
    return  ERROR_CODES::NO_ERROR;

}
