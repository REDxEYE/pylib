//
// Created by AORUS on 13.07.2022.
//

#include "bc3.h"

void decode_bc3_alpha_to_buffer(int block_index, const BC3AlphaBlock &block, uint32_t width, RGBA8888 *output_pixels) {
    decode_2value_8indices_to_channel(block_index, block.alpha0, block.alpha1, block.indices0, block.indices1, width,
                                      output_pixels, 3);

}

ERROR_CODES decode_bc3_buffer(char *input_buffer, uint32_t input_buffer_size,
                      RGBA8888 *output_buffer, uint32_t output_buffer_size,
                      uint32_t width, uint32_t height) {
    uint32_t block_count = (height * width) / 16;
    auto *data_blocks = (BC3Block *) input_buffer;

    if (input_buffer_size < sizeof(BC3Block) * block_count) {
        return  ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size < width * height * 4) {
        return  ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    for (int block_index = 0; block_index < block_count; ++block_index) {
        decode_bc3_alpha_to_buffer(block_index, data_blocks[block_index].alpha, width, output_buffer);
        decode_bc1_color_to_buffer(block_index, data_blocks[block_index].color, width, output_buffer, true);
    }
    return  ERROR_CODES::NO_ERROR;

}

void decode_2value_8indices_to_channel(int block_index,
                                       uint8_t value0, uint8_t value1,
                                       uint32_t indices0, uint32_t indices1,
                                       uint32_t width, RGBA8888 *output_pixels,
                                       uint32_t channel) {

    uint8_t values[8] = {value0, value1};
    if (value0 > value1) {
        values[2] = lerp(value0, value1, 6, 1);
        values[3] = lerp(value0, value1, 5, 2);
        values[4] = lerp(value0, value1, 4, 3);
        values[5] = lerp(value0, value1, 3, 4);
        values[6] = lerp(value0, value1, 2, 5);
        values[7] = lerp(value0, value1, 1, 6);
    } else {
        values[2] = lerp(value0, value1, 4, 1);
        values[3] = lerp(value0, value1, 3, 2);
        values[4] = lerp(value0, value1, 2, 3);
        values[5] = lerp(value0, value1, 1, 4);
        values[6] = 0;
        values[7] = 255;
    }
    uint32_t x = block_index % (width / 4) * 4;
    uint32_t y = (block_index / (width / 4)) * 4;
    for (int by = 0; by < 2; ++by) {
        for (int bx = 0; bx < 4; ++bx) {
            uint8_t index0 = indices0 & 0b111;
            indices0 >>= 3;
            uint8_t index1 = indices1 & 0b111;
            indices1 >>= 3;
            uint32_t full_x = x + bx;
            uint32_t full_y = y + by;
            ((uint8_t *) &output_pixels[full_y * width + full_x])[channel] = values[index0];
            ((uint8_t *) &output_pixels[(full_y + 2) * width + full_x])[channel] = values[index1];
        }
    }

}
