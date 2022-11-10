//
// Created by AORUS on 13.07.2022.
//

#include <cstring>
#include <cmath>
#include "bc1.h"


void decode_bc1_color_to_buffer(int block_index, const BC1ColorBlock &block,
                                uint32_t width, RGBA8888 *output_pixels,
                                bool no_alpha) {
    RGB888 color0 = block.color0.to_rgb888();
    RGB888 color1 = block.color1.to_rgb888();
    RGB888 colors[4] = {color0, color1};
    if (block.color0.as_int() > block.color1.as_int() || no_alpha) {
        colors[2] = rgb_lerp(color0, color1, 2, 1);
        colors[3] = rgb_lerp(color0, color1, 1, 2);
    } else {
        colors[2] = rgb_lerp(color0, color1, 1, 1);
        colors[3] = RGB888();
    }

    uint32_t color_indices = block.indices;
    uint32_t x = block_index % (width / 4) * 4;
    uint32_t y = (block_index / (width / 4)) * 4;
    for (int by = 0; by < 4; ++by) {
        for (int bx = 0; bx < 4; ++bx) {
            uint8_t color_index = color_indices & 0b11;
            color_indices >>= 2;
            uint32_t full_x = x + bx;
            uint32_t full_y = y + by;
            output_pixels[full_y * width + full_x].set_rgb(colors[color_index]);
            if (!no_alpha && color_index == 3) {
                output_pixels[full_y * width + full_x].a = 0;
            }
        }
    }
}

ERROR_CODES decode_bc1_buffer(char *input_buffer, uint32_t input_buffer_size,
                              RGBA8888 *output_buffer, uint32_t output_buffer_size,
                              uint32_t width, uint32_t height, bool no_alpha) {
    uint32_t block_count = (height * width) / 16;
    auto *data_blocks = (BC1ColorBlock *) input_buffer;

    if (input_buffer_size < sizeof(BC1ColorBlock) * block_count) {
        return ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size != width * height * 4) {
        return ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    for (int block_index = 0; block_index < block_count; ++block_index) {
        decode_bc1_color_to_buffer(block_index, data_blocks[block_index], width, output_buffer, no_alpha);
    }
    return ERROR_CODES::NO_ERROR;

}

struct GradientInfo {
    uint8_t color0_index;
    uint8_t color1_index;
    RGB565 lerped[4];
    uint32_t error;
};

struct Color {
    RGB565 value;
    uint8_t frequency;
};

void selection_sort(Color arr[], uint32_t n) {
    uint32_t min_idx;

    for (uint32_t i = 0; i < n - 1; i++) {
        min_idx = i;
        for (uint32_t j = i + 1; j < n; j++)
            if (arr[j].frequency < arr[min_idx].frequency)
                min_idx = j;
        SWAP(Color, arr[min_idx], arr[i]);
    }
}

void pick_2_major_colors(const RGB565 *unique_colors, const uint8_t *color_freq, uint16_t color_count, RGB565 *color0,
                         RGB565 *color1) {
    auto *colors = new Color[color_count];
    for (int i = 0; i < color_count; ++i) {
        colors[i].value = unique_colors[i];
        colors[i].frequency = color_freq[i];
    }
    selection_sort(colors, color_count);
    *color0 = colors[color_count - 1].value;

    if (color_count == 1) {
        *color1 = colors[color_count - 1].value;
    } else
        *color1 = colors[color_count - 2].value;
}

constexpr uint8_t get_closest_color_index(const RGB565 colors[4], RGB565 color) {
    uint16_t color_error = 65535;
    uint16_t lowest_id = 0;

    for (int color_id = 0; color_id < 4; color_id++) {
        uint8_t error = colors[color_id].diff(color);
        if (error == 0) {
            return color_id;
        }
        if (error <= color_error) {
            color_error = error;
            lowest_id = color_id;
        }
    }
    return lowest_id;
}


DLL_EXPORT ERROR_CODES encode_bc1_buffer(RGBA8888 *input_buffer, uint32_t input_buffer_size,
                                         BC1ColorBlock *output_buffer, uint32_t output_buffer_size,
                                         uint32_t width, uint32_t height, bool no_alpha) {
    if (input_buffer_size < (width * height * 4)) {
        return ERROR_CODES::INPUT_BUFFER_SMALL;
    }
    if (output_buffer_size < width * height / 2) {
        return ERROR_CODES::OUTPUT_BUFFER_SMALL;
    }

    uint32_t block_count = (height * width) / 16;
    for (uint32_t block_index = 0; block_index < block_count; block_index++) {
        uint32_t x = block_index % (width / 4) * 4;
        uint32_t y = (block_index / (width / 4)) * 4;
        uint16_t unique_count = 0;
        RGB565 all_colors[16]{0};
        RGB565 unique_colors[16]{0};
        uint8_t color_frequency[16]{0};
        for (int by = 0; by < 4; ++by) {
            for (int bx = 0; bx < 4; ++bx) {
                auto color = input_buffer[(y + by) * width + (x + bx)].to_rgb565();
                all_colors[bx + by * 4] = color;
                bool new_color = true;
                for (uint16_t color_id = 0; color_id < unique_count; color_id++) {
                    if (unique_colors[color_id].as_int() == color.as_int()) {
                        color_frequency[color_id]++;
                        new_color = false;
                        break;
                    }
                }
                if (new_color) {
                    unique_colors[unique_count] = color;
                    color_frequency[unique_count]++;
                    unique_count++;
                }
            }
        }
        
        RGB565 color0{0}, color1{0};
        pick_2_major_colors(unique_colors, color_frequency, unique_count, &color0, &color1);
        if (color0.as_int() < color1.as_int())
            SWAP(RGB565, color0, color1);

        RGB565 output_colors[4] = {color0, color1, 0, 0};
        if (no_alpha) {
            output_colors[2] = color0.lerp(color1, 2, 1);
            output_colors[3] = color0.lerp(color1, 1, 2);
        } else {
            output_colors[2] = color0.lerp(color1, 1, 1);
            output_colors[3] = RGB565();
        }

        BC1ColorBlock &block = output_buffer[block_index];
        block.color0 = color0;
        block.color1 = color1;
        for (auto color_id = 0; color_id < 16; ++color_id) {
            uint8_t bc_color_id = get_closest_color_index(output_colors, all_colors[color_id]);
            SET_BITS(block.indices, color_id * 2, 2, bc_color_id);
        }
    }
    return ERROR_CODES::NO_ERROR;
}
