#include "nj_test.h"

#include "nj/guest.h"

NJ_TEST(mem_map_roundtrip) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map(NJ_HEAP_BASE, 64 * 1024, NJ_PROT_R | NJ_PROT_W), 0);
    nj_w32(NJ_HEAP_BASE + 4, 0xdeadbeefu);
    NJ_EQ_U(nj_r32(NJ_HEAP_BASE + 4), 0xdeadbeefu);
    NJ_EQ_U(*(uint32_t *)nj_host(NJ_HEAP_BASE + 4), 0xdeadbeefu);
    NJ_CHECK(nj_mem_is_mapped(NJ_HEAP_BASE, 64 * 1024));
    NJ_CHECK(!nj_mem_is_mapped(NJ_HEAP_BASE, 64 * 1024 + 1));
}

NJ_TEST(mem_unaligned) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map(NJ_HEAP_BASE, 4096, NJ_PROT_R | NJ_PROT_W), 0);
    nj_w32(NJ_HEAP_BASE + 13, 0x11223344u);
    NJ_EQ_U(nj_r32(NJ_HEAP_BASE + 13), 0x11223344u);
    NJ_EQ_U(nj_r8(NJ_HEAP_BASE + 13), 0x44u);
    nj_w64(NJ_HEAP_BASE + 101, 0x0102030405060708ull);
    NJ_EQ_U(nj_r64(NJ_HEAP_BASE + 101), 0x0102030405060708ull);
}

NJ_TEST(mem_null_page_unmapped) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_CHECK(!nj_mem_is_mapped(0, 4));
    NJ_CHECK(!nj_mem_is_mapped(0xFFFC, 4));
}

NJ_TEST(mem_guest_host_inverse) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_guest(nj_host(0x12345678u)), 0x12345678u);
    NJ_EQ_U(nj_guest(nj_host(NJ_RETURN_TRAP)), NJ_RETURN_TRAP);
}

NJ_TEST(mem_fixed_regions) {
    NJ_EQ_U(nj_mem_init(), 0);
    NJ_EQ_U(nj_mem_map_fixed_regions(), 0);
    NJ_CHECK(nj_mem_is_mapped(NJ_HLEDATA_BASE, NJ_HLEDATA_SIZE));
    NJ_CHECK(nj_mem_is_mapped(NJ_STACK_BASE, NJ_STACK_SIZE * NJ_STACK_COUNT));
    NJ_CHECK(nj_mem_is_mapped(NJ_THUNK_BASE, NJ_THUNK_SIZE));
    nj_w32(NJ_STACK_BASE + NJ_STACK_SIZE * NJ_STACK_COUNT - 4, 7);
    NJ_EQ_U(nj_r32(NJ_STACK_BASE + NJ_STACK_SIZE * NJ_STACK_COUNT - 4), 7);
}
