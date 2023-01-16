
#include "utils.hpp"
#include "lz4_module.h"
#include "lz4.h"
#include <string>
#include <cstring>

PYLIB_DLL_EXPORT int lz4_decompress(char *src, int src_size, char *dst, int dst_size) {
    int32_t real_decompressed_size = LZ4_decompress_safe(src, dst, src_size, dst_size);
    if (real_decompressed_size <= 0) {
        return real_decompressed_size;
    }
    if (real_decompressed_size != dst_size) {
        fprintf(stderr,"Failed to decompress buffer\n");
        return 0;
    }
    return real_decompressed_size;
}

PYLIB_DLL_EXPORT int lz4_compress(char *src, int src_size, char *dst, int dst_size) {
    int32_t real_compressed_size = LZ4_compress_default(src, dst, src_size, dst_size);
    if (real_compressed_size <= 0) {
        return real_compressed_size;
    }
    if (real_compressed_size != dst_size) {
        fprintf(stderr,"Failed to decompress buffer\n");
        return 0;
    }
    return real_compressed_size;
}


void prepare(LZ4ChainDecoder *self, size_t block_size) {
    if (self->output_index + block_size <= self->output_length)
        return;
    size_t dict_start = MAX(self->output_index - (1024 * 64), 0);
    size_t dict_size = self->output_index - dict_start;
    memmove(self->output_buffer, self->output_buffer + dict_start, dict_size);
    LZ4_setStreamDecode(self->stream_state, (const char *) self->output_buffer, (int) dict_size);
    self->output_index = dict_size;
}

int64_t decode(LZ4ChainDecoder *self, const uint8_t *src, size_t src_size, size_t block_size) {
    if (block_size <= 0) {
        block_size = self->block_size;
    }
    prepare(self, block_size);
    uint8_t *tmp = self->output_buffer + self->output_index;
    int64_t decoded_size = LZ4_decompress_safe_continue(self->stream_state, (const char *) src, (char *) tmp,
                                                        (int) src_size, (int) block_size);
    if (decoded_size > 0) {
        self->output_index += decoded_size;
    }
    return decoded_size;
}

bool drain(LZ4ChainDecoder *self, uint8_t *dst, int64_t offset, size_t size) {
    offset += self->output_index;
    if (offset < 0 || size < 0 || offset + size > self->output_index) {
        fprintf(stderr,"Invalid offset(%lli), size(%lli) or offset+size > %u\n",
               offset, size, self->output_index);
        return false;
    }
    memmove(dst, self->output_buffer + offset, size);
    return true;
}

bool decode_and_drain(LZ4ChainDecoder *self, const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size) {
    int64_t decoded = decode(self, src, src_size, dst_size);
    if (decoded <= 0 || dst_size < decoded) {
        fprintf(stderr,"Received error code from LZ4: %lli\n", decoded);
        return false;
    }
    return drain(self, dst, -decoded, decoded);
};


PYLIB_DLL_EXPORT bool LZ4ChainDecoder_decompress(LZ4ChainDecoder *self, char *src, size_t data_size, char *dst, size_t decompressed_size) {
    auto *src_buf = (uint8_t *) src;
    auto *dst_buf = (uint8_t *) dst;

    if (!decode_and_drain(self, src_buf, data_size, dst_buf, decompressed_size)) {
        return false;
    }
    return true;
}

PYLIB_DLL_EXPORT int LZ4ChainDecoder_init(LZ4ChainDecoder *self, int block_size, int extra_blocks) {
    self->block_size = round_up(MAX(block_size, 1024), 1024);
    self->extra_blocks = MAX(extra_blocks, 0);
    self->output_length = (1024 * 64) + (1 + self->extra_blocks) * self->block_size + 32;
    self->output_index = 0;

    self->output_buffer = (uint8_t *) malloc(self->output_length + 8);
    memset(self->output_buffer, 0, self->output_length + 8);
    self->stream_state = LZ4_createStreamDecode();
    return 0;
}

PYLIB_DLL_EXPORT LZ4ChainDecoder *LZ4ChainDecoder_new() {
    return new LZ4ChainDecoder();
}

PYLIB_DLL_EXPORT void LZ4ChainDecoder_dealloc(LZ4ChainDecoder *self) {
    free(self->output_buffer);
    LZ4_freeStreamDecode(self->stream_state);
    free(self);
}
