/**
 * @file test.h
 * @brief What every host test binary shares: one assertion, and its tally.
 *
 * Each suite is a binary run by run.sh, built from its test files and this
 * harness; a suite exits non-zero when any check failed. A check prints one
 * line either way, so the run reads as a list of what was proved.
 */
#pragma once

/** Checks failed so far in this binary. A suite's exit status is whether it is 0. */
extern int failures;

/** Print "[PASS] what" or "[FAIL] what", and count a failure. */
void check(const char* what, int ok);
