/**
 * @file itso_i.h
 * @brief Decoder internals shared between the ITSO parsing translation units.
 */
#pragma once

#include "itso.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Structural variants of the ITSO location record (TS 1000-1 clause 4.2.4.2). */
typedef enum {
    ItsoLocStructLoc1, /**< Tag, length, data. Used inside IPE datasets. */
    ItsoLocStructLoc2, /**< Tag plus a fixed 7-byte body. Used in the cyclic log. */
} ItsoLocStruct;

/**
 * Decode one location record into readable text.
 *
 * @param      data    start of the record (the LocDefType byte).
 * @param      avail   bytes remaining in the containing buffer.
 * @param      variant LOC1 or LOC2.
 * @param[out] out     filled in on success; zeroed otherwise.
 * @return             bytes consumed, or 0 if the record does not fit.
 */
size_t itso_parse_location(
    const uint8_t* data,
    size_t avail,
    ItsoLocStruct variant,
    ItsoLocation* out);

/**
 * Decode the locations of a fixed-length LOC3 or LOC4 structure (TS 1000-1
 * clauses 4.2.4.2.3 and 4.2.4.2.4): an origin, a destination and, in a LOC4, a
 * via, each a LOCE of up to four bytes padded to four. The half-byte type in
 * front of them is the caller's to read, as it seldom falls on a byte.
 *
 * A slot of zeros is left invalid: it is the padding the structure requires
 * where a place is not recorded.
 *
 * @param def_type LocDefType, 200 to 215.
 * @param data     the first slot; @p slots × 4 bytes are read.
 * @param slots    2 for a LOC3, 3 for a LOC4.
 * @param[out] out @p slots locations.
 */
void itso_parse_loc_fixed(uint8_t def_type, const uint8_t* data, uint8_t slots, ItsoLocation* out);

/** Decode a VALC nibble and a raw amount into an ItsoMoney. */
void itso_decode_money(int32_t raw, uint8_t valc, ItsoMoney* out);

/**
 * CRC_B over @p len bytes, as ITSO TS 1000-2 Annex A defines it.
 *
 * Every CRC in the specification is of this variety; the shell's SECRC is the
 * only one Flipso has any use for.
 */
uint16_t itso_crc_b(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
