#include "common.h"

void image_flip(uint32_t *data, uint32_t width, uint32_t height) {
    for (uint32_t y = 0; y < height / 2; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t *a = &data[y * width + x];
            uint32_t *b = &data[(height - y - 1) * width + x];
            *a ^= *b;
            *b ^= *a;
            *a ^= *b;
        }
    }
}