/**
 * @file itso_i.h
 * @brief Decoder internals shared between the ITSO parsing translation units.
 */
#pragma once

#include "itso.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ITSO_IPE_BLOCK_LEN   4 /* BL for every IPE type we decode. */
#define ITSO_INSTANCE_ID_LEN 8
#define ITSO_SEAL_LEN        8

/* The Space Saving IPE dataset as TS 1000-5 table 48 defines it: 16 static
 * bytes, then 8 rewritable and 4 one-time-programmable dynamic ones. A CMD4
 * scatters them across its pages (see itso_cmd4.c); every layout of TYP 27, 28
 * and 29 fills all 28. */
#define ITSO_SPACE_SAVING_STATIC_LEN 16
#define ITSO_SPACE_SAVING_DYN_LEN    8
#define ITSO_SPACE_SAVING_OTP_LEN    4
#define ITSO_SPACE_SAVING_LEN \
    (ITSO_SPACE_SAVING_STATIC_LEN + ITSO_SPACE_SAVING_DYN_LEN + ITSO_SPACE_SAVING_OTP_LEN)

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

/**
 * The halves of itso_parse_reservation(), apart so that each can be tested
 * against buffers cut short on its own.
 *
 * @param data a TYP 24 IPE Data Group from its header; @p len bytes of it.
 */
bool itso_parse_reservation_dataset(const uint8_t* data, size_t len, ItsoReservation* out);

/**
 * @param vgx a VGXRef 3 Value Group Extension from its header; @p len bytes of it.
 * @param count NumberOfReservations: the legs it holds.
 * @return false when it is not a VGXRef 3 extension, or its fixed part did not read.
 */
bool itso_parse_reservation_vgx(
    const uint8_t* vgx,
    size_t len,
    uint8_t count,
    ItsoReservation* out);

/** Decode a VALC nibble and a raw amount into an ItsoMoney. */
void itso_decode_money(int32_t raw, uint8_t valc, ItsoMoney* out);

/**
 * CRC_B over @p len bytes, as ITSO TS 1000-2 Annex A defines it.
 *
 * Every CRC in the specification is of this variety; the shell's SECRC is the
 * only one Flipso has any use for.
 */
uint16_t itso_crc_b(const uint8_t* data, size_t len);

/** Read a signed 16-bit big-endian value: a VALS, which only a balance is. */
int32_t itso_int16(const uint8_t* data);

/**
 * Read an unsigned 16-bit big-endian value: a VALI, which every limit, deposit
 * and price is (TS 1000-1 table 5).
 */
int32_t itso_uint16(const uint8_t* data);

/** Copy a fixed-length ASCII name field, trimming trailing spaces. */
size_t itso_copy_name(const uint8_t* src, size_t n, char* dst, size_t dst_len);

/** itso_copy_name(), less leading spaces too. */
void itso_copy_trimmed(const uint8_t* src, size_t n, char* dst, size_t dst_len);

/**
 * Make room for @p count products in all, keeping the ones already there.
 * The slots beyond them are zeroed. Capped at ITSO_MAX_CARD_PRODUCTS.
 *
 * @return false when the room could not be had; the card is unchanged then.
 */
bool itso_card_reserve(ItsoCard* card, uint8_t count);

/** The next product slot, zeroed and counted, or NULL when there is no room. */
ItsoProduct* itso_card_next_product(ItsoCard* card);

/** True for a Compact ITSO Shell (TS 1000-2 table 4); see itso_shell.c. */
bool itso_shell_is_compact(const uint8_t* data, size_t len);

/** Decode one IPE Directory Entry (TS 1000-2 clause 6.1) into @p product. */
void itso_parse_dir_entry(ItsoProduct* product, const uint8_t* entry, uint8_t index);

/** The IPE InstanceID that follows a dataset of @p dataset_len bytes (TS 1000-2 table 11). */
void itso_parse_instance_id(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    size_t dataset_len);

/**
 * Decode a Space Saving IPE (TYP 27, 28 or 29) into @p product and
 * @c card->space.
 *
 * @param dataset ITSO_SPACE_SAVING_LEN bytes, reassembled into table 48's order.
 */
void itso_parse_space_saving(ItsoCard* card, ItsoProduct* product, const uint8_t* dataset);

#ifdef __cplusplus
}
#endif
