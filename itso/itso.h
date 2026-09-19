/**
 * @file itso.h
 * @brief Data model and decoder for the ITSO Shell held on a UK transport smartcard.
 *
 * Structures and field offsets follow ITSO TS 1000 (version 2.1.5, 2025-03):
 *   Part 1  - data types (DATE, DTS, VALC/VALS) and location definitions
 *   Part 2  - Shell Environment, Directory, IPE, Value Record and Log Directory Entry
 *   Part 5  - per-IPE-type datasets and the Transient Ticket Record
 *   Part 10 - the customer media definitions (CMD2, CMD7, CMD12)
 *
 * Everything here is pure computation over byte buffers: no NFC, no GUI. That keeps
 * the decoder testable off-device and keeps the transport layer free to stream
 * sectors in whatever order is cheapest.
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
#define ITSO_MAX_PRODUCTS  16
#define ITSO_MAX_TAPS      6
#define ITSO_NAME_LEN      40
#define ITSO_LOC_LEN       28
/* Twelve-character AtcoCode plus terminator, the longest code a location can
 * carry (TS 1000-1 table 40). A NaptanCode needs nine of these bytes. */
#define ITSO_LOC_CODE_LEN  13
#define ITSO_ISRN_DIGITS   18

/* Largest IPE + Value Record group we assemble from chained sectors. Permitted
 * DESFire geometries allow sectors of up to 240 bytes, and a purse needs its IPE
 * sector plus the value record sector that follows it. */
#define ITSO_MAX_GROUP_LEN 512

/* IPE types we decode beyond the directory entry (ITSO TS 1000-5 clause 2). */
typedef enum {
    ItsoTypStoredTravelRights = 2,  /**< Pay as you go purse. */
    ItsoTypLoyalty1 = 3,
    ItsoTypChargeToAccount1 = 4,
    ItsoTypChargeToAccount2 = 5,
    ItsoTypEntitlement = 14,
    ItsoTypId = 16,                 /**< ITSO ID: holder name and entitlement. */
    ItsoTypLoyalty2 = 17,
    ItsoTypPeriodTicket = 22,       /**< Pre-defined area-based ticket. */
    ItsoTypJourneyTicket = 23,      /**< Pre-defined specific journey ticket. */
    ItsoTypReservationTicket = 24,
    ItsoTypVoucher = 25,
    ItsoTypTolling = 26,
    ItsoTypPeriodCompact = 27,
    ItsoTypCarnet = 28,
    ItsoTypMultiUse = 29,
} ItsoTyp;

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
    ItsoCountRides, /**< TYP 23/24/25/26: rides, journeys or tickets left. */
    ItsoCountPasses, /**< TYP 22: unactivated passes left. */
    ItsoCountTransactions, /**< TYP 5: charge transactions used this period. */
    ItsoCountPoints, /**< TYP 3: loyalty points held. */
} ItsoCountKind;

/**
 * Which national register @c ItsoLocation::code is a key into.
 *
 * Several LocDefTypes carry the same kind of code - 206, 212 and 216 all end up
 * holding a NaptanCode - so what a caller needs in order to look one up is the
 * kind of code, not the type of location that happened to carry it.
 */
typedef enum {
    ItsoLocCodeNone, /**< Nothing to look up; the rendered text is all there is. */
    ItsoLocCodeNlc, /**< Four-character rail National Location Code. */
    ItsoLocCodeNaptan, /**< NaptanCode bus stop, as the digits the card stores. */
    ItsoLocCodeAtco, /**< AtcoCode bus stop, up to twelve ASCII characters. */
} ItsoLocCodeKind;

/**
 * A decoded location.
 *
 * @c text is a self-contained rendering that is always safe to display. @c code
 * carries the bare location code for the types where something outside the
 * decoder can do better - a rail NLC can be turned into a station name, and a
 * bus stop code into a stop name, given lookup tables the decoder itself has no
 * business owning.
 */
typedef struct {
    bool valid;
    uint8_t def_type; /**< LocDefType, ITSO TS 1000-1 table 6. */
    uint8_t code_kind; /**< ItsoLocCodeKind; meaningless while @c code is empty. */
    char text[ITSO_LOC_LEN];
    char code[ITSO_LOC_CODE_LEN]; /**< Bare code, empty when not resolvable. */
} ItsoLocation;

/**
 * The register @c code belongs to, or ItsoLocCodeNone when there is no code.
 *
 * A malformed field leaves @c code empty while @c text still describes what the
 * card said, so the emptiness test is what distinguishes "not resolvable" from
 * "resolvable and of kind zero".
 */
static inline ItsoLocCodeKind itso_location_code_kind(const ItsoLocation* location) {
    if(location->code[0] == '\0') return ItsoLocCodeNone;
    return (ItsoLocCodeKind)location->code_kind;
}

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

/** One entry of the Directory Data Group, plus whatever its IPE dataset yielded. */
typedef struct {
    bool present;
    uint8_t dir_index; /**< 1-based position E(i) in the directory. */
    uint16_t oid; /**< Operator that owns the product, after any EF extension. */
    bool oid_extended; /**< EF was set: the operator is in the extended IPE-owner range. */
    uint8_t typ;
    uint8_t ptyp;
    bool value_group; /**< VGP: a Value Record Data Group follows the IPE. */
    bool foreign_iin; /**< IINL: operator belongs to a different network. */
    uint16_t expiry; /**< Raw DATE; 0 means "no expiry" (decodes to 2041-11-10). */
    ItsoProductStatus status;

    bool body_parsed; /**< The IPE dataset itself was read and understood. */
    uint8_t format_rev;
    uint8_t bitmap; /**< IPEBitMap: which optional elements the dataset carries. */

    /* Optional IIN, appended after the dataset padding when IPEBitMap bit 0 is
     * set. Every IPE type defines that bit the same way (TS 1000-5 tables 3, 8,
     * 11, 16, 21, 23, 26). */
    bool has_iin;
    uint32_t iin;

    /* Elements common to the front of most IPE datasets (TS 1000-5): a remove
     * date and the operator that sold the product, which is not always the
     * operator that owns it. */
    bool has_remove_date;
    uint8_t remove_date; /**< RDATE: days past expiry before the IPE may be removed. */
    bool has_retailer;
    uint16_t retailer; /**< ProductRetailer OID. */

    /* IPE InstanceID (TS 1000-2 table 11), which follows the dataset. Together
     * with the owner and type this is the only unique identity a product has:
     * there is no product serial number anywhere else in the shell. */
    bool instance_valid;
    uint8_t key_id; /**< KID: seal key version. */
    uint8_t iteration; /**< INP#: bumped to re-validate a hotlisted IPE. */
    uint32_t isam_id; /**< ISAM that created the product. */
    uint32_t isam_seq; /**< ISAMS#, 24 bits. */

    /* Purse and credit limits held in the IPE dataset rather than the value
     * record: TYP 2 (table 2), TYP 4 (table 10) and TYP 5 (table 15). */
    bool has_limits;
    ItsoMoney max_value; /**< MaxValue2/4/5: ceiling on balance or spend. */
    ItsoMoney max_negative; /**< TYP 2 MaximumNegativeAmount: permitted overdraft. */
    bool has_top_up;
    ItsoMoney top_up_threshold; /**< Auto-top-up trigger level. */
    ItsoMoney top_up_amount;
    bool has_deposit;
    ItsoMoney deposit;
    uint8_t deposit_mop; /**< EN1545 PaymentMeansCode the deposit was paid by. */

    /* TYP 5 charge period (table 15). */
    bool has_charge_period;
    uint8_t weeks_per_period;
    uint8_t max_transactions;

    /* Validity window carried inside the dataset, where the type has one that is
     * distinct from the directory expiry: TYP 4/5 EndDate. */
    bool has_end_date;
    uint16_t end_date;

    /* Value Record Data Group (TS 1000-2 clause 7), present when VGP is set.
     * Every value record shares a common header (table 15); the five bytes after
     * it mean whatever the IPE type says they mean, which is what the
     * type-specific fields below are for. */
    bool value_parsed; /**< A live value record was found and decoded. */
    uint32_t value_dts; /**< Raw DTS of the value record we picked. */
    uint8_t value_txn; /**< EventTypeCode: what the last transaction was. */
    uint16_t value_ts; /**< TS#: how many times the record has been written. */
    uint32_t value_isam; /**< ISAMIDModifier: the POST that wrote the record. */
    uint8_t value_action_seq; /**< ActionSequenceNumber, for action lists. */

    /* Stored value (TYP 2 Value, TYP 4 CumulativeAmount). The two are the same
     * field in the same place and differ only in sign of meaning: a purse counts
     * down, a charge-to-account accumulates. */
    ItsoMoney balance;
    bool balance_is_spend; /**< TYP 4: the amount is spend to date, not credit. */

    /* A counter, where the type keeps one. */
    ItsoCountKind count_kind;
    uint32_t count;

    /* Multi-leg journey in progress (TYP 2 and TYP 4 value records). */
    bool has_journey;
    uint8_t journey_legs; /**< CountJourneyLegs. */
    ItsoMoney cumulative_fare;

    /* Ticket state flags, gathered from the various TYPnValueFlags. */
    bool ticket_used; /**< TYP23ValueFlags UsedChecked: the ticket has been used. */
    bool auto_renew;
    bool auto_top_up;
    bool priority_override; /**< This IPE is to be spent before any other. */
    bool has_transfers;
    uint8_t transfers; /**< CountTransfers on the current multi-leg journey. */

    /* Expiry dates that live in the value record rather than the directory. */
    bool has_stored_expiry;
    uint16_t stored_expiry; /**< ExpiryDateSP/SRJ: expiry of the unactivated stock. */
    bool has_current_expiry;
    uint16_t current_expiry; /**< TYP 22 ExpiryDateCurrent: the pass in use. */
    bool has_last_reset;
    uint16_t last_reset; /**< TYP 5 LastResetDate. */

    /* Identity and entitlement (TYP 14 / TYP 16, tables 20 and 22). */
    bool has_name;
    char name[ITSO_NAME_LEN];
    bool has_dob;
    uint16_t dob_year; /**< DateOfBirth, stored as Datef BCD rather than a DATE. */
    uint8_t dob_month;
    uint8_t dob_day;
    bool has_id_flags;
    uint8_t id_flags; /**< IDFlags, TS 1000-5 table 24. */
    bool has_passback;
    uint8_t passback; /**< PassbackTime in minutes; 0 means the POST decides. */
    bool has_entitlement;
    uint8_t entitlement_code; /**< EN1545 EntitlementTypeCode. */
    uint8_t concession_class; /**< EN1545 ProfileCodeIOP. */
    bool has_start;
    uint16_t start; /**< Entitlement or validity start, raw DATE. */
    bool has_sub_expiry;
    uint16_t sub_expiry; /**< Entitlement expiry, distinct from IPE expiry. */

    ItsoLocation from;
    ItsoLocation to;
} ItsoProduct;

/** One Transient Ticket Record from the cyclic log: a single tap. */
typedef struct {
    bool present;
    uint8_t format_rev;
    uint8_t transaction_type; /**< EN1545 EventTypeCode. */
    uint32_t dts; /**< Raw DTS of the tap. */

    /* AMT group (TS 1000-5 table 59). */
    ItsoMoney amount;
    bool has_mop;
    uint8_t mop; /**< EN1545 PaymentMeansCode: how the fare was paid. */
    bool no_fare_charged; /**< Operator let the holder travel without taking the fare. */
    bool has_vat;
    uint16_t vat; /**< VATSalesTax in 0.01% steps. */

    bool has_ipe_pointer;
    uint8_t ipe_pointer; /**< Directory entry of the product used. */
    ItsoLocation origin;
    ItsoLocation destination;
    ItsoLocation route; /**< RC group: the "via" point that determines the fare. */

    /* IIN group: the network the POST that wrote the record is registered with. */
    bool has_iin;
    uint32_t iin;

    /* CIPE group (format revisions 3 and 4): which products the POST considered
     * for this journey, and whether anything was flagged against the holder. */
    bool has_cipe;
    uint8_t cipe[4]; /**< Candidate directory entries, zero where unused. */
    bool invalid_travel; /**< CIPEFlags bit 0. */
    bool inspected; /**< CIPEFlags bit 1: checked by an inspector this journey. */

    /* ENTRY group (format revision 4): where and when this journey checked in,
     * copied from the tap-in record so a tap-out record is self-contained. */
    bool has_entry;
    uint32_t entry_dts;
    bool has_entry_oid;
    uint16_t entry_oid; /**< Operator whose gate the holder entered through. */

    bool latest; /**< Newest record, per the Log Directory Entry record offset. */
} ItsoTap;

/** Everything Flipso knows about one card. */
typedef struct {
    /* --- ITSO Shell Environment Data Group (TS 1000-2 clause 4) --- */
    bool shell_valid;
    char isrn[ITSO_ISRN_DIGITS + 1]; /**< 18-digit card number, IIN+OID+ISSN+check. */
    bool isrn_check_ok; /**< Luhn check digit verifies. */
    uint32_t iin; /**< Issuer Identification Number as a decimal value. */
    uint16_t oid; /**< Shell owner. */
    uint16_t expiry; /**< Raw DATE. */
    uint8_t format_rev; /**< ShellFormatRevision. */
    uint8_t fvc; /**< Format Version Code: 7 = DESFire CMD7, 12 = CMD12. */
    uint8_t ksc;
    uint8_t kvc;
    uint8_t sector_size; /**< B */
    uint8_t sector_count; /**< S */
    uint8_t dir_entries; /**< e# */
    uint8_t sct_len; /**< SCTL */
    bool mcrn_present;
    char mcrn[21];

    /* --- Directory Data Group (TS 1000-2 clause 5) --- */
    bool dir_valid;
    bool shell_blocked;
    uint8_t dir_sequence;

    /* --- Log Directory Entry (TS 1000-2 clause 8) --- */
    bool log_entry_valid;
    uint8_t log_dir_index; /**< Directory entry holding the log entry, 0 if none. */
    bool log_normal_mode; /**< LPF: normal mode references a transient ticket record. */
    uint8_t log_ptr; /**< Directory entry of the product used on the last tap. */
    uint8_t log_eei; /**< Entry/exit indicator: 0 = outside a closed system. */
    uint32_t log_dts;
    uint8_t log_record_offset; /**< RO: next record to be written. */
    uint8_t log_passback; /**< PTLBM, minutes. */

    ItsoProduct products[ITSO_MAX_PRODUCTS];
    uint8_t product_count;

    ItsoTap taps[ITSO_MAX_TAPS];
    uint8_t tap_count;
} ItsoCard;

/* ------------------------------------------------------------------ */
/* Primitive field access                                             */
/* ------------------------------------------------------------------ */

/** Read up to 32 bits, MSB first, from an arbitrary bit offset. */
uint32_t itso_bits(const uint8_t* data, uint32_t bit_offset, uint8_t bit_len);

/** Read @p digits BCD nibbles into @p out, which needs digits+1 bytes. */
void itso_bcd(const uint8_t* data, uint32_t bit_offset, uint8_t digits, char* out);

/** True if every byte in the range is zero. */
bool itso_is_blank(const uint8_t* data, size_t len);

/* ------------------------------------------------------------------ */
/* Date and time                                                      */
/* ------------------------------------------------------------------ */

/**
 * Convert a 14-bit EN1545 DateStamp to a Unix timestamp.
 * Days since 1997-01-01; a stored zero means the maximum date (2041-11-10).
 */
uint32_t itso_date_to_unix(uint16_t date);

/**
 * Convert a 24-bit DTS to a Unix timestamp.
 * DTS is a two's complement count of minutes from the epoch 2028-11-24 20:16.
 */
uint32_t itso_dts_to_unix(uint32_t dts);

/** True once the DATE has passed relative to @p now (a Unix timestamp). */
bool itso_date_expired(uint16_t date, uint32_t now);

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

void itso_card_reset(ItsoCard* card);

/** Parse the 24/32-byte ITSO Shell Environment Data Group. */
bool itso_parse_shell(ItsoCard* card, const uint8_t* data, size_t len);

/** True if @p data looks like an ITSO Shell Environment (IIN 6335 97). */
bool itso_looks_like_shell(const uint8_t* data, size_t len);

/** Parse the Directory Data Group: directory entries, log entry and blocked flag. */
bool itso_parse_directory(ItsoCard* card, const uint8_t* data, size_t len);

/**
 * Read one Sector Chain Table element.
 * @param sector 1-based sector number; SCT(i) describes logical sector i.
 */
uint8_t itso_sct_entry(const ItsoCard* card, const uint8_t* dir, size_t dir_len, uint8_t sector);

/** Number of bits per SCT element: the smallest psi with S <= 2^psi. */
uint8_t itso_sct_bits(uint8_t sector_count);

/**
 * Decode the IPE Data Group (and any Value Record Data Group) for one product.
 * @param group  bytes of every chained sector, concatenated in chain order.
 */
void itso_parse_ipe(ItsoProduct* product, const uint8_t* group, size_t len, uint8_t sector_size);

/** Decode the cyclic log into card->taps. */
void itso_parse_log(ItsoCard* card, const uint8_t* data, size_t len);

/* ------------------------------------------------------------------ */
/* Human-readable names for coded values                              */
/* ------------------------------------------------------------------ */

const char* itso_typ_name(uint8_t typ);
const char* itso_entitlement_name(uint8_t code);
const char* itso_profile_name(uint8_t code);
const char* itso_transaction_name(uint8_t code);
const char* itso_status_name(ItsoProductStatus status);

/** EN1545 PaymentMeansCode, e.g. "Cash" (TS 1000-5 annex A.12). */
const char* itso_payment_name(uint8_t code);

/** Label for a product counter, e.g. "Rides left". NULL for ItsoCountNone. */
const char* itso_count_name(ItsoCountKind kind);

/** Gender recorded in IDFlags, or NULL when it is not known or not specified. */
const char* itso_gender_name(uint8_t id_flags);

/** IDFlags bit 0: the card surface carries a photo of the holder. */
static inline bool itso_id_personalised(uint8_t id_flags) {
    return (id_flags & 0x01) != 0;
}

/** IDFlags bit 4: a companion travels at the holder's concessionary rate. */
static inline bool itso_id_companion(uint8_t id_flags) {
    return (id_flags & 0x10) != 0;
}

/** Render an amount, e.g. "GBP 12.34". Writes at most @p len bytes. */
void itso_format_money(const ItsoMoney* money, char* out, size_t len);

#ifdef __cplusplus
}
#endif
