/**
 * @file itso_types.h
 * @brief The limits, codes and value types every part of the decoder shares.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A shell allows at most 31 directory entries. The DESFire profiles all use 8,
 * but CMD2 cards are issued with 16, so 16 is the working maximum rather than a
 * generous one. The directory parser stops here, leaving an unusual geometry
 * short of a product or two rather than overflowing the array. */
#define ITSO_MAX_PRODUCTS          16
/* Products a saved card keeps after they have left the card, which a live read
 * never produces. A directory entry is freed when a ticket expires and is
 * removed, so a product the card no longer carries exists only in the file that
 * was written while it did - and four of them is as much of that as a screen
 * the user scrolls is worth. */
#define ITSO_MAX_HISTORIC_PRODUCTS 4
/* The most ItsoCard::products can hold: every entry the directory can carry,
 * plus the ones a saved file remembers from before. A cap, not a size - the
 * array holds only as many as the card has. */
#define ITSO_MAX_CARD_PRODUCTS     (ITSO_MAX_PRODUCTS + ITSO_MAX_HISTORIC_PRODUCTS)
/* Four 48-byte records fit the DESFire cyclic log, and a saved card adds the
 * records earlier reads of it saw: the log on the card is a rolling window, so
 * a journey history is only as long as something off the card remembers. Twelve
 * holds a full log plus the eight a saved card keeps. */
#define ITSO_MAX_TAPS              12
#define ITSO_NAME_LEN              40
#define ITSO_LOC_LEN               28
/* Twelve-character AtcoCode plus terminator, the longest code a location can
 * carry (TS 1000-1 table 40). A NaptanCode needs nine of these bytes. */
#define ITSO_LOC_CODE_LEN          13
#define ITSO_ISRN_DIGITS           18

/* BL for the ITSO Shell Environment Data Group. ShellFormatRevision 1 is the
 * only revision TS 1000-2 table 2 defines a block size for, and it is 4. */
#define ITSO_SHELL_BLOCK_LEN 4

/* Largest IPE + Value Record group we assemble from chained sectors. Permitted
 * DESFire geometries allow sectors of up to 240 bytes, and a purse needs its IPE
 * sector plus the value record sector that follows it. */
#define ITSO_MAX_GROUP_LEN 512

/**
 * Value records kept per product, newest first.
 *
 * A Value Record Data Group holds at most five (TS 1000-2 table 14), and the
 * cards to hand are issued with two. The rest of the room is for the records an
 * earlier read of the same card saw, which a saved card keeps: the store on the
 * card is a rolling window, so a history is only ever as long as something off
 * the card remembers. Eight is a compromise - the array is per product, so it
 * is paid for sixteen times over whether a product has a value group or not.
 */
#define ITSO_MAX_VALUE_RECORDS 8

/* One value record: a 10-byte common header (TS 1000-2 table 15) and a 5-byte
 * tail whose meaning TS 1000-5 defines per IPE type. */
#define ITSO_VALUE_RECORD_LEN 15

/* One IPE Directory Entry (TS 1000-2 clause 6.1). */
#define ITSO_DIR_ENTRY_LEN 5

/* One slot of the DESFire cyclic log, which holds fixed-length Transient Ticket
 * Records whatever the record inside it claims to be (TS 1000-10 clause 8.7.5). */
#define ITSO_TAP_RECORD_LEN 48

/* IPE types we decode beyond the directory entry (ITSO TS 1000-5 clause 2). */
typedef enum {
    ItsoTypStoredTravelRights = 2, /**< Pay as you go purse. */
    ItsoTypLoyalty1 = 3,
    ItsoTypChargeToAccount1 = 4,
    ItsoTypChargeToAccount2 = 5,
    ItsoTypEntitlement = 14,
    ItsoTypId = 16, /**< ITSO ID: holder name and entitlement. */
    ItsoTypLoyalty2 = 17,
    ItsoTypPeriodTicket = 22, /**< Pre-defined area-based ticket. */
    ItsoTypJourneyTicket = 23, /**< Pre-defined specific journey ticket. */
    ItsoTypReservationTicket = 24,
    ItsoTypVoucher = 25,
    ItsoTypTolling = 26,
    ItsoTypPeriodCompact = 27,
    ItsoTypCarnet = 28,
    ItsoTypMultiUse = 29,
} ItsoTyp;

/**
 * Why itso_parse_shell() refused a block of bytes.
 *
 * A failed shell read is the one error the user sees with no way to tell a
 * card Flipso does not understand from a card it simply did not read cleanly,
 * so the parser records which test rejected the bytes rather than only that
 * one did.
 */
typedef enum {
    ItsoShellRejectNone, /**< No shell has been offered to the parser yet. */
    ItsoShellRejectShort, /**< Fewer bytes than the header occupies. */
    ItsoShellRejectIin, /**< Bytes 2-4 are not ITSO's 63 35 97. */
    ItsoShellRejectCompact, /**< Bitmap bit 0 clear: no directory to walk. */
    ItsoShellRejectGeometry, /**< Sector or directory sizes out of range. */
    ItsoShellRejectNumber, /**< The card number holds a digit that is not 0-9. */
    ItsoShellAccepted,
} ItsoShellReject;

/** Lifecycle of a product, derived from the Sector Chain Table terminator. */
typedef enum {
    ItsoProductStatusUnknown,
    ItsoProductStatusUnused, /**< Chain terminator points at itself: never used. */
    ItsoProductStatusActive, /**< Terminator S-1: used at least once, not blocked. */
    ItsoProductStatusBlocked, /**< Terminator S-2. */
} ItsoProductStatus;

/**
 * What a product's value-record counter counts.
 *
 * Every IPE type that keeps a counter puts it in the same place - the first
 * byte of the type-specific tail - but they count different things, and a
 * screen that says "Rides left" against a loyalty balance is worse than one
 * that says nothing. TS 1000-5 tables 9, 17, 29, 33, 139, 38 and 42.
 */
typedef enum {
    ItsoCountNone,
    ItsoCountRides, /**< TYP 23/25/26: rides or tickets left. */
    ItsoCountPasses, /**< TYP 22: unactivated passes left. */
    ItsoCountTransactions, /**< TYP 5: charge transactions used this period. */
    ItsoCountPoints, /**< TYP 3: loyalty points held. */
    ItsoCountCoupons, /**< TYP 29 coupons: units of travel, several to a journey. */
    /** TYP 24 JourneysRemaining: a return sold as one ticket counts two (table
     *  139), so "rides" would undercount what the holder bought. */
    ItsoCountJourneys,
} ItsoCountKind;

/* DAYOFWEEK, TS 1000-5 annex A.6: Monday is the most significant bit and the
 * least significant is "special days", which schemes use for public holidays. */
#define ITSO_DOW_MONDAY   0x80u
#define ITSO_DOW_SATURDAY 0x04u
#define ITSO_DOW_SUNDAY   0x02u
#define ITSO_DOW_SPECIAL  0x01u
#define ITSO_DOW_WEEKDAYS 0xF8u
#define ITSO_DOW_ALL_DAYS 0xFEu

/**
 * A monetary amount plus the currency/scaling nibble that gives it meaning.
 *
 * Field order is deliberate: a product can carry half a dozen of these, so
 * putting the int32 first packs the struct into 8 bytes instead of 12.
 */
typedef struct {
    int32_t value; /**< Already multiplied by the VALC scaling factor. */
    uint8_t currency; /**< VALC currency bits: 0 local, 1 global, 2/3 tokens. */
    bool valid;
} ItsoMoney;

/* Everything Flipso knows about one card: see itso_card.h. Declared here so
 * that the parsers for each part of a card can take one without needing the
 * whole model. */
typedef struct ItsoCard ItsoCard;

#ifdef __cplusplus
}
#endif
