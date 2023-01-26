#ifndef PYLIB_LZ4_MODULE_HPP
#define PYLIB_LZ4_MODULE_HPP

#include <cstdint>
#include <utils.hpp>
#include <lz4.h>

typedef struct {
    uint32_t block_size;
    uint32_t extra_blocks;
    uint32_t output_length;
    uint32_t output_index;
    uint8_t *output_buffer;
    LZ4_streamDecode_t *stream_state;
} LZ4ChainDecoder;

extern "C" {

PYLIB_DLL_EXPORT int lz4_decompress(char *src, int src_size, char *dst, int dst_size);
PYLIB_DLL_EXPORT int lz4_compress(char *src, int src_size, char *dst, int dst_size);

PYLIB_DLL_EXPORT bool
LZ4ChainDecoder_decompress(LZ4ChainDecoder *self, char *src, size_t data_size, char *dst, size_t decompressed_size);

PYLIB_DLL_EXPORT int LZ4ChainDecoder_init(LZ4ChainDecoder *self, int block_size, int extra_blocks);

PYLIB_DLL_EXPORT LZ4ChainDecoder *LZ4ChainDecoder_new();

PYLIB_DLL_EXPORT void LZ4ChainDecoder_dealloc(LZ4ChainDecoder *self);
}

#endif // PYLIB_LZ4_MODULE_HPP
