/**
 * @file flipso_names.h
 * @brief The long name tables, read from the SD card on demand.
 *
 * On the device the names in itso/names/ - payment means, profiles, railcards,
 * operators and the rest - come from assets/names.dat rather than from the
 * .fap, so they cost no heap while nothing is looking them up. The itso_
 * functions that answer them (flipso_names_itso.c) read the file through here.
 *
 * Opening the file and holding it open costs about 1 KB, so it is opened by
 * the first lookup and closed by flipso_names_release(), which each scene calls
 * once it has built what it shows. A name is copied into one of
 * FLIPSO_NAMES_SLOTS buffers that are reused in turn, so it lasts until that
 * many more names have been looked up, or until the release: use it, or copy
 * it, before then. Lookups belong to the UI thread.
 */
#pragma once

#include "flipso_names_file.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How many names can be in use at once. */
#define FLIPSO_NAMES_SLOTS 8

/** What a total table answers when the file cannot be read. */
#define FLIPSO_NAMES_UNKNOWN "Unknown"

typedef struct FlipsoNames FlipsoNames;

/** The app's one instance, which the lookups below use until it is freed. */
FlipsoNames* flipso_names_alloc(void);
void flipso_names_free(FlipsoNames* instance);

/**
 * Close the file and free the buffers the names are in. Every name looked up so
 * far is gone; the next lookup opens the file again.
 */
void flipso_names_release(void);

/** The name for @p code in a byte table, or NULL when it has none or the file cannot be read. */
const char* flipso_names_byte(FlipsoNamesTable table, uint8_t code);

/**
 * The name for @p key in a keyed table, or NULL.
 * @param extra set to the entry's flag when there is one. May be NULL.
 */
const char* flipso_names_keyed(FlipsoNamesTable table, uint32_t key, uint8_t* extra);

#ifdef __cplusplus
}
#endif
