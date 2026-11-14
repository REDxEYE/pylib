//Taken from https://github.com/K0lb3/texture2ddecoder

#ifndef ETC_H
#define ETC_H

#include <cstdint>

// All decoders read ceil(w/4) * ceil(h/4) 4x4 blocks from `data` (8 bytes per
// block for ETC1/ETC2/ETC2A1/EAC R11, 16 for ETC2A8/EAC RG11) and write w*h
// tightly packed RGBA8 pixels (byte order R, G, B, A) to `image`.
// EAC outputs: R11 -> R in byte 0, G = B = 0, A = 255; RG11 -> R, G in bytes 0/1,
// B = 0, A = 255. Signed variants store (value + 1) * 127.5 approximately,
// i.e. biased unsigned bytes (0 ~ -1.0, 127 ~ 0.0, 254..255 ~ +1.0).
void decode_etc1(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_etc2(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_etc2a1(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_etc2a8(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_eacr(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_eacr_signed(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_eacrg(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);
void decode_eacrg_signed(const uint8_t *data, uint32_t w, uint32_t h, uint32_t *image);

#endif /* end of include guard: ETC_H */
