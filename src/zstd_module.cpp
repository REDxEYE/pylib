#include <utils.hpp>
#include <cstdint>
#include "zstd.h"

extern "C" {

PYLIB_DLL_EXPORT size_t zstd_decompress(char *src, size_t src_size, char *dst, size_t dst_size) {
    return ZSTD_decompress(dst, dst_size, src, src_size);
}

PYLIB_DLL_EXPORT size_t zstd_compress(char *src, size_t src_size, char *dst, size_t dst_size) {
    return ZSTD_compress(dst, dst_size, src, src_size, 0);
}

PYLIB_DLL_EXPORT size_t zstd_decompress_stream(char *src, size_t src_size, char *dst, size_t dst_size) {
    ZSTD_DCtx_s *dctx = ZSTD_createDCtx();
    size_t zresult, total_write = 0, read_size = ZSTD_DStreamInSize();

    size_t in_offset = 0, in_size = src_size;

    ZSTD_inBuffer in_buffer;


    ZSTD_outBuffer out_buffer;
    out_buffer.dst = dst;
    out_buffer.size = dst_size;
    out_buffer.pos = 0;
    while (true) {
        if (in_size == 0) {
            break;
        }

        size_t block_size = std::min(in_size, read_size);
        in_buffer.src = src + in_offset;
        in_buffer.size = block_size;
        in_buffer.pos = 0;

        in_size -= block_size;
        in_offset += block_size;

        while (in_buffer.pos < in_buffer.size) {
            zresult = ZSTD_decompressStream(dctx, &out_buffer, &in_buffer);
            if (ZSTD_isError(zresult)) {
                fprintf(stderr, "zstd decompressor error: %s\n", ZSTD_getErrorName(zresult));
                return -1;
            }

            if (out_buffer.pos) {
                total_write += out_buffer.pos;
            }
        }
    }

    ZSTD_freeDCtx(dctx);
    return total_write;
}

PYLIB_DLL_EXPORT size_t zstd_compress_bound(size_t size) {
    return ZSTD_compressBound(size);
}

PYLIB_DLL_EXPORT size_t zstd_compress_stream(char *src, size_t src_size, char *dst, size_t dst_size) {
    size_t zresult;
    ZSTD_CCtx_s *cctx = ZSTD_createCCtx();
    ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);

    zresult = ZSTD_CCtx_setPledgedSrcSize(cctx, src_size);
    if (ZSTD_isError(zresult)) {
        fprintf(stderr, "error setting source size: %s\n", ZSTD_getErrorName(zresult));
        return -1;
    }

    ZSTD_inBuffer in_buffer;
    in_buffer.src = src;
    in_buffer.size = src_size;
    in_buffer.pos = 0;

    ZSTD_outBuffer out_buffer;
    out_buffer.dst = dst;
    out_buffer.size = dst_size;
    out_buffer.pos = 0;

    zresult = ZSTD_compressStream2(cctx, &out_buffer, &in_buffer, ZSTD_e_end);
    if (ZSTD_isError(zresult)) {
        fprintf(stderr, "cannot compress: %s\n", ZSTD_getErrorName(zresult));
        return -1;
    } else if (zresult) {
        fprintf(stderr, "unexpected partial frame flush\n");
        return -1;
    }
    return out_buffer.pos;

}

}