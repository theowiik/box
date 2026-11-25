// A minimal test harness: CHECK records a failure and keeps going.
#pragma once

#include <math.h>
#include <stdio.h>
#include <string.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_STR(a, b) CHECK(strcmp((a), (b)) == 0)
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

#define RUN(test)                          \
    do {                                   \
        const int before = g_failures;     \
        test();                            \
        printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", #test); \
    } while (0)

void run_render_tests(void);
void run_event_tests(void);
void run_scheduler_tests(void);
void run_led_tests(void);
