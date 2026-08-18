/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ *
 *  Test function signature
 * ------------------------------------------------------------------ */

typedef void (*hil_test_func_t)(void);

/* ------------------------------------------------------------------ *
 *  Test registry entry — one per test case
 * ------------------------------------------------------------------ */

typedef struct hil_test_entry_s {
    const char      *name;   /* human-readable test name (TAP output) */
    hil_test_func_t  func;   /* test function to call                  */
    int              result; /* 0 = not run, 1 = pass, -1 = fail       */
} hil_test_entry_t;

/* ------------------------------------------------------------------ *
 *  Assert macros — record failures and keep going
 * ------------------------------------------------------------------ */

/*
 * Current test name (set by the runner before each test).
 * Assert macros reference this to produce useful diagnostics.
 */
extern const char *hil_current_test;

/*
 * Incremented on every HIL_ASSERT_* / HIL_FAIL / HIL_SKIP inside a test.
 * Reset to zero by the runner before each test.
 */
extern int hil_fail_count;

/*
 * HIL_SKIP(reason) — mark the current test as skipped.
 * Prints a TAP "ok N # SKIP reason" line and returns from the test function.
 */
#define HIL_SKIP(reason) do {                                   \
    printf("ok - # SKIP %s: %s\n", hil_current_test, reason);   \
    return;                                                     \
} while (0)

/*
 * HIL_FAIL(msg) — unconditional failure with a message.
 */
#define HIL_FAIL(msg) do {                                          \
    hil_fail_count++;                                               \
    printf("# FAIL [%s] %s\n", hil_current_test, msg);              \
} while (0)

/*
 * HIL_ASSERT(cond) — record failure if cond is false.
 */
#define HIL_ASSERT(cond) do {                                       \
    if (!(cond)) {                                                  \
        hil_fail_count++;                                           \
        printf("# FAIL [%s] assertion failed: %s\n",                \
               hil_current_test, #cond);                            \
    }                                                               \
} while (0)

/*
 * HIL_ASSERT_TRUE(cond)  — convenience alias for HIL_ASSERT.
 */
#define HIL_ASSERT_TRUE(cond)  HIL_ASSERT(cond)

/*
 * HIL_ASSERT_FALSE(cond) — assert that cond is false.
 */
#define HIL_ASSERT_FALSE(cond) HIL_ASSERT(!(cond))

/*
 * HIL_ASSERT_EQ(a, b) — assert that (a) == (b).
 * Prints both values on failure (as 32-bit unsigned hex).
 */
#define HIL_ASSERT_EQ(a, b) do {                                    \
    uint32_t _va = (uint32_t)(a);                                   \
    uint32_t _vb = (uint32_t)(b);                                   \
    if (_va != _vb) {                                               \
        hil_fail_count++;                                           \
        printf("# FAIL [%s] %s == %s: expected 0x%lx got 0x%lx\n",  \
               hil_current_test, #a, #b,                            \
               (unsigned long)_vb, (unsigned long)_va);              \
    }                                                               \
} while (0)

/*
 * HIL_ASSERT_NE(a, b) — assert that (a) != (b).
 */
#define HIL_ASSERT_NE(a, b) do {                                    \
    uint32_t _va = (uint32_t)(a);                                   \
    uint32_t _vb = (uint32_t)(b);                                   \
    if (_va == _vb) {                                               \
        hil_fail_count++;                                           \
        printf("# FAIL [%s] %s != %s: both are 0x%lx\n",            \
               hil_current_test, #a, #b, (unsigned long)_va);       \
    }                                                               \
} while (0)

/*
 * HIL_INFO(fmt, ...) — informational output, prefixed with "#".
 */
#define HIL_INFO(fmt, ...) \
    printf("# [%s] " fmt "\n", hil_current_test, ##__VA_ARGS__)

/* ------------------------------------------------------------------ *
 *  Framework API — called from main
 * ------------------------------------------------------------------ */

/*
 * Get the test registry array and its length.
 * Defined in hil_test.c.
 */
extern hil_test_entry_t hil_test_registry[];
extern const unsigned    hil_test_count;

/*
 * Run all registered tests, print TAP header/footer, report summary.
 * Returns 0 when all tests pass, non-zero on any failure.
 */
int hil_test_run_all(void);

/*
 * Run a single test by registry index.
 * Returns 0 on pass, 1 on failure, -1 on invalid index.
 */
int hil_test_run_one(unsigned index);

/*
 * Print a snapshot summary of the tests completed so far.
 * Used by the soft-reset test before it reboots the board.
 */
void hil_test_print_summary_so_far(void);

/* ------------------------------------------------------------------ *
 *  Helper: millisecond busy-wait (reuses system delay)
 * ------------------------------------------------------------------ */

void hil_delay_ms(unsigned ms);
