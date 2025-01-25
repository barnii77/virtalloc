#ifndef TESTING_H
#define TESTING_H

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#define printAndFlush(msg, ...) do {printf(msg, ##__VA_ARGS__); fflush(stdout);} while (0)

#define MAKE_INVERTED_TEST(test_name, inner_test_name) \
static int test_name(void) { \
    return !inner_test_name(); \
}

static int print_on_pass; // = 0;
static int print_pre_run_msg; // = 0;
static int run_all_tests; // = 1;
static int n_test_reps; // = 1;
static const char *selected_test; // = "";

#define REGISTER_TEST_CASE(test_name) \
{ \
    *names = realloc(*names, (n_test_cases + 1) * sizeof(char *)); \
    (*names)[n_test_cases] = #test_name; \
    *tests = realloc(*tests, (n_test_cases + 1) * sizeof(int (*)(void))); \
    (*tests)[n_test_cases] = test_name; \
    n_test_cases++; \
}

static void init_static_vars(void);

static int build_test_cases(const char ***names, int (***tests)(void));

#define BEGIN_TEST_LIST() \
static int build_test_cases(const char ***names, int (***tests)(void)) { \
    int n_test_cases = 0; \
    *names = malloc(1 * sizeof(char *)); \
    *tests = malloc(1 * sizeof(int (*)(void))); \
    // Evil macro magic

#define END_TEST_LIST() \
    return n_test_cases; \
}

#define BEGIN_RUNNER_SETTINGS() \
static void init_static_vars() {

#define END_RUNNER_SETTINGS() \
}

#define MAKE_TEST_SUITE_RUNNABLE() \
int main(void) { \
    run_tests(); \
    return 0; \
}

static void run_tests(void) {
    init_static_vars();
    const char **names;
    int (**tests)(void);
    int n_tests = build_test_cases(&names, &tests);
    int tests_always_passed = 1;
    int n_total_failed = 0;
    int n_tests_run = 0;
    for (int k = 0; k < n_test_reps; k++) {
        int all_passed = 1;
        int n_failed = 0;
        for (int i = 0; i < n_tests; i++) {
            const char *name = names[i];
            if (!run_all_tests && strcmp(name, selected_test) != 0)
                continue;
            n_tests_run++;
            if (print_pre_run_msg) printAndFlush("Running test %s...\n", name);
            int err = tests[i]();
            if (err) {
                all_passed = 0;
                tests_always_passed = 0;
                n_failed++;
                n_total_failed++;
                printAndFlush("Test %s... Error: Code 0x%X (%d)\n", name, err, err);
            } else if (print_on_pass) {
                printAndFlush("Test %s... Passed\n", name);
            }
        }
        if (n_test_reps > 1 && all_passed) printAndFlush("All tests passed this iteration!\n");
        else if (n_test_reps > 1) printAndFlush("%d/%d tests failed this iteration!\n", n_failed, n_tests_run);
    }
    if (tests_always_passed) printAndFlush("*** ALL %d TEST RUNS OF %d TESTS PASSED (%d SUCCESSFUL RUNS / TEST)! ***\n",
                                           n_tests_run, n_test_reps * (run_all_tests ? n_tests : 1), n_test_reps);
    else printAndFlush("*** %d/%d TEST RUNS OF %d TESTS FAILED OVERALL! ***\n", n_total_failed, n_tests_run, n_tests);
    free(tests);
    free(names);
}

#endif
