#ifndef PYLIB_BCDEC_HELPER_H
#define PYLIB_BCDEC_HELPER_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ext/bcdec.h"

// Decoder: callable as decoder(const void *src_block, void *dst, int pitch).

template<size_t blockSize, size_t pixelSize, typename Decoder>
inline void partialBCn(const uint8_t *src, uint8_t *dst, uint32_t partialWidth, uint32_t partialHeight,
                       size_t stride, Decoder &decoder) {
    // Decode the whole block into scratch (row pitch = one block row), then copy
    // only the visible part: rows are blockSize * pixelSize bytes apart in the
    // scratch, and each visible row is partialWidth * pixelSize bytes wide.
    alignas(8) uint8_t tmpBuffer[blockSize * blockSize * pixelSize];
    decoder((const void *) src, (void *) tmpBuffer, (int) (blockSize * pixelSize));

    for (uint32_t y = 0; y < partialHeight; y++) {
        memcpy(dst + (size_t) y * stride, &tmpBuffer[y * blockSize * pixelSize], partialWidth * pixelSize);
    }
}

// Caller guarantees width * pixelSize fits in int (bcdec takes an int pitch) and
// that src holds ceil(width/blockSize) * ceil(height/blockSize) blocks.
template<size_t bytesPerBlock, size_t blockSize, size_t pixelSize, typename Decoder>
inline void convertBCn(const uint8_t *src, uint8_t *output, uint32_t width, uint32_t height, Decoder decoder) {
    const size_t stride = (size_t) width * pixelSize;
    for (uint32_t y = 0; y < height; y += blockSize) {
        for (uint32_t x = 0; x < width; x += blockSize) {
            uint8_t *dst = output + (size_t) y * stride + (size_t) x * pixelSize;

            if (height - y >= blockSize && width - x >= blockSize) {
                decoder((const void *) src, (void *) dst, (int) stride);
            } else {
                uint32_t partialWidth = std::min(width - x, (uint32_t) blockSize);
                uint32_t partialHeight = std::min(height - y, (uint32_t) blockSize);
                partialBCn<blockSize, pixelSize>(src, dst, partialWidth, partialHeight, stride, decoder);
            }

            src += bytesPerBlock;
        }
    }
}

template<size_t pixelSize, typename Decoder>
inline void convertBC6(const uint8_t *src, uint8_t *output, uint32_t width, uint32_t height, Decoder decoder) {
    const size_t stride = (size_t) width * pixelSize;
    for (uint32_t y = 0; y < height; y += 4) {
        uint8_t *dstRow = output + (size_t) y * stride;
        for (uint32_t x = 0; x < width; x += 4) {
            uint8_t *dst = dstRow + (size_t) x * pixelSize;
            if (height - y >= 4 && width - x >= 4) {
                // BCDEC BC6 expects pitch in half-float count, not in byte count.
                decoder((const void *) src, (void *) dst, (int) (stride / 2));
            } else {
                uint32_t partialWidth = std::min(width - x, (uint32_t) 4);
                uint32_t partialHeight = std::min(height - y, (uint32_t) 4);
                alignas(8) uint8_t tmpBuffer[4 * 4 * pixelSize];
                // The scratch block is 4 pixels of 3 halfs wide.
                decoder((const void *) src, (void *) tmpBuffer, 4 * 3);

                for (uint32_t row = 0; row < partialHeight; row++) {
                    memcpy(dst + (size_t) row * stride, &tmpBuffer[row * 4 * pixelSize], partialWidth * pixelSize);
                }
            }
            src += BCDEC_BC6H_BLOCK_SIZE;
        }
    }
}

inline void bcdec_bc6h_half_unsigned(const void *compressedBlock, void *decompressedBlock, int destinationPitch) {
    bcdec_bc6h_half(compressedBlock, decompressedBlock, destinationPitch, false);
}

inline void bcdec_bc6h_half_signed(const void *compressedBlock, void *decompressedBlock, int destinationPitch) {
    bcdec_bc6h_half(compressedBlock, decompressedBlock, destinationPitch, true);
}

#endif //PYLIB_BCDEC_HELPER_H
