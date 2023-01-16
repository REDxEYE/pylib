//
// Created by MED45 on 08.03.2022.
//

#include "texture_decompression.h"
#include "detex.h"

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

PYLIB_DLL_EXPORT bool image_decompress(char *src, size_t src_size, char *dst, size_t dst_size, int32_t width,
                                       int32_t height, uint32_t input_fmt, uint32_t output_fmt, uint32_t flip) {
    detexTexture texture;
    if (src == nullptr)return false;
    if (dst == nullptr)return false;
    if (dst_size < width * height * 4)return false;
    texture.format = input_fmt;
    texture.data = (uint8_t *) src;
    texture.width = width;
    texture.height = height;
    texture.width_in_blocks = (int32_t) (width / 4);
    texture.height_in_blocks = (int32_t) (height / 4);

    if (!detexDecompressTextureLinear(&texture, (uint8_t *) dst, output_fmt)) {
        fprintf(stderr, "Buffer cannot be decompressed: %s\n", detexGetErrorMessage());
        return false;
    }

    if (flip) {
        image_flip((uint32_t *) dst, width, height);
    }
    return true;
}