#ifndef PYLIB_BCDEC_HELPER_H
#define PYLIB_BCDEC_HELPER_H

using Decoder = std::function<void(void *src, void *dst, int len)>;

template<size_t blockSize, size_t pixelSize>
void
partialBCn(const uint8_t *src, uint8_t *dst, uint32_t partialWidth, uint32_t partialHeight, uint32_t stride,
           const Decoder &decoder) {
    static uint8_t tmpBuffer[blockSize * blockSize * pixelSize];
    decoder((void *) src, (void *) tmpBuffer, blockSize * pixelSize);

    for (int y = 0; y < partialHeight; y++) {
        memcpy(dst + y * stride, &tmpBuffer[y * blockSize], partialWidth * blockSize);
    }
}

template<size_t bytesPerBlock, size_t blockSize, size_t pixelSize>
bool convertBCn(const uint8_t *src, uint8_t *output, uint32_t width, uint32_t height, const Decoder &decoder) {
    const uint32_t stride = width * pixelSize;
    for (int y = 0; y < height; y += blockSize) {
        for (int x = 0; x < width; x += blockSize) {
            if (x >= width) {
                continue;
            }

            uint8_t *dst = output + y * stride + x * pixelSize;

            if (y + blockSize <= height && x + blockSize <= width) {
                decoder((void *) src, dst, (int) stride);
            } else {
                uint32_t partialWidth = std::min(width - x, (uint32_t) blockSize);
                uint32_t partialHeight = std::min(height - y, (uint32_t) blockSize);
                partialBCn<blockSize, pixelSize>(src, dst, partialWidth, partialHeight, stride, decoder);
            }

            src += bytesPerBlock;
        }
    }
    return true;
}

template<size_t pixelSize>
bool convertBC6(const uint8_t *src, uint8_t *output, uint32_t width, uint32_t height, const Decoder &decoder) {
    const uint32_t stride = width * pixelSize;
    for (int y = 0; y < height; y += 4) {
        uint8_t *dstRow = &output[y * stride];
        for (int x = 0; x < width; x += 4) {
            if (x >= width) {
                continue;
            }
            uint8_t *dst = dstRow + x * pixelSize;
            if (y + 4 <= height && x + 4 <= width) {
                decoder((void *) src, dst, (int) stride / 4);
            } else {
                uint32_t partialWidth = std::min(width - x, (uint32_t) 4);
                uint32_t partialHeight = std::min(height - y, (uint32_t) 4);
                static uint8_t tmpBuffer[4 * 4 * pixelSize];
                // BCDEC BC6 expects pitch in component count, not in byte count
                decoder((void *) src, tmpBuffer, (int) stride / 4);

                for (int y = 0; y < partialHeight; y++) {
                    memcpy(dst + y * stride, &tmpBuffer[y * 4], partialWidth * 4);
                }
            }
            src += BCDEC_BC6H_BLOCK_SIZE;
        }
    }
    return true;
}

void bcdec_bc6h_half_unsigned(const void *compressedBlock, void *decompressedBlock, int destinationPitch) {
    bcdec_bc6h_half(compressedBlock, decompressedBlock, destinationPitch, false);
}

#endif //PYLIB_BCDEC_HELPER_H
