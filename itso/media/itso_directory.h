/**
 * @file itso_directory.h
 * @brief The Directory Data Group and the sector chains it describes (TS 1000-2
 * clause 5).
 */
#pragma once

#include "../itso_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Parse the Directory Data Group: directory entries, log entry and blocked flag. */
bool itso_parse_directory(ItsoCard* card, const uint8_t* data, size_t len);

/**
 * Read one Sector Chain Table element.
 * @param sector 1-based sector number; SCT(i) describes logical sector i.
 */
uint8_t itso_sct_entry(const ItsoCard* card, const uint8_t* dir, size_t dir_len, uint8_t sector);

/** Number of bits per SCT element: the smallest psi with S <= 2^psi. */
uint8_t itso_sct_bits(uint8_t sector_count);

/* Sectors one data group may chain across. A cap rather than a spec limit, so
 * that a corrupt chain cannot spin or overrun the group buffer. */
#define ITSO_MAX_CHAIN_HOPS 6

/**
 * Read one logical sector for itso_read_chain().
 *
 * @return bytes written to @p out, or 0 when the sector could not be read - a
 *         sector the card does not have, or a card that stopped answering.
 */
typedef size_t (*ItsoSectorRead)(void* context, uint8_t sector, uint8_t* out, size_t capacity);

/**
 * Follow one data group's sector chain from @p start, concatenating the sectors
 * it occupies into @p out (TS 1000-2 clause 5.1.5).
 *
 * The chain ends at a terminator - the sector itself (unused), S-2 (blocked) or
 * S-1 (in use) - at a free entry, at a sector that will not read, or when the
 * next sector would not fit in @p capacity. Every transport walks a chain this
 * way; only how a sector is fetched differs, which is what @p read is for.
 *
 * @return bytes gathered, 0 when the first sector would not read.
 */
size_t itso_read_chain(
    const ItsoCard* card,
    const uint8_t* dir,
    size_t dir_len,
    uint8_t start,
    ItsoSectorRead read,
    void* context,
    uint8_t* out,
    size_t capacity);

/**
 * Gather a software anti-tear card's cyclic log, one Transient Ticket Record
 * per logical sector.
 *
 * On media that give each record a sector of its own (TS 1000-2 clause 2.4.8) -
 * CMD2 and CMD9/CMD10 - record T0 sits at the start of the sector the Log
 * Directory Entry's number names, and SCT of that sector names the sector
 * holding T1, whose own SCT is 0 (clause 5.1.5.5). Each sector is B bytes of
 * which a record uses 48, so the chain cannot simply be concatenated: this
 * keeps the first ITSO_TAP_RECORD_LEN bytes of each, so record n sits at n × 48
 * and Record Offset indexes it as it does a DESFire log.
 *
 * @param read fetches one whole sector; @p out must have room for a whole
 *             sector past the records already gathered.
 * @return bytes gathered, a multiple of ITSO_TAP_RECORD_LEN.
 */
size_t itso_read_log_sectors(
    const ItsoCard* card,
    const uint8_t* dir,
    size_t dir_len,
    ItsoSectorRead read,
    void* context,
    uint8_t* out,
    size_t capacity);

/**
 * The 1-based @p index'th IPE Directory Entry within a Directory Data Group.
 *
 * @return ITSO_DIR_ENTRY_LEN bytes, or NULL when the group is too short. The
 *         entry is returned whether or not it holds a product: an all-zero
 *         entry is an unused one.
 */
const uint8_t* itso_dir_entry(const uint8_t* dir, size_t len, uint8_t index);

#ifdef __cplusplus
}
#endif
