/* Qualcomm ATC block decoding (layout as in bimg's decodeBlockAtc). 4x4 blocks:
 * colour block (8 bytes): c0 u16 RGB555 (bit 15 selects the mode), c1 u16 RGB565, 32-bit 2-bit indices LSB first.
 * ATC_RGBA_EXPLICIT: 8 bytes of 4-bit alphas, then a colour block.
 * ATC_RGBA_INTERPOLATED: DXT5-style alpha block (a0, a1, 48-bit 3-bit indices), then a colour block. */
#include <string.h>

#include "nj/texture.h"

int nj_atc_format(uint32_t f) { return f == NJ_ATC_RGB || f == NJ_ATC_RGBA_EXPLICIT || f == NJ_ATC_RGBA_INTERPOLATED; }

static int block_bytes(uint32_t f) { return f == NJ_ATC_RGB ? 8 : 16; }

size_t nj_atc_size(uint32_t f, int w, int h) {
    size_t bw = (size_t)(w + 3) / 4, bh = (size_t)(h + 3) / 4;
    return bw * bh * (size_t)block_bytes(f);
}

static uint8_t x5(uint32_t v) { return (uint8_t)((v << 3) | (v >> 2)); }
static uint8_t x6(uint32_t v) { return (uint8_t)((v << 2) | (v >> 4)); }
static uint8_t sat_sub(int a, int b) { return (uint8_t)(a - b < 0 ? 0 : a - b); }

/* Writes 16 RGB triplets (alpha untouched) into px[16][4]. */
static void colour(const uint8_t *b, uint8_t px[16][4]) {
    uint32_t c0 = (uint32_t)(b[0] | b[1] << 8), c1 = (uint32_t)(b[2] | b[3] << 8);
    uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
    uint8_t pal[4][3];
    uint8_t r0 = x5((c0 >> 10) & 0x1f), g0 = x5((c0 >> 5) & 0x1f), b0 = x5(c0 & 0x1f);
    uint8_t r1 = x5((c1 >> 11) & 0x1f), g1 = x6((c1 >> 5) & 0x3f), b1 = x5(c1 & 0x1f);
    if (!(c0 & 0x8000)) {
        pal[0][0] = r0; pal[0][1] = g0; pal[0][2] = b0;
        pal[1][0] = (uint8_t)((5 * r0 + 3 * r1) / 8); pal[1][1] = (uint8_t)((5 * g0 + 3 * g1) / 8);
        pal[1][2] = (uint8_t)((5 * b0 + 3 * b1) / 8);
        pal[2][0] = (uint8_t)((3 * r0 + 5 * r1) / 8); pal[2][1] = (uint8_t)((3 * g0 + 5 * g1) / 8);
        pal[2][2] = (uint8_t)((3 * b0 + 5 * b1) / 8);
    } else {
        pal[0][0] = pal[0][1] = pal[0][2] = 0;
        pal[1][0] = sat_sub(r0, r1 / 4); pal[1][1] = sat_sub(g0, g1 / 4); pal[1][2] = sat_sub(b0, b1 / 4);
        pal[2][0] = r0; pal[2][1] = g0; pal[2][2] = b0;
    }
    pal[3][0] = r1; pal[3][1] = g1; pal[3][2] = b1;
    for (int i = 0; i < 16; i++) {
        uint32_t k = (idx >> (2 * i)) & 3;
        memcpy(px[i], pal[k], 3);
    }
}

static void alpha_explicit(const uint8_t *b, uint8_t px[16][4]) {
    for (int i = 0; i < 16; i++) {
        uint8_t nib = (uint8_t)((b[i / 2] >> (4 * (i & 1))) & 0xf);
        px[i][3] = (uint8_t)(nib * 17);
    }
}

static void alpha_interpolated(const uint8_t *b, uint8_t px[16][4]) {
    int a0 = b[0], a1 = b[1];
    uint8_t pal[8];
    pal[0] = (uint8_t)a0;
    pal[1] = (uint8_t)a1;
    if (a0 > a1) {
        for (int i = 2; i < 8; i++) pal[i] = (uint8_t)(((8 - i) * a0 + (i - 1) * a1) / 7);
    } else {
        for (int i = 2; i < 6; i++) pal[i] = (uint8_t)(((6 - i) * a0 + (i - 1) * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) px[i][3] = pal[(bits >> (3 * i)) & 7];
}

void nj_atc_decode(uint32_t f, int w, int h, const uint8_t *src, uint8_t *rgba) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4, bb = block_bytes(f);
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++) {
            const uint8_t *b = src + (size_t)(by * bw + bx) * (size_t)bb;
            uint8_t px[16][4];
            for (int i = 0; i < 16; i++) px[i][3] = 255;
            if (f == NJ_ATC_RGB) colour(b, px);
            else {
                colour(b + 8, px);
                if (f == NJ_ATC_RGBA_EXPLICIT) alpha_explicit(b, px);
                else alpha_interpolated(b, px);
            }
            for (int y = 0; y < 4; y++) {
                int py = by * 4 + y;
                if (py >= h) break;
                for (int x = 0; x < 4; x++) {
                    int pxx = bx * 4 + x;
                    if (pxx >= w) break;
                    memcpy(rgba + ((size_t)py * (size_t)w + (size_t)pxx) * 4, px[y * 4 + x], 4);
                }
            }
        }
}
