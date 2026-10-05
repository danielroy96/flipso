/**
 * @file flipso_names_file.h
 * @brief The layout of names.dat: what tools/names/build_names.c writes and
 *        flipso_names.c reads.
 *
 * The long name tables in itso/names/ would cost their size in heap if they
 * were in the .fap, which the Flipper loads whole into RAM. They ship as this
 * file instead, which the firmware unpacks to the SD card with the other assets.
 *
 * Little-endian throughout, and laid out so that a lookup is a handful of small
 * reads rather than a scan:
 *
 *   header     FLIPSO_NAMES_HEADER_LEN bytes
 *                0  magic, FLIPSO_NAMES_MAGIC
 *                4  u16 table count: FlipsoNamesTableCount, or the file is
 *                   not this build's
 *                6  u16 string count
 *                8  u32 offset of the string index
 *               12  u32 offset of the string bytes
 *   directory  FLIPSO_NAMES_TABLE_LEN bytes a table, in FlipsoNamesTable order
 *                0  u8  FlipsoNamesKind
 *                1  u8  0
 *                2  u16 entry count
 *                4  u32 offset of the entries
 *   entries    a byte table: 256 u16 string ids, one for each code, and
 *              FLIPSO_NAMES_NONE where the code has no name
 *              a keyed table: FLIPSO_NAMES_ENTRY_LEN bytes an entry, by key
 *                0  u32 key
 *                4  u16 string id
 *                6  u8  extra: the table's one flag, if it has one
 *                7  u8  0
 *   index      FLIPSO_NAMES_INDEX_LEN bytes a string
 *                0  u16 offset into the string bytes
 *                2  u8  length
 *                3  u8  0
 *   strings    every distinct name once, unterminated
 */
#pragma once

#define FLIPSO_NAMES_MAGIC      "FNM1"
#define FLIPSO_NAMES_HEADER_LEN 16
#define FLIPSO_NAMES_TABLE_LEN  8
#define FLIPSO_NAMES_ENTRY_LEN  8
#define FLIPSO_NAMES_INDEX_LEN  4
#define FLIPSO_NAMES_NONE       0xFFFF
/** The longest name the file may hold, so that one fits a lookup's buffer. */
#define FLIPSO_NAMES_MAX_LEN    47

typedef enum {
    FlipsoNamesKindByte,
    FlipsoNamesKindKeyed,
} FlipsoNamesKind;

/*
 * The tables keyed by one byte, each with the function in itso/names/ it is
 * built from: X(table, function, total). The file holds the function's answer
 * for every code from 0 to 255, so whatever the function does to its code -
 * masking it, defaulting it - the file has already done. @c total is true for a
 * function that answers every code, which the device keeps doing when it cannot
 * read the file; the builder fails if it is wrong.
 */
#define FLIPSO_NAMES_BYTE_TABLES(X)                        \
    X(FlipsoNamesEntitlement, itso_entitlement_name, true) \
    X(FlipsoNamesProfile, itso_profile_name, true)         \
    X(FlipsoNamesTransaction, itso_transaction_name, true) \
    X(FlipsoNamesPayment, itso_payment_name, true)         \
    X(FlipsoNamesSubway, itso_spt_subway_station, false)

typedef enum {
#define FLIPSO_NAMES_ID(table, function, total) table,
    FLIPSO_NAMES_BYTE_TABLES(FLIPSO_NAMES_ID)
#undef FLIPSO_NAMES_ID
    /** By itso_railcard_key(); extra is itso_railcard_name()'s card flag. */
    FlipsoNamesRailcard,
    /** By itso_seat_attribute_key(). */
    FlipsoNamesSeat,
    /** itso_operator_name(), by OID. */
    FlipsoNamesOperator,
    /** itso_operator_brand(), by OID. */
    FlipsoNamesBrand,
    FlipsoNamesTableCount,
} FlipsoNamesTable;
