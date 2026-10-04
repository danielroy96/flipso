/**
 * @file flipso_name_validator.h
 * @brief The check a saved card's name has to pass before it is written.
 */
#pragma once

#include "flipso_saved.h"

#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Text input validator for the name of a saved card, used by both the screen
 * that names one and the screen that renames one.
 *
 * Rejects the characters a file name cannot carry, then defers to the
 * firmware's own check for a name already taken. The two have to be one
 * callback because a text input holds only one, and catching a bad character
 * here rather than at the write is the difference between saying what is wrong
 * and reporting a failure the user cannot explain.
 */
typedef struct FlipsoNameValidator FlipsoNameValidator;

/**
 * @param current_name the card's name when renaming it, or "" when naming a new
 *                     one. Keeping that name is allowed, and so is changing
 *                     only its case, which the SD card's file system would
 *                     otherwise report as a clash with the card itself.
 */
FlipsoNameValidator* flipso_name_validator_alloc(const char* current_name);
void flipso_name_validator_free(FlipsoNameValidator* validator);

/** The TextInputValidatorCallback; @p context is a FlipsoNameValidator. */
bool flipso_name_validator(const char* text, FuriString* error, void* context);

#ifdef __cplusplus
}
#endif
