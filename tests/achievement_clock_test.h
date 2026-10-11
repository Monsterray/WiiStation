#ifndef ACHIEVEMENT_CLOCK_TEST_H
#define ACHIEVEMENT_CLOCK_TEST_H
#include <stdint.h>
#include <stdio.h>
int achievement_clock_tls_install(uint64_t (*milliseconds)(void));
int achievement_clock_test(FILE *report);
#endif
