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

/**
 * A shell, a directory, a cyclic log and its history, the chip's description,
 * one group per directory entry, one value history per entry, and the same pair
 * again for each product the card has dropped since a file was written.
 */
#define FLIPSO_CAPTURE_MAX_BLOCKS \
    (ITSO_MAX_PRODUCTS * 2 + ITSO_MAX_HISTORIC_PRODUCTS * 2 + 5)

/**
 * Block index the products that have left the card are keyed from.
 *
 * They cannot be keyed by directory entry the way a live product is: the entry
 * they sat in is the one thing about them that is no longer theirs, and a
 * ticket that expired and was replaced shares its number with the product that
 * took the slot. So each gets a slot of its own, well clear of any entry
 * number a shell can carry, and says inside the block which entry it held.
 */
#define FLIPSO_CAPTURE_HISTORY_BASE 100
_Static_assert(
    FLIPSO_CAPTURE_HISTORY_BASE > ITSO_MAX_PRODUCTS,
    "history slots must not collide with directory entry numbers");

/**
 * Header on a FlipsoBlockProductHistory block, before the IPE group itself:
 *
 *   0..3  Unix time of the last read that found the product on the card
 *   4     the 1-based directory entry E(i) it occupied then
 *   5..9  that IPE Directory Entry, the five bytes the directory held
 *
 * The rest of the file is card bytes and nothing else, and this block is nearly
 * that: the entry is exactly what the Directory Data Group said. What it has to
 * add is the date, because a product the card has forgotten has no other way of
 * saying when it was last true - and the entry number, because the directory
 * that numbered it is not the one in the file any more.
 */
#define FLIPSO_PRODUCT_HISTORY_HEADER (4 + 1 + ITSO_DIR_ENTRY_LEN)

/**
 * Tap records kept from earlier reads of the same card.
 *
 * A DESFire log holds four, so four live records plus these fill ITSO_MAX_TAPS
 * exactly: there is no point keeping records in the file that the decoder would
 * not have room to show.
 */
#define FLIPSO_CAPTURE_MAX_LOG_HISTORY 8

/** Value records kept from earlier reads, per product, for the same reason. */
#define FLIPSO_CAPTURE_MAX_VALUE_HISTORY ITSO_MAX_VALUE_RECORDS

/**
 * Ceiling on the bytes one capture holds.
 *
 * No single block can be longer than the longest sector chain the decoder will
 * assemble, and there are at most FLIPSO_CAPTURE_MAX_BLOCKS of them. The arena
 * grows towards this rather than starting at it: a CMD7 card fills about a
 * tenth of it, and the Flipper has 190 KB of heap for everything.
 */
#define FLIPSO_CAPTURE_MAX_BYTES (FLIPSO_CAPTURE_MAX_BLOCKS * ITSO_MAX_GROUP_LEN)

/** Longest key the file uses, terminator included: "Product history 103". */
#define FLIPSO_CAPTURE_KEY_MAX 24

/**
 * Longest line flipso_capture_line() writes, terminator included.
 *
 * A key, then three characters per byte. Too large for the app's 4 KB stack:
 * the caller allocates this buffer on the heap.
 */
#define FLIPSO_CAPTURE_LINE_MAX (FLIPSO_CAPTURE_KEY_MAX + ITSO_MAX_GROUP_LEN * 3)

/** Which data group a block of captured bytes came from. */
typedef enum {
    FlipsoBlockShell, /**< ITSO Shell Environment Data Group. */
    FlipsoBlockDirectory, /**< Directory Data Group. */
    FlipsoBlockProduct, /**< One product's IPE and Value Record groups, chained. */
    FlipsoBlockLog, /**< The cyclic log of Transient Ticket Records. */

    /* The blocks that are not what the card said this time. They hold raw
     * bytes exactly as some earlier read of the same card found them, which is
     * what keeps a saved card a record of bytes rather than of decisions: they
     * go through the same decoder, and the build that is running decides what
     * they mean. */
    FlipsoBlockLogHistory, /**< Tap records earlier reads saw, newest first. */
    FlipsoBlockValueHistory, /**< Value records earlier reads saw, per entry. */
    /* A whole product an earlier read saw and the card no longer lists, as its
     * IPE group behind the header described above. Keyed by history slot, and
     * its own value history is keyed by the same slot. */
    FlipsoBlockProductHistory,

    /* What a DESFire said about itself: GetVersion's reply and then
     * GetFreeMemory's, as flipso_media_parse_chip() reads them. Not part of the
     * ITSO shell, but it is still what the card said, and without it a saved
     * card could not say what chip it is. */
    FlipsoBlockChip,
} FlipsoBlockKind;

/**
 * What changed between the card in the reader and the record already saved.
 *
 * Counted in records rather than worked out from the decode, because the merge
 * is the only point at which both sides are in hand: afterwards the two are one
 * history and nothing distinguishes what came from where.
 */
typedef struct {
    uint8_t new_taps; /**< Journeys the card has made since that record. */
    uint8_t kept_taps; /**< Older journeys carried forward out of the file. */
    uint8_t new_values; /**< Transactions on products since that record. */
    uint8_t kept_values; /**< Older transactions carried forward. */
    uint8_t kept_products; /**< Products carried forward that the card has dropped. */
} FlipsoCaptureDiff;

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
 * @param index directory entry the block belongs to, for FlipsoBlockProduct
 *              and FlipsoBlockValueHistory, or the history slot, for
 *              FlipsoBlockProductHistory. Ignored for the kinds that occur
 *              once per card.
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
 * The card number the captured shell carries, e.g. "633597019604241569".
 *
 * This is how a capture is matched against the cards already saved: the number
 * is the card's identity, and everything else about it - the products, the
 * balance, the log - is what changes between one read and the next.
 *
 * @param out at least ITSO_ISRN_DIGITS + 1 bytes.
 * @return false when there is no shell, or it does not parse.
 */
bool flipso_capture_card_number(const FlipsoCapture* capture, char* out);

/**
 * Decode a whole card from the captured blocks.
 *
 * The same sequence a live read performs: shell, directory, then each product
 * against its directory entry, then the log.
 *
 * @return false when there is no shell, or the shell does not parse.
 */
bool flipso_capture_decode(const FlipsoCapture* capture, ItsoCard* card);

/**
 * The raw IPE and Value Record groups of the product in directory entry
 * @p dir_index, as read - for decoding the parts of a product too large to keep
 * decoded in every ItsoProduct, such as a capping extension. NULL if none.
 */
const uint8_t*
    flipso_capture_product_group(const FlipsoCapture* capture, uint8_t dir_index, size_t* len);

/** The chip's own description, as the card gave it, or NULL if none was kept. */
const uint8_t* flipso_capture_chip(const FlipsoCapture* capture, size_t* len);

/**
 * Fold the history @p previous holds into @p capture, so that saving over it
 * keeps what it knew.
 *
 * A card keeps a rolling window: four slots of journey log, two value records
 * per product. Reading a card again and writing the file fresh would throw away
 * everything that has since rolled off, so instead the records the previous
 * file holds and this read does not are kept alongside it - which makes a saved
 * card a longer history of the card than the card itself has room to be.
 *
 * Records are matched byte for byte. A record is written once and never
 * altered, so a record still on the card is the same bytes in both, and the
 * ones that are not in @p capture are exactly the ones that have rolled off.
 *
 * Value records are only carried forward for a directory entry whose entry
 * bytes are unchanged - same owner, type, subtype and expiry. A product that
 * has been removed and replaced leaves its slot to something else, and its
 * transactions would otherwise be shown as that new product's own.
 *
 * That product itself is kept, though, rather than dropped with its slot: the
 * whole group goes into a history slot of its own, so the card still shows the
 * season ticket that ran out last month - marked as one it no longer carries.
 *
 * Merging is idempotent: history a previous merge added to @p capture is
 * replaced rather than added to.
 *
 * @param[out] diff what was found, for telling the user. May be NULL.
 */
void flipso_capture_merge_history(
    FlipsoCapture* capture,
    const FlipsoCapture* previous,
    FlipsoCaptureDiff* diff);

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
