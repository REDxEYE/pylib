//
// Created by MED45 on 08.03.2022.
//
#define BCDEC_IMPLEMENTATION

#include <bcdec.h>

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


PYLIB_DLL_EXPORT bool image_decode_bcn(char *src, size_t src_size, char *dst, size_t dst_size,
                                       int32_t width, int32_t height, BCnMode bc_mode, uint32_t flip) {
    uint32_t i, j;

    if (src == nullptr)return false;
    if (dst == nullptr)return false;
    if (bc_mode == BC6) {
        if (dst_size < width * height * 12)return false;
    } else {
        if (dst_size < width * height * 4)return false;
    }

    char *output = dst;
    float *output_hdr = (float *) dst;
    float *dst_hdr;

    switch (bc_mode) {
        case BC1:
            if (src_size != BCDEC_BC1_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec_bc1(src, dst, width * 4);
                    src += BCDEC_BC1_BLOCK_SIZE;
                }
            }
            break;
        case BC1A:
            if (src_size != BCDEC_BC1_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec__color_block(src, dst, width * 4, 1);
                    src += BCDEC_BC1_BLOCK_SIZE;
                }
            }
            break;
        case BC2:
            if (src_size != BCDEC_BC2_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec_bc2(src, dst, width * 4);
                    src += BCDEC_BC2_BLOCK_SIZE;
                }
            }
            break;
        case BC3:
            if (src_size != BCDEC_BC3_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec_bc3(src, dst, width * 4);
                    src += BCDEC_BC3_BLOCK_SIZE;
                }
            }
            break;
        case BC4:
            if (src_size != BCDEC_BC4_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = dst + (i * width + j) * 4;
                    bcdec_bc4(src, dst, width * 4);
                    src += BCDEC_BC4_BLOCK_SIZE;
                }
            }
            break;
        case BC5:
            if (src_size != BCDEC_BC5_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec_bc5(src, dst, width * 4);
                    src += BCDEC_BC5_BLOCK_SIZE;
                }
            }
            break;
        case BC6:
            if (src_size != BCDEC_BC6H_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst_hdr = output_hdr + (i * width + j) * 3;
                    bcdec_bc6h_float(src, dst_hdr, width * 3, false);
                    src += BCDEC_BC6H_BLOCK_SIZE;
                }
            }
            break;
        case BC7:
            if (src_size != BCDEC_BC7_COMPRESSED_SIZE(width, height))return false;
            for (i = 0; i < height; i += 4) {
                for (j = 0; j < width; j += 4) {
                    dst = output + (i * width + j) * 4;
                    bcdec_bc7(src, dst, width * 4);
                    src += BCDEC_BC7_BLOCK_SIZE;
                }
            }
            break;
    }
    return true;
}


PYLIB_DLL_EXPORT bool image_decompress(char *src, size_t src_size, char *dst, size_t dst_size,
                                       int32_t width, int32_t height,
                                       uint32_t input_fmt, uint32_t output_fmt, uint32_t flip) {
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