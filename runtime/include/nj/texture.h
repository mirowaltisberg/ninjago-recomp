/* Software decoding of texture formats iOS GPUs lack (Qualcomm ATC). */
#ifndef NJ_TEXTURE_H
#define NJ_TEXTURE_H

#include <stddef.h>
#include <stdint.h>

#define NJ_ATC_RGB 0x8C92u                    /* GL_ATC_RGB_AMD */
#define NJ_ATC_RGBA_EXPLICIT 0x8C93u          /* GL_ATC_RGBA_EXPLICIT_ALPHA_AMD */
#define NJ_ATC_RGBA_INTERPOLATED 0x87EEu      /* GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD */

/* 1 if internalformat is one of the three ATC formats. */
int nj_atc_format(uint32_t internalformat);
/* Compressed size in bytes of a w x h image. */
size_t nj_atc_size(uint32_t fmt, int w, int h);
/* Decode to RGBA8, row-major w x h (edge blocks clipped). */
void nj_atc_decode(uint32_t fmt, int w, int h, const uint8_t *src, uint8_t *rgba);

#endif
