/**
 * @file flipso_saved.h
 * @brief Saved cards on the SD card: where they live, and how they get there.
 *
 * A saved card is one file of the raw blocks a read produced - see
 * flipso_capture.h for why those rather than the decoded fields. This is only
 * the file handling around that: the folder, the naming, and reading and
 * writing a line at a time so that neither direction needs the whole file in
 * memory at once. The longest line a card produces is about 1.5 KB, which is
 * more than the app's 4 KB stack should carry, so the buffer is on the heap.
 *
 * A saved card carries the card number and, where the card has an ITSO ID on
 * it, the holder's name. They are the user's own cards and the files stay on
 * their SD card, but that is what is in them.
 */
#pragma once

#include "flipso_capture.h"

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Alongside the operator table rather than in it: these are the user's data. */
#define FLIPSO_SAVED_FOLDER    APP_DATA_PATH("cards")
#define FLIPSO_SAVED_EXTENSION ".flipso"

/**
 * Longest name the save screen accepts, terminator included.
 *
 * The limit is the file browser rather than the file system: a longer name is
 * elided on a 128px row, so the part that distinguishes two cards would be the
 * part that is not shown.
 */
#define FLIPSO_SAVED_NAME_LEN 28

/** Create the saved-cards folder if it is not there. */
void flipso_saved_mkdir(void);

/** True when there is at least one saved card to offer. */
bool flipso_saved_any(void);

/** Build the full path of the card saved under @p name. */
void flipso_saved_path(FuriString* path, const char* name);

/** The name a saved card is shown under: its file name, less the extension. */
void flipso_saved_name(FuriString* name, const char* path);

/**
 * Propose a name for a card that has just been read.
 *
 * The card's branding and the last four digits of its number, which is what
 * distinguishes two cards from the same scheme in a list of them. Characters a
 * file name cannot carry are dropped rather than substituted, so the suggestion
 * is always usable as it stands.
 */
void flipso_saved_suggest_name(
    char* out,
    size_t out_len,
    const ItsoCard* card,
    const char* brand);

/**
 * Write a capture to @p path, creating the folder if need be.
 *
 * A write that fails part way takes the file with it: a half-written card would
 * load as a card with pieces missing rather than as a card that is not there.
 */
bool flipso_saved_write(const FlipsoCapture* capture, const char* path);

/**
 * Read a saved card into @p capture, which is reset first.
 *
 * @return false when the file is missing, is not a saved card, or was written
 *         by a later version of Flipso. A file that is ours but has a damaged
 *         block loads without it: see flipso_capture_parse_line().
 */
bool flipso_saved_read(FlipsoCapture* capture, const char* path);

/**
 * Find the saved card that holds the same card as @p capture.
 *
 * Matched on the card number, not the file name: the point of looking is that
 * the same physical card has been read again, and what has changed since is the
 * products on it. The user may have called the file anything.
 *
 * Only the header of each candidate is read - enough to reach its shell - so
 * the walk costs a few lines per saved card rather than a whole file.
 *
 * @param[out] path    the file, when one matches. Untouched otherwise.
 * @param[out] read_at when that record was read, or NULL if the caller does not
 *                     care. It is worth showing: it says how stale the record
 *                     being replaced is.
 * @return false when no saved card holds this one.
 */
bool flipso_saved_find(const FlipsoCapture* capture, FuriString* path, uint32_t* read_at);

/**
 * Let the user choose a saved card.
 *
 * Blocks until they pick one or press Back, so it is called from a scene's
 * enter handler rather than from a draw callback.
 *
 * @return false when they backed out; @p path is untouched in that case.
 */
bool flipso_saved_pick(FuriString* path);

bool flipso_saved_delete(const char* path);

/**
 * Show a blocking "something went wrong" dialog with a single OK button.
 *
 * Saving and loading fail for reasons outside the app - no SD card, a full one,
 * a file someone has edited - and each of them is a dead end rather than a
 * screen to navigate. A scene of its own for something the user can only
 * acknowledge would cost more than it explains.
 */
void flipso_saved_alert(const char* header, const char* text);

#ifdef __cplusplus
}
#endif
