/**
 * @file flipso_capture.h
 * @brief The raw bytes a card gave up, kept so a read can be saved and replayed.
 *
 * Saving a card could mean writing out the decoded @c ItsoCard, but that would
 * freeze each saved card at the decoder that wrote it: a fix to the decoder
 * would never reach the cards already on the SD card, and every new field would
 * need a serialiser of its own. What is saved here instead is what the card
 * actually said - the Shell Environment, the Directory, each product's sector
 * chain and the cyclic log - and loading one runs those bytes back through the
 * same decoder a live read uses. A saved card therefore decodes exactly as the
 * card would if it were tapped again, on whatever build is running now.
 *
 * It is the same set of blocks tools/test/replay.py already feeds to the host
 * decoder, so a saved card doubles as a test case: pull one off the device and
 * replay it without the card or the Flipper.
 *
 * Pure computation over byte buffers, with no firmware dependency, so the whole
 * save/load round trip is testable on the host - see tools/test/test_capture.c.
 */
#pragma once

#include "itso/itso.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A shell, a directory, a cyclic log, and one group per directory entry. */
#define FLIPSO_CAPTURE_MAX_BLOCKS (ITSO_MAX_PRODUCTS + 3)

/**
 * Ceiling on the bytes one capture holds.
 *
 * No single block can be longer than the longest sector chain the decoder will
 * assemble, and there are at most FLIPSO_CAPTURE_MAX_BLOCKS of them. The arena
 * grows towards this rather than starting at it: a CMD7 card fills about a
 * tenth of it, and the Flipper has 190 KB of heap for everything.
 */
#define FLIPSO_CAPTURE_MAX_BYTES (FLIPSO_CAPTURE_MAX_BLOCKS * ITSO_MAX_GROUP_LEN)

/**
 * Longest line flipso_capture_line() writes, terminator included.
 *
 * A key, then three characters per byte. Too large for the app's 4 KB stack:
 * the caller allocates this buffer on the heap.
 */
#define FLIPSO_CAPTURE_LINE_MAX (16 + ITSO_MAX_GROUP_LEN * 3)

/** Which data group a block of captured bytes came from. */
typedef enum {
    FlipsoBlockShell, /**< ITSO Shell Environment Data Group. */
    FlipsoBlockDirectory, /**< Directory Data Group. */
    FlipsoBlockProduct, /**< One product's IPE and Value Record groups, chained. */
    FlipsoBlockLog, /**< The cyclic log of Transient Ticket Records. */
} FlipsoBlockKind;

typedef struct FlipsoCapture FlipsoCapture;

FlipsoCapture* flipso_capture_alloc(void);
void flipso_capture_free(FlipsoCapture* capture);

/** Forget the last card, releasing the bytes it held. */
void flipso_capture_reset(FlipsoCapture* capture);

/**
 * True once there is a shell to decode.
 *
 * Below that there is nothing worth saving: without the Shell Environment the
 * other blocks cannot even be placed, because the geometry that describes them
 * is in the shell.
 */
bool flipso_capture_valid(const FlipsoCapture* capture);

/** When the card was read, as a Unix timestamp, or 0 when it was not recorded. */
uint32_t flipso_capture_time(const FlipsoCapture* capture);
void flipso_capture_set_time(FlipsoCapture* capture, uint32_t timestamp);

/**
 * Keep one block of card bytes.
 *
 * @param index directory entry the block belongs to, for FlipsoBlockProduct.
 *              Ignored for the other kinds, which occur once per card.
 * @return false when the block did not fit, or was empty, or repeats one
 *         already held. A capture that has dropped a block still decodes; it
 *         decodes to fewer products, exactly as a read that lost one would.
 */
bool flipso_capture_add(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    const uint8_t* data,
    size_t len);

/**
 * Decode a whole card from the captured blocks.
 *
 * The same sequence a live read performs: shell, directory, then each product
 * against its directory entry, then the log.
 *
 * @return false when there is no shell, or the shell does not parse.
 */
bool flipso_capture_decode(const FlipsoCapture* capture, ItsoCard* card);

/* ------------------------------------------------------------------ */
/* The saved file, a line at a time                                    */
/* ------------------------------------------------------------------ */

/**
 * Number of lines flipso_capture_line() will render, the header included.
 *
 * The file is laid out as the firmware's own key-value format - a Filetype and
 * Version line, then one key per block - so anything that reads a Flipper file
 * can read a saved card, and so a new block kind can be added without making
 * the files already on the card unreadable.
 */
size_t flipso_capture_lines(const FlipsoCapture* capture);

/**
 * Render line @p index, without its newline.
 * @param out at least FLIPSO_CAPTURE_LINE_MAX bytes; see the note there.
 * @return false for an index past the end, or a buffer too small to hold it.
 */
bool flipso_capture_line(const FlipsoCapture* capture, size_t index, char* out, size_t out_len);

/**
 * Feed one line of a saved file back in.
 *
 * Lines may arrive in any order and anything unrecognised is skipped, so a file
 * written by a later build loses the blocks this one does not know about rather
 * than failing to load.
 *
 * @return false only for a line that makes the file unusable: a Filetype or a
 *         Version that is not ours. The caller should stop and report that.
 */
bool flipso_capture_parse_line(FlipsoCapture* capture, const char* line);

#ifdef __cplusplus
}
#endif
