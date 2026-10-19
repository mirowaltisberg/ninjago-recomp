#include "nj_test.h"

nj_test_entry nj_tests[NJ_MAX_TESTS];
int nj_test_count;
int nj_test_failed;
int nj_test_skipped;
jmp_buf nj_test_jmp;
jmp_buf nj_test_fatal_jb;
char nj_test_fatal_msg[1024];

void nj_test_fatal_to_jmp(const char *msg) {
    snprintf(nj_test_fatal_msg, sizeof nj_test_fatal_msg, "%s", msg);
    longjmp(nj_test_fatal_jb, 1);
}

int main(int argc, char **argv) {
    int failures = 0, skips = 0, ran = 0;
    for (int i = 0; i < nj_test_count; i++) {
        if (argc > 1 && strcmp(argv[1], nj_tests[i].name) != 0) continue;
        ran++;
        nj_test_failed = nj_test_skipped = 0;
        fprintf(stderr, "[ RUN  ] %s\n", nj_tests[i].name);
        if (setjmp(nj_test_jmp) == 0) nj_tests[i].fn();
        nj_fatal_hook = NULL;
        if (nj_test_failed) { failures++; fprintf(stderr, "[ FAIL ] %s\n", nj_tests[i].name); }
        else if (nj_test_skipped) { skips++; fprintf(stderr, "[ SKIP ] %s\n", nj_tests[i].name); }
        else fprintf(stderr, "[  OK  ] %s\n", nj_tests[i].name);
    }
    fprintf(stderr, "%d run, %d failed, %d skipped\n", ran, failures, skips);
    if (ran == 0) return 1;
    if (failures) return 1;
    return (skips == ran) ? 77 : 0;
}
