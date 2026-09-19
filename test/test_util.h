/***************************************************************************//**
 * @file
 * @brief Minimal assertion harness for the host-side unit tests.
 *
 * No framework and no dependencies beyond the C standard library, so a test
 * binary builds anywhere the host compiler runs.
 *
 * Usage:
 * @code
 * #include "test_util.h"
 *
 * static void test_something(void)
 * {
 *   TEST_ASSERT(1 + 1 == 2);
 *   TEST_ASSERT_EQ_UINT(answer(), 42U);
 * }
 *
 * int main(void)
 * {
 *   TEST_RUN(test_something);
 *   return TEST_SUMMARY();
 * }
 * @endcode
 ******************************************************************************/

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/// Number of failed checks in this binary.
static int test_failures;

/// Number of checks made in this binary.
static int test_checks;

/// Name of the test case currently running.
static const char *test_current = "";

/***************************************************************************//**
 * Record one check result and report a failure.
 ******************************************************************************/
#define TEST_CHECK_(cond, ...)                                     \
  do {                                                             \
    test_checks++;                                                 \
    if (!(cond)) {                                                 \
      test_failures++;                                             \
      (void)printf("FAIL %s at %s:%d: ", test_current, __FILE__, __LINE__); \
      (void)printf(__VA_ARGS__);                                   \
      (void)printf("\n");                                          \
    }                                                              \
  } while (0)

/// Assert that a condition holds.
#define TEST_ASSERT(cond)  TEST_CHECK_((cond), "%s", #cond)

/// Fail unconditionally with a printf-style explanation.
///
/// For a check inside a loop, where naming what failed is far more useful than
/// the condition that detected it.
#define TEST_FAIL(...)  TEST_CHECK_(0, __VA_ARGS__)

/// Assert that two unsigned values are equal.
#define TEST_ASSERT_EQ_UINT(actual, expected)                          \
  do {                                                                 \
    uint64_t a_ = (uint64_t)(actual);                                  \
    uint64_t e_ = (uint64_t)(expected);                                \
    TEST_CHECK_(a_ == e_, "%s: expected 0x%llX, got 0x%llX",           \
                #actual, (unsigned long long)e_, (unsigned long long)a_); \
  } while (0)

/// Assert that two memory regions hold the same bytes.
#define TEST_ASSERT_EQ_MEM(actual, expected, len)                      \
  do {                                                                 \
    const void *a_ = (actual);                                         \
    const void *e_ = (expected);                                       \
    size_t n_ = (size_t)(len);                                         \
    TEST_CHECK_(memcmp(a_, e_, n_) == 0,                               \
                "%s differs from %s over %zu bytes", #actual, #expected, n_); \
  } while (0)

/// Assert that two NUL-terminated strings are equal.
#define TEST_ASSERT_EQ_STR(actual, expected)                           \
  do {                                                                 \
    const char *a_ = (actual);                                         \
    const char *e_ = (expected);                                       \
    TEST_CHECK_(strcmp(a_, e_) == 0, "%s: expected \"%s\", got \"%s\"", \
                #actual, e_, a_);                                      \
  } while (0)

/// Run one test case function.
#define TEST_RUN(fn)        \
  do {                      \
    test_current = #fn;     \
    fn();                   \
    test_current = "";      \
  } while (0)

/***************************************************************************//**
 * Print the result line and produce the process exit code.
 *
 * @return 0 when every check passed, 1 otherwise.
 ******************************************************************************/
static int test_summary(const char *suite)
{
  if (test_failures == 0) {
    (void)printf("PASS %s: %d checks\n", suite, test_checks);
    return 0;
  }

  (void)printf("FAIL %s: %d of %d checks failed\n",
               suite, test_failures, test_checks);
  return 1;
}

/// Print the summary for this file and return the process exit code.
#define TEST_SUMMARY()  test_summary(__FILE__)

#endif  // TEST_UTIL_H
