/* Minimal test harness: NJ_TEST(name) { ... } registers a test; each test file is one executable.
 * Run all tests, or one: ./test_x name. NJ_SKIP("why") marks a skip (exit code 77 if all skipped). */
#ifndef NJ_TEST_H
#define NJ_TEST_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/guest.h"

typedef void (*nj_test_fn)(void);
typedef struct { const char *name; nj_test_fn fn; } nj_test_entry;

#define NJ_MAX_TESTS 256
extern nj_test_entry nj_tests[NJ_MAX_TESTS];
extern int nj_test_count;
extern int nj_test_failed;
extern int nj_test_skipped;
extern jmp_buf nj_test_jmp;
extern char nj_test_fatal_msg[1024];

#define NJ_TEST(tname)                                                         \
    static void tname(void);                                                   \
    __attribute__((constructor)) static void nj_register_##tname(void) {       \
        nj_tests[nj_test_count].name = #tname;                                 \
        nj_tests[nj_test_count++].fn = tname;                                  \
    }                                                                          \
    static void tname(void)

#define NJ_FAIL(...)                                                           \
    do {                                                                       \
        fprintf(stderr, "  %s:%d: ", __FILE__, __LINE__);                      \
        fprintf(stderr, __VA_ARGS__);                                          \
        fprintf(stderr, "\n");                                                 \
        nj_test_failed = 1;                                                    \
        longjmp(nj_test_jmp, 1);                                               \
    } while (0)

#define NJ_CHECK(cond) do { if (!(cond)) NJ_FAIL("check failed: %s", #cond); } while (0)
#define NJ_EQ_U(a, b)                                                          \
    do {                                                                       \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        if (_a != _b) NJ_FAIL("%s == %s: 0x%llx != 0x%llx", #a, #b, _a, _b);   \
    } while (0)
#define NJ_EQ_STR(a, b)                                                        \
    do {                                                                       \
        const char *_a = (a), *_b = (b);                                       \
        if (!_a || !_b || strcmp(_a, _b) != 0)                                 \
            NJ_FAIL("%s == %s: \"%s\" != \"%s\"", #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
    } while (0)
#define NJ_SKIP(why) do { fprintf(stderr, "  skip: %s\n", why); nj_test_skipped = 1; longjmp(nj_test_jmp, 2); } while (0)

/* Runs stmt and expects nj_fatal() to be called with a message containing substr. */
#define NJ_EXPECT_FATAL(stmt, substr)                                          \
    do {                                                                       \
        nj_test_fatal_msg[0] = 0;                                              \
        nj_fatal_hook = nj_test_fatal_to_jmp;                                  \
        if (setjmp(nj_test_fatal_jb) == 0) {                                   \
            stmt;                                                              \
            nj_fatal_hook = NULL;                                              \
            NJ_FAIL("expected nj_fatal containing \"%s\"", substr);            \
        }                                                                      \
        nj_fatal_hook = NULL;                                                  \
        if (!strstr(nj_test_fatal_msg, substr))                                \
            NJ_FAIL("fatal message \"%s\" lacks \"%s\"", nj_test_fatal_msg, substr); \
    } while (0)

extern jmp_buf nj_test_fatal_jb;
void nj_test_fatal_to_jmp(const char *msg);

/* Path helpers for tests needing game inputs (set by CMake). */
#ifndef NJ_GUEST_SO_PATH
#define NJ_GUEST_SO_PATH ""
#endif
#ifndef NJ_GUEST_OBB_PATH
#define NJ_GUEST_OBB_PATH ""
#endif

#endif
