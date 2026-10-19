#include "nj_test.h"

#include "nj/texture.h"

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

static void colour_block(uint8_t *b, uint16_t c0, uint16_t c1, uint32_t indices) {
    put16(b, c0);
    put16(b + 2, c1);
    put32(b + 4, indices);
}

NJ_TEST(atc_sizes) {
    NJ_EQ_U(nj_atc_size(NJ_ATC_RGB, 8, 8), 32);
    NJ_EQ_U(nj_atc_size(NJ_ATC_RGBA_INTERPOLATED, 8, 8), 64);
    NJ_EQ_U(nj_atc_size(NJ_ATC_RGBA_EXPLICIT, 5, 3), 32);
    NJ_CHECK(nj_atc_format(0x8C92) && nj_atc_format(0x8C93) && nj_atc_format(0x87EE));
    NJ_CHECK(!nj_atc_format(0x8D64));
}

NJ_TEST(atc_rgb_mode0_solid) {
    uint8_t blk[8], out[4 * 4 * 4];
    colour_block(blk, 0x7C00 /* RGB555 red, mode 0 */, 0xF800 /* RGB565 red */, 0);
    nj_atc_decode(NJ_ATC_RGB, 4, 4, blk, out);
    for (int i = 0; i < 16; i++) {
        NJ_EQ_U(out[4 * i + 0], 255);
        NJ_EQ_U(out[4 * i + 1], 0);
        NJ_EQ_U(out[4 * i + 2], 0);
        NJ_EQ_U(out[4 * i + 3], 255);
    }
}

NJ_TEST(atc_rgb_mode1_black_and_colours) {
    uint8_t blk[8], out[64];
    /* mode 1 (bit 15): index 0 = black, 2 = c0, 3 = c1; pixel0 idx0, pixel1 idx2, pixel2 idx3 */
    colour_block(blk, 0x8000 | 0x001F /* blue */, 0x07E0 /* green */, (2u << 2) | (3u << 4));
    nj_atc_decode(NJ_ATC_RGB, 4, 4, blk, out);
    NJ_EQ_U(out[0] | out[1] | out[2], 0);
    NJ_EQ_U(out[4 + 2], 255); /* pixel 1 blue */
    NJ_EQ_U(out[4 + 0], 0);
    NJ_EQ_U(out[8 + 1], 255); /* pixel 2 green */
}

NJ_TEST(atc_palette_mid) {
    uint8_t blk[8], out[64];
    colour_block(blk, 0x7FFF /* white */, 0x0000 /* black */, 1u /* pixel 0 -> index 1 */);
    nj_atc_decode(NJ_ATC_RGB, 4, 4, blk, out);
    NJ_EQ_U(out[0], (5 * 255 + 3 * 0) / 8); /* 159 */
    NJ_EQ_U(out[1], 159);
    NJ_EQ_U(out[2], 159);
}

NJ_TEST(atc_rgba_interp_alpha) {
    uint8_t blk[16] = {0}, out[64];
    blk[0] = 255; /* a0 */
    blk[1] = 0;   /* a1 */
    blk[2] = 0x08; /* pixel 0 index 0 (a0), pixel 1 index 1 (a1) */
    colour_block(blk + 8, 0x7FFF, 0x7FFF, 0);
    nj_atc_decode(NJ_ATC_RGBA_INTERPOLATED, 4, 4, blk, out);
    NJ_EQ_U(out[3], 255);
    NJ_EQ_U(out[4 + 3], 0);
    NJ_EQ_U(out[0], 255); /* colour still decoded */
}

NJ_TEST(atc_rgba_explicit_alpha) {
    uint8_t blk[16] = {0}, out[64];
    blk[0] = 0xF0; /* pixel 0 alpha 0, pixel 1 alpha 15 */
    colour_block(blk + 8, 0x7FFF, 0x7FFF, 0);
    nj_atc_decode(NJ_ATC_RGBA_EXPLICIT, 4, 4, blk, out);
    NJ_EQ_U(out[3], 0);
    NJ_EQ_U(out[4 + 3], 255);
}

NJ_TEST(atc_edge_clip) {
    uint8_t src[16], out[5 * 3 * 4 + 1];
    colour_block(src, 0x7C00, 0xF800, 0);
    colour_block(src + 8, 0x001F, 0x001F, 0);
    out[5 * 3 * 4] = 0xA5; /* guard */
    nj_atc_decode(NJ_ATC_RGB, 5, 3, src, out);
    NJ_EQ_U(out[5 * 3 * 4], 0xA5);
    NJ_EQ_U(out[(2 * 5 + 4) * 4 + 2], 255); /* last pixel comes from the second (blue) block */
    NJ_EQ_U(out[(2 * 5 + 3) * 4 + 0], 255); /* pixel x=3 is red */
}
