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

/** Decode a VALC nibble and a raw amount into an ItsoMoney. */
void itso_decode_money(int32_t raw, uint8_t valc, ItsoMoney* out);

#ifdef __cplusplus
}
#endif
