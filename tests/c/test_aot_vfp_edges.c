#include <math.h>

#include "nj_test.h"

#include "recomp_rt.h"

static uint32_t nzcv(const NjCtx *c) { return c->fpscr >> 28; }

NJ_TEST(vcvt_saturate) {
    NJ_EQ_U(nj_cvt_s32((double)NAN), 0);
    NJ_EQ_U(nj_cvt_s32((double)3e9f), 0x7FFFFFFFu);
    NJ_EQ_U(nj_cvt_s32((double)-3e9f), 0x80000000u);
    NJ_EQ_U(nj_cvt_s32(-2147483648.0), 0x80000000u);
    NJ_EQ_U(nj_cvt_s32(-1.9), (uint32_t)-1);
    NJ_EQ_U(nj_cvt_s32((double)INFINITY), 0x7FFFFFFFu);
    NJ_EQ_U(nj_cvt_u32((double)-1.0f), 0);
    NJ_EQ_U(nj_cvt_u32(-0.5), 0);
    NJ_EQ_U(nj_cvt_u32(5e9), 0xFFFFFFFFu);
    NJ_EQ_U(nj_cvt_u32(4294967295.9), 0xFFFFFFFFu);
    NJ_EQ_U(nj_cvt_u32((double)NAN), 0);
}

NJ_TEST(vcmp_flags) {
    NjCtx c = {0};
    c.fpscr = 0x0000ABCDu;
    nj_vcmp(&c, (double)NAN, 1.0);
    NJ_EQ_U(nzcv(&c), 0x3);
    NJ_EQ_U(c.fpscr & 0x0FFFFFFFu, 0x0000ABCDu); /* control bits untouched */
    nj_vcmp(&c, -0.0, 0.0);
    NJ_EQ_U(nzcv(&c), 0x6);
    nj_vcmp(&c, 1.0, 2.0);
    NJ_EQ_U(nzcv(&c), 0x8);
    nj_vcmp(&c, 2.0, 1.0);
    NJ_EQ_U(nzcv(&c), 0x2);
    nj_fmstat(&c);
    NJ_CHECK(!c.n && !c.z && c.c && !c.v);
}
