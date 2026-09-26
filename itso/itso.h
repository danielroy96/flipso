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
/* Products a saved card keeps after they have left the card, which a live read
 * never produces. A directory entry is freed when a ticket expires and is
 * removed, so a product the card no longer carries exists only in the file that
 * was written while it did - and four of them is as much of that as a screen
 * the user scrolls is worth. */
#define ITSO_MAX_HISTORIC_PRODUCTS 4
/* What ItsoCard::products holds: every entry the directory can carry, plus the
 * ones a saved file remembers from before. */
#define ITSO_MAX_CARD_PRODUCTS (ITSO_MAX_PRODUCTS + ITSO_MAX_HISTORIC_PRODUCTS)
/* Four 48-byte records fit the DESFire cyclic log, and a saved card adds the
 * records earlier reads of it saw: the log on the card is a rolling window, so
 * a journey history is only as long as something off the card remembers. Twelve
 * holds a full log plus the eight a saved card keeps. */
#define ITSO_MAX_TAPS      12
#define ITSO_NAME_LEN      40
#define ITSO_LOC_LEN       28
/* Twelve-character AtcoCode plus terminator, the longest code a location can
 * carry (TS 1000-1 table 40). A NaptanCode needs nine of these bytes. */
#define ITSO_LOC_CODE_LEN  13
#define ITSO_ISRN_DIGITS   18

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
    ItsoCountRides, /**< TYP 23/24/25/26: rides, journeys or tickets left. */
    ItsoCountPasses, /**< TYP 22: unactivated passes left. */
    ItsoCountTransactions, /**< TYP 5: charge transactions used this period. */
    ItsoCountPoints, /**< TYP 3: loyalty points held. */
} ItsoCountKind;

/** TYP23Mode: how a journey ticket's rides are counted (TS 1000-5 table 35a). */
typedef enum {
    ItsoJourneyModeStoredRides = 0, /**< Each ride uses one. */
    ItsoJourneyModeStoredJourneys = 1, /**< Each journey uses one, legs within limits. */
    ItsoJourneyModeSimple = 2, /**< An ordinary ticket, the default. */
} ItsoJourneyMode;

/** PassDurationCode: the unit PassDuration counts in (TS 1000-5 table 3.30a). */
typedef enum {
    ItsoDurationDays = 0, /**< The only unit revisions 1 and 2 have. */
    ItsoDurationMonths = 1,
    ItsoDurationQuarters = 2,
    ItsoDurationYears = 3,
} ItsoDurationUnit;

/* TYP22Flags, numbered from the least significant bit (TS 1000-5 table 30). The
 * AM/PM pairs are a second day-of-week filter on top of ValidOnDayCode: rule 7
 * of clause 2.9.1.4 makes a ticket valid only when both allow today. */
#define ITSO_T22_TRANSFERABLE    (1u << 0)
#define ITSO_T22_OFF_PEAK_ONLY   (1u << 8)
#define ITSO_T22_WEEKDAY_AM      (1u << 9)
#define ITSO_T22_WEEKDAY_PM      (1u << 10)
#define ITSO_T22_SATURDAY_AM     (1u << 11)
#define ITSO_T22_SATURDAY_PM     (1u << 12)
#define ITSO_T22_SUNDAY_AM       (1u << 13)
#define ITSO_T22_SUNDAY_PM       (1u << 14)
#define ITSO_T22_PUBLIC_HOLIDAY  (1u << 15)
#define ITSO_T22_DAY_MASK        0xFE00u

/* DAYOFWEEK, TS 1000-5 annex A.6: Monday is the most significant bit and the
 * least significant is "special days", which schemes use for public holidays. */
#define ITSO_DOW_MONDAY   0x80u
#define ITSO_DOW_SATURDAY 0x04u
#define ITSO_DOW_SUNDAY   0x02u
#define ITSO_DOW_SPECIAL  0x01u
#define ITSO_DOW_WEEKDAYS 0xF8u
#define ITSO_DOW_ALL_DAYS 0xFEu


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

/**
 * The terms a TYP 22 period ticket or TYP 23 journey ticket is sold on (TS
 * 1000-5 tables 27, 27a, 3.27, 31, 31a and 31b).
 *
 * Kept apart from the rest of ItsoProduct because only the two ticket types
 * fill it. They share most of these elements, at offsets that differ between
 * the types and again between each type's three revisions.
 */
typedef struct {
    ItsoMoney amount_paid; /**< AmountPaid; not valid when the card records none. */
    ItsoMoney ride_value; /**< TYP 23 ValueOfRideJourney: nominal value of one ride. */
    uint32_t photocard; /**< TYP 23 PhotocardNumber; 0 when not recorded. */
    uint32_t valid_from_dts; /**< Revisions 1 and 2: ValidityStartDTS, 0 if unset. */
    uint16_t flags; /**< TYP22Flags, ITSO_T22_*. */
    uint16_t issue_date; /**< IssueDate DATE; 0 when not recorded. */
    uint16_t expiry_time; /**< ExpiryTime, minutes; 1440 and over is the next day. */
    uint16_t start_time; /**< Revision 3 ValidityStartTime, minutes. */
    uint16_t pass_duration; /**< Length of one pass, in @c duration_unit. */
    uint16_t stock_duration; /**< Revision 3 ExpiryDateSPDuration, days. */
    uint16_t vat; /**< AmountPaidVATSalesTax in 0.01% steps. */
    uint8_t valid_days; /**< ValidOnDayCode, ITSO_DOW_*. */
    uint8_t renew_quantity; /**< AutoRenewQuantity1: passes or days per renewal. */
    uint8_t travel_class; /**< EN1545 AccommodationClassCode. */
    uint8_t validity_code; /**< Owner-defined; zero is the null condition. */
    uint8_t promotion_code; /**< Owner-defined. */
    uint8_t adults;
    uint8_t children;
    uint8_t concessions;
    uint8_t paid_mop; /**< EN1545 PaymentMeansCode for AmountPaid. */
    uint8_t duration_unit; /**< ItsoDurationUnit. */
    uint8_t mode; /**< TYP23Mode, ItsoJourneyMode. */
    uint8_t max_transfers; /**< TYP 23 MaxTransfers per journey. */
    uint8_t time_limit; /**< TYP 23 TimeLimit between legs, in 30 second steps. */
    bool has_mode_group; /**< TYP 23 bitmap bit 3: the four elements above. */
    bool valid; /**< The fixed part of the dataset was long enough to read. */
    bool has_start_time;
    bool has_pass_duration;
    bool has_stock_duration;
} ItsoTicketTerms;

#define ITSO_CAP_ACCUMULATORS 4

/** One of the four accumulator sets of a Complex Capping extension. */
typedef struct {
    ItsoMoney uncapped; /**< What the fares would have come to without a cap. */
    ItsoMoney day; /**< Spent towards today's cap. */
    ItsoMoney multiday; /**< Spent towards the multi-day cap. */
    ItsoMoney last_fare; /**< LastFarePaid; VGXRef 2 only. */
    uint32_t cap_dts; /**< When the last cap was applied; VGXRef 2 only, 0 if never. */
    uint16_t day_count; /**< Days into a multi-day accumulation; 0 for single day. */
    uint8_t rule; /**< ItsoCapRule. */
    uint8_t last_txn; /**< EventTypeCode of the last fare paid. */
    ItsoLocation location; /**< Where the last cap was applied, or zones used. */
} ItsoCapAccumulator;

/** CapAccumulatorRule (TS 1000-5 tables AD1 and AD2). */
typedef enum {
    ItsoCapRuleNone = 0,
    ItsoCapRuleDay = 1, /**< Day capping only. */
    ItsoCapRuleShortPeriod = 2, /**< Accumulate for n days, n set by the strategy. */
    ItsoCapRuleLongPeriod = 3, /**< Accumulate for m days, m set by the strategy. */
} ItsoCapRule;

/**
 * A Complex Capping Value Group Extension (TS 1000-5 clause 4.1, VGXRef 1 and
 * 2): what a pay-as-you-go product has spent towards its fare caps.
 *
 * Decoded on demand rather than held in every ItsoProduct: four locations make
 * it larger than anything else a product carries, and at most one product on a
 * card has one.
 */
typedef struct {
    bool valid;
    uint8_t ref; /**< VGXRef: 1 reduced data, 2 full data. */
    uint16_t strategy; /**< CapStrategyCode: the owner's capping rule set, 0 if unused. */
    ItsoCapAccumulator acc[ITSO_CAP_ACCUMULATORS];
} ItsoCapping;

/**
 * One record of a product's Value Record Data Group.
 *
 * The group is a small cyclic store (TS 1000-2 clause 7): the live record is
 * the one with the highest TS#, and the records beside it are the transactions
 * before it - the balance as it was, and when it changed. Cards do not keep a
 * statement anywhere else, so this is the only history a product carries.
 *
 * Only the common header (table 15) and whichever of a balance or a counter the
 * IPE type keeps in its tail are held per record. The rest of the newest
 * record's tail - journey legs, flags, the expiry dates a period ticket keeps
 * there - is decoded into the product itself, because it describes the product
 * as it stands rather than a transaction that happened.
 */
typedef struct {
    uint32_t dts; /**< Raw DTS of the transaction. */
    ItsoMoney amount; /**< Balance after it, for the types that keep money. */
    uint32_t count; /**< Counter after it; meaning per ItsoProduct::count_kind. */
    uint16_t ts; /**< TS#: which write to the group this was. */
    uint8_t txn; /**< EventTypeCode: what the transaction was. */
    bool has_count;
    /* False for a record that came out of a saved file rather than out of the
     * group the card just offered. The card keeps two; everything before them
     * survives only because a file remembered it, and a screen that showed the
     * two kinds alike would claim the card still holds all of it. */
    bool on_card;
} ItsoValueRecord;

/** One entry of the Directory Data Group, plus whatever its IPE dataset yielded. */
typedef struct {
    bool present;
    uint8_t dir_index; /**< 1-based position E(i) in the directory. */

    /**
     * The card's directory listed this product on the read that is on screen.
     *
     * False for a product a saved file carries from an earlier read and the
     * card has since dropped - an expired ticket whose entry has been freed.
     * Such a product is as real as any other and decodes the same way; what it
     * is not is a statement about the card as it is now.
     */
    bool on_card;
    /** Unix time of the last read that found it on the card; 0 while it is. */
    uint32_t last_seen;

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
    uint16_t deposit_vat; /**< DepositVATSalesTax in 0.01% steps. */
    bool auto_top_up_internal; /**< TYP2ValueFlags bit 2: tops up from another purse. */

    /* Concessionary Pass Issuer Identity, or the owner's cost centre or ticket
     * subtype: TYP 16, 22 and 23 all carry one, at different offsets. */
    bool has_cpicc;
    uint16_t cpicc;

    /* The Value Group Extension a value record group carries, if any (TS 1000-2
     * clause 7.5). The extension itself is decoded by itso_parse_capping(). */
    uint8_t vgx_ref; /**< VGXRef, 0 when there is no extension. */

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

    /* Every record the group held, newest first, value_history[0] being the
     * live one the fields below describe. A saved card read again adds the
     * records the file already had, so this grows past what the card itself
     * keeps: see flipso_capture_merge_history(). */
    ItsoValueRecord value_history[ITSO_MAX_VALUE_RECORDS];
    uint8_t value_history_count;

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
    bool stored_passes; /**< TYP22ValueFlags bit 1: sold as a stock of passes. */
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

    /* The rest of an ITSO ID (TS 1000-5 tables 22 and 22a). */
    bool has_holder_id;
    uint32_t holder_id; /**< HolderID: the issuer's number for the holder or photo. */
    bool has_secondary_holder;
    uint32_t secondary_holder_id;
    uint8_t language; /**< ITSO language code, TS 1000-5 annex A.24; 0 if unset. */
    uint8_t rounding; /**< ITSO_ROUNDING_* bits. */
    bool has_half_days;
    uint16_t half_days; /**< HalfDayOfWeek, annex A.10: two periods per day. */
    bool has_shell_deposit;
    ItsoMoney shell_deposit; /**< ShellDeposit: paid for the card itself. */
    uint8_t shell_deposit_mop;
    uint16_t shell_deposit_vat;

    ItsoTicketTerms ticket; /**< TYP 22 only. */

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
    uint8_t entry_iin_index; /**< ENTRY_IIN_Index: that operator's network. */
    uint32_t entry_isam; /**< ENTRY group: ISAM of the check-in record. */
    uint32_t entry_isam_seq; /**< ...and its sequence number. */

    /* AMT group flags, format revision 2 on (TS 1000-5 table 60). */
    bool companion; /**< CompanionTravelled. */
    bool return_ticket; /**< ReturnTicket: the fare was for a return. */

    /* The record's own InstanceID, after its dataset: the ISAM of the POST
     * that wrote it, which names the operator whose reader took the tap. */
    bool has_writer;
    uint32_t writer_isam;

    bool latest; /**< Newest record, per the Log Directory Entry record offset. */
} ItsoTap;

/** Everything Flipso knows about one card. */
typedef struct {
    /* --- ITSO Shell Environment Data Group (TS 1000-2 clause 4) --- */
    bool shell_valid;
    ItsoShellReject shell_reject; /**< Which test rejected the shell, if one did. */
    char isrn[ITSO_ISRN_DIGITS + 1]; /**< 18-digit card number, IIN+OID+ISSN+check. */
    bool isrn_check_ok; /**< Luhn check digit verifies. */
    uint32_t iin; /**< Issuer Identification Number as a decimal value. */
    uint16_t oid; /**< Shell owner. */
    uint16_t expiry; /**< Raw DATE. */
    uint8_t format_rev; /**< ShellFormatRevision. */
    uint8_t fvc; /**< Format Version Code: 7 = DESFire CMD7, 12 = CMD12. */
    uint8_t ksc;
    uint8_t kvc;
    uint8_t shell_len; /**< ShellLength, in blocks of ITSO_SHELL_BLOCK_LEN. */

    /* ITSO Shell Environment Checksum (TS 1000-2 clause 4.1.15).
     *
     * This is the only integrity check on the shell that can be made without
     * keys. The data groups are sealed rather than encrypted, and a seal is a
     * MAC over a key Flipso does not have, so Flipso can report what a card
     * says but never whether it has been tampered with. A CRC cannot tell you
     * that either - anyone rewriting a shell would recompute it - but it does
     * catch the thing that actually goes wrong here, which is a misread. */
    bool secrc_checked; /**< The shell was long enough to hold its checksum. */
    bool secrc_valid;
    uint16_t secrc_stored;
    uint16_t secrc_computed;
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
    /* Directory InstanceID (TS 1000-2 table 8): the last ISAM to rewrite the
     * directory is the last device that changed anything on the card. */
    bool dir_instance_valid;
    uint8_t dir_kid;
    uint8_t shell_iteration; /**< INS#: hotlists name a shell by ISRN and this. */
    uint32_t dir_isam;

    /* --- Log Directory Entry (TS 1000-2 clause 8) --- */
    bool log_entry_valid;
    uint8_t log_dir_index; /**< Directory entry holding the log entry, 0 if none. */
    bool log_normal_mode; /**< LPF: normal mode references a transient ticket record. */
    uint8_t log_ptr; /**< Directory entry of the product used on the last tap. */
    uint8_t log_eei; /**< Entry/exit indicator: 0 = outside a closed system. */
    uint32_t log_dts;
    uint8_t log_record_offset; /**< RO: next record to be written. */
    uint8_t log_passback; /**< PTLBM, minutes. */

    /* The directory's products first, in entry order, then any the card has
     * dropped since a file was written - so a screen that walks the array in
     * order shows the card before it shows the card's past. */
    ItsoProduct products[ITSO_MAX_CARD_PRODUCTS];
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

/**
 * Read just the card number out of a Shell Environment Data Group.
 *
 * The card number is the only unique identity a card has, so this is how one
 * card is told from another without decoding either: a saved card is matched
 * to the card in the reader by comparing these, and an ItsoCard is several
 * kilobytes to build for the sake of eighteen digits.
 *
 * @param out at least ITSO_ISRN_DIGITS + 1 bytes.
 * @return false for bytes this decoder would not accept as a shell, in which
 *         case @p out is untouched.
 */
bool itso_shell_card_number(const uint8_t* data, size_t len, char* out);

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

/**
 * Locate the value records inside a product's concatenated sector chain.
 *
 * The Value Record Data Group starts in the sector after the one the IPE data
 * group ends in, which takes the IPE's own length and the card's geometry to
 * work out - so anything wanting the records themselves, rather than the
 * decode of them, would otherwise have to repeat that arithmetic.
 *
 * @param      group       every chained sector, concatenated in chain order.
 * @param      sector_size B from the shell; zero means the offset is unknowable.
 * @param[out] offset      offset of the first record within @p group.
 * @return number of ITSO_VALUE_RECORD_LEN records present, 0 when there is no
 *         group or it does not fit; @p offset is untouched in that case.
 */
uint8_t itso_value_records(const uint8_t* group, size_t len, uint8_t sector_size, size_t* offset);

/**
 * Decode value records an earlier read of this card saw.
 *
 * @param data ITSO_VALUE_RECORD_LEN records back to back, in any order.
 *
 * They join the product's history and are deduplicated against it, so a record
 * still on the card is not counted twice. Nothing here touches the product's
 * live fields: the card itself is the authority on what a product holds now,
 * and this is only what it held before.
 */
void itso_parse_value_history(ItsoProduct* product, const uint8_t* data, size_t len);

/**
 * Append a product the directory does not list, from the entry bytes that did.
 *
 * Only a saved card has these: the file kept the directory entry and the IPE
 * group of a product from a read that still found it, and the entry it sat in
 * has since been freed or taken by something else. The entry is decoded exactly
 * as itso_parse_directory() decodes a live one - it is the same five bytes - so
 * what follows can parse the group into it in the usual way.
 *
 * @param entry ITSO_DIR_ENTRY_LEN bytes of IPE Directory Entry.
 * @param index the 1-based directory position it occupied, for display.
 * @return the product, or NULL when there is no room for another. Status is
 *         left unknown: the chain terminator that says used or blocked lives in
 *         a Sector Chain Table that describes the card as it is now.
 */
ItsoProduct* itso_card_add_product(ItsoCard* card, const uint8_t* entry, uint8_t index);

/**
 * Mark a product, and everything it has decoded so far, as no longer on the
 * card.
 *
 * Called after itso_parse_ipe() rather than before it, because the value
 * records inside the group are read as live ones - they were, on the read that
 * captured them - and it is only knowing where the group came from that says
 * otherwise.
 *
 * @param last_seen Unix time of that read.
 */
void itso_product_off_card(ItsoProduct* product, uint32_t last_seen);

/** Decode the cyclic log into card->taps. */
void itso_parse_log(ItsoCard* card, const uint8_t* data, size_t len);

/**
 * Decode Transient Ticket Records an earlier read of this card saw.
 *
 * @param data ITSO_TAP_RECORD_LEN records back to back, in any order.
 *
 * Call it after itso_parse_log(), so that the record the card itself calls its
 * newest keeps the latest flag and so that the live log fills the array first.
 */
void itso_parse_log_history(ItsoCard* card, const uint8_t* data, size_t len);

/* ------------------------------------------------------------------ */
/* Raw records, for code that stores them rather than decoding them    */
/* ------------------------------------------------------------------ */

/**
 * B, the size of a memory sector, from a Shell Environment Data Group.
 *
 * Enough of the shell to find the value records in a product group, without
 * decoding a whole card to get at one byte. Zero when @p data is not a shell.
 */
uint8_t itso_shell_sector_size(const uint8_t* data, size_t len);

/**
 * The 1-based @p index'th IPE Directory Entry within a Directory Data Group.
 *
 * @return ITSO_DIR_ENTRY_LEN bytes, or NULL when the group is too short. The
 *         entry is returned whether or not it holds a product: an all-zero
 *         entry is an unused one.
 */
const uint8_t* itso_dir_entry(const uint8_t* dir, size_t len, uint8_t index);

/** True when a log slot holds a Transient Ticket Record at all. */
bool itso_tap_record_present(const uint8_t* record, size_t len);

/** True when tap record @p a was written later than @p b. */
bool itso_tap_record_newer(const uint8_t* a, const uint8_t* b);

/** True when value record @p a was written later than @p b, by TS#. */
bool itso_value_record_newer(const uint8_t* a, const uint8_t* b);

/* ------------------------------------------------------------------ */
/* Human-readable names for coded values                              */
/* ------------------------------------------------------------------ */

const char* itso_typ_name(uint8_t typ);
const char* itso_entitlement_name(uint8_t code);
const char* itso_profile_name(uint8_t code);
const char* itso_transaction_name(uint8_t code);
const char* itso_status_name(ItsoProductStatus status);
const char* itso_shell_reject_name(ItsoShellReject reject);

/** EN1545 PaymentMeansCode, e.g. "Cash" (TS 1000-5 annex A.12). */
const char* itso_payment_name(uint8_t code);

/* RoundingFlagsEnable, RoundingFlag and RoundingValueFlag (TS 1000-5 table 22):
 * how a POST rounds a half or proportional fare for this holder. */
#define ITSO_ROUNDING_ENABLED 0x01
#define ITSO_ROUNDING_FLAG    0x02
#define ITSO_ROUNDING_VALUE   0x04

/**
 * The operator an ISAM is registered to (TS 1000-2 annex B, tables B3 and B4).
 *
 * The top 13 bits of an ISAM ID are the OID; bits 18, 17 and 16 extend it into
 * the 8192, 24576 and 57344 ranges at the expense of the serial number. Every
 * ISAM ID on a card - who created a product, who last wrote a value record or
 * the directory, whose reader took a tap - can be named this way.
 */
uint16_t itso_isam_oid(uint32_t isam);

/** ITSO language code (TS 1000-5 annex A.24) as ISO 639-1, e.g. "en". False if unknown. */
bool itso_language_code(uint8_t code, char out[3]);

/** English name for the languages of the British Isles, else NULL. */
const char* itso_language_name(uint8_t code);

/**
 * Decode a Complex Capping Value Group Extension out of a product group - the
 * same chained IPE and Value Record groups itso_parse_ipe() takes.
 *
 * @param valc currency of the amounts, which the extension does not carry: the
 *             value records' ValueCurrencyCode applies.
 * @return false when the group has no VGXRef 1 or 2 extension.
 */
bool itso_parse_capping(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    uint8_t valc,
    ItsoCapping* out);

/** HalfDayOfWeek as a ValidOnDayCode-style day mask: a day counts if either period does. */
uint8_t itso_half_days_mask(uint16_t half_days);

/** EN1545 AccommodationClassCode, e.g. "Standard". NULL for 0, "unknown". */
const char* itso_class_name(uint8_t code);

/**
 * The days a TYP 22 ticket may be used, as a ValidOnDayCode-style mask.
 *
 * Rule 7 of TS 1000-5 clause 2.9.1.4 requires ValidOnDayCode and the day's
 * TYP22Flags to both allow it, so a day either one excludes is dropped. A filter
 * that is entirely zero is taken as not in use rather than as "never valid": a
 * ticket nobody could travel on is not a product anyone sells.
 */
uint8_t itso_ticket_days(uint8_t valid_on_day, uint16_t flags);

/**
 * Render a day mask as briefly as it will go: "every day", "Mon-Fri",
 * "Sat Sun", "Mon Wed Fri". The special-days bit is not rendered; callers say
 * what they mean by it.
 */
void itso_format_days(uint8_t days, char* out, size_t len);

/**
 * The days in @p days that TYP22Flags allows for only half of, e.g. "Sat PM
 * only". Empty when there are none, including when the flags do not restrict by
 * day at all.
 */
void itso_format_part_days(uint8_t days, uint16_t flags, char* out, size_t len);

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
