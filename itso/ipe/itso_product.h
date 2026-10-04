/**
 * @file itso_product.h
 * @brief One product: its directory entry, its IPE dataset and its value records.
 *
 * The data model every IPE type decodes into, and the entry points that fill it:
 * itso_parse_ipe() for the IPE Data Group and the Value Record Data Group bound
 * to it. The type-specific decoders behind it are one file per TYP in this
 * directory; see itso_ipe.c for which types are decoded.
 */
#pragma once

#include "../itso_location.h"

#ifdef __cplusplus
extern "C" {
#endif

/** TYP23Mode: how a journey ticket's rides are counted (TS 1000-5 tables 35a, 35b). */
typedef enum {
    ItsoJourneyModeStoredRides = 0, /**< Each ride uses one. */
    ItsoJourneyModeStoredJourneys = 1, /**< Each journey uses one, legs within limits. */
    ItsoJourneyModeSimple = 2, /**< An ordinary ticket, the default. */
    /** Revision 3 only, RFU before it: journeys in outward and return pairs, each
     *  leg within the same limits as stored journeys (table 35b). */
    ItsoJourneyModeReturn = 3,
} ItsoJourneyMode;

/* PrintTicket and PrintReceipt: flags 5 and 6 of TYP2Flags, TYP4Flags,
 * TYP5Flags, TYP22Flags and TYP23Flags (TS 1000-5 tables 5, 13, 18, 30 and 34),
 * and for PrintTicket alone IDFlags bit 5 (table 24) - what a machine should
 * print when the product is used. */
#define ITSO_PRINT_TICKET  0x01
#define ITSO_PRINT_RECEIPT 0x02

/* Room kept for a revision 3 period ticket's IdentityDocumentID (TS 1000-5
 * table 3.27), which may run to 31 bytes. A photocard or railcard number is a
 * fraction of that, and the element is in every product slot. */
#define ITSO_ID_DOC_LEN 16

/** IdentityDocumentIDType: how IdentityDocumentID is coded (TS 1000-5 table 3.27). */
typedef enum {
    ItsoIdDocHex = 1, /**< A number. */
    ItsoIdDocAscii = 2, /**< Text. */
    ItsoIdDocEntry = 3, /**< The directory entry of another product on the card. */
} ItsoIdDocType;

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
#define ITSO_T22_TRANSFERABLE   (1u << 0)
#define ITSO_T22_PRINT_TICKET   (1u << 5)
#define ITSO_T22_PRINT_RECEIPT  (1u << 6)
/** Revision 3's TreatmentOfExpiredSP (table 3.30, rule 8 of clause 2.9.3.4):
 *  set, a top-up adds its passes to any that have expired; clear, the expired
 *  ones are written off. RFU in revisions 1 and 2. */
#define ITSO_T22_KEEP_EXPIRED   (1u << 7)
#define ITSO_T22_OFF_PEAK_ONLY  (1u << 8)
#define ITSO_T22_WEEKDAY_AM     (1u << 9)
#define ITSO_T22_WEEKDAY_PM     (1u << 10)
#define ITSO_T22_SATURDAY_AM    (1u << 11)
#define ITSO_T22_SATURDAY_PM    (1u << 12)
#define ITSO_T22_SUNDAY_AM      (1u << 13)
#define ITSO_T22_SUNDAY_PM      (1u << 14)
#define ITSO_T22_PUBLIC_HOLIDAY (1u << 15)
#define ITSO_T22_DAY_MASK       0xFE00u

/* TYP24Flags, numbered from the least significant of its twelve bits (TS
 * 1000-5 table 138); 10 and 11 are RFU. */
#define ITSO_T24_FOLLOW_ON     (1u << 0) /**< A follow-on renewal ticket. */
#define ITSO_T24_DUPLICATE     (1u << 1)
#define ITSO_T24_REPLACEMENT   (1u << 2)
#define ITSO_T24_WARRANT       (1u << 3) /**< UnfulfilledWarrant. */
#define ITSO_T24_CARNET        (1u << 4)
/** TestOrLive: set, a test ticket, which is not valid for travel. */
#define ITSO_T24_TEST          (1u << 5)
#define ITSO_T24_PASSENGER     (1u << 6) /**< The dataset holds a name and gender. */
#define ITSO_T24_SEAT_REQUIRED (1u << 7) /**< ReservationsMandatory. */
#define ITSO_T24_COMPANION     (1u << 8) /**< CompanionPermitted. */
#define ITSO_T24_AUTO_RENEW    (1u << 9)

/** ProductTypeEncoding: how a TYP 24's NumberOfJourneysSold is spent (table 136). */
typedef enum {
    ItsoSoldOneWay = 0, /**< n journeys in one direction: n = 1 is a single. */
    ItsoSoldReturns = 1, /**< n journeys, taken in outward and return pairs. */
    ItsoSoldEitherWay = 2, /**< n journeys in either direction. */
} ItsoSoldAs;

/**
 * The terms a TYP 22 period ticket, TYP 23 journey ticket or TYP 24 reserved
 * journey is sold on (TS 1000-5 tables 27, 27a, 3.27, 31, 31a, 31b and 136),
 * and what its live value record says about it now.
 *
 * The ticket family's ItsoTerms: a Space Saving IPE (TYP 27-29) fills the
 * elements it shares with them. They share most of these elements, at offsets
 * that differ between the types and again between each type's revisions.
 */
typedef struct {
    ItsoMoney amount_paid; /**< AmountPaid; not valid when the card records none. */
    ItsoMoney ride_value; /**< TYP 23 ValueOfRideJourney: nominal value of one ride. */
    uint32_t photocard; /**< TYP 23 PhotocardNumber; 0 when not recorded. */
    /** Revisions 1 and 2 of TYP 22: ValidityStartDTS; TYP 24: OutPortionValidFrom.
     *  0 if unset. */
    ItsoDts valid_from_dts;
    /* TYP 24 (tables 136 and 139): the parts of a reserved journey its summary
     * and list row need. The rest of its dataset, and its reservations, are
     * decoded on demand by itso_parse_reservation() - see ItsoReservation. */
    ItsoDts return_from_dts; /**< RtnPortionValidFrom; 0 if unset. */
    uint16_t outward_days; /**< OutPortionPeriodOfValidity: days on from valid_from_dts. */
    uint16_t return_days; /**< RtnPortionPeriodOfValidity: days on from return_from_dts. */
    uint16_t journeys_sold; /**< NumberOfJourneysSold, "n". */
    /** TransfersRemaining: one 11-bit count, the total over every transfer type. */
    uint16_t transfers_left;
    uint8_t sold_as; /**< ProductTypeEncoding, ItsoSoldAs. */
    /** NumberOfReservations in the live value record; 0 unless IPEBitMap bit 3. */
    uint8_t reservations;
    /** The first DiscountCode, which is usually the railcard the ticket is not
     *  valid without, and so goes on the summary. */
    bool has_discount;
    uint8_t discount[5];
    bool part_used; /**< JourneyPartUsedFlag: part-way through a leg. */
    uint16_t flags; /**< TYP22Flags, ITSO_T22_*, or TYP24Flags, ITSO_T24_*. */
    ItsoDate issue_date; /**< IssueDate; 0 when not recorded. */
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
    /** RouteCode, owner-defined, zero when unused (revisions 2 and 3 of TYP 22 and
     *  23), or TYP 24's Route. */
    uint8_t route_code[5];
    bool has_route_code;
    /* TYP 22 revision 3 IdentityDocumentID: the ID the holder must carry. */
    bool has_id_doc;
    uint8_t id_doc_type; /**< ItsoIdDocType. */
    uint8_t id_doc_len; /**< Bytes the card holds, which may exceed ITSO_ID_DOC_LEN. */
    uint8_t id_doc[ITSO_ID_DOC_LEN];
    bool valid; /**< The fixed part of the dataset was long enough to read. */
    bool has_start_time;
    bool has_pass_duration;
    bool has_stock_duration;

    /* What the live value record says about the ticket as it stands. */
    bool ticket_used; /**< TYP23ValueFlags UsedChecked: the ticket has been used. */
    bool stored_passes; /**< TYP22ValueFlags bit 1: sold as a stock of passes. */
    bool has_transfers;
    uint8_t transfers; /**< TYP 23 CountTransfers on the current journey. */
    bool has_stored_expiry;
    ItsoDate stored_expiry; /**< ExpiryDateSP/SRJ: expiry of the unactivated stock. */
    bool has_current_expiry;
    ItsoDate current_expiry; /**< TYP 22 ExpiryDateCurrent: the pass in use. */
} ItsoTicketTerms;

/**
 * What a purse or an account holds beyond the elements every product shares:
 * TYP 2 stored travel rights, TYP 4 charge to account and TYP 5 charge to
 * account with a transaction allowance. The purse family's ItsoTerms.
 */
typedef struct {
    /* Stored value (TYP 2 Value, TYP 4 CumulativeAmount). The two are the same
     * field in the same place and differ only in sign of meaning: a purse counts
     * down, a charge-to-account accumulates. */
    ItsoMoney balance;
    bool balance_is_spend; /**< TYP 4: the amount is spend to date, not credit. */

    /* Purse and credit limits held in the IPE dataset rather than the value
     * record: TYP 2 (table 2), TYP 4 (table 10) and TYP 5 (table 15). */
    bool has_limits;
    ItsoMoney max_value; /**< MaxValue2/4/5: ceiling on balance or spend. */
    ItsoMoney max_negative; /**< TYP 2 MaximumNegativeAmount: permitted overdraft. */
    bool has_top_up;
    ItsoMoney top_up_threshold; /**< Auto-top-up trigger level. */
    ItsoMoney top_up_amount;
    bool auto_top_up;
    bool auto_top_up_internal; /**< TYP2ValueFlags bit 2: tops up from another purse. */
    bool priority_override; /**< This IPE is to be spent before any other. */

    /* TYP 5 charge period (table 15). */
    bool has_charge_period;
    uint8_t weeks_per_period;
    uint8_t max_transactions;
    bool has_last_reset;
    ItsoDate last_reset; /**< TYP 5 LastResetDate. */

    /* Validity window carried inside the dataset, distinct from the directory
     * expiry: TYP 4/5 EndDate. */
    bool has_end_date;
    ItsoDate end_date;

    /* Multi-leg journey in progress (TYP 2, 4 and 5 value records). */
    bool has_journey;
    uint8_t journey_legs; /**< CountJourneyLegs. */
    ItsoMoney cumulative_fare;
} ItsoPurseTerms;

/**
 * Who an ITSO ID (TYP 16) or entitlement (TYP 14) belongs to, and what it
 * entitles them to (TS 1000-5 tables 20, 22 and 22a). The ID family's
 * ItsoTerms.
 */
typedef struct {
    bool has_name;
    char name[ITSO_NAME_LEN];
    bool has_dob;
    uint16_t dob_year; /**< DateOfBirth, stored as Datef BCD rather than a DATE. */
    uint8_t dob_month;
    uint8_t dob_day;
    bool has_id_flags;
    uint8_t id_flags; /**< IDFlags, TS 1000-5 table 24. */
    bool has_entitlement;
    uint8_t entitlement_code; /**< EN1545 EntitlementTypeCode. */
    uint8_t concession_class; /**< EN1545 ProfileCodeIOP. */
    bool has_sub_expiry;
    ItsoDate sub_expiry; /**< Entitlement expiry, distinct from IPE expiry. */
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
} ItsoIdTerms;

/** Which ItsoTerms a type fills. */
typedef enum {
    ItsoFamilyOther, /**< None: loyalty, vouchers, tolling and anything unknown. */
    ItsoFamilyPurse, /**< ItsoPurseTerms: TYP 2, 4 and 5. */
    ItsoFamilyId, /**< ItsoIdTerms: TYP 14 and 16. */
    ItsoFamilyTicket, /**< ItsoTicketTerms: TYP 22, 23, 24 and 27-29. */
} ItsoFamily;

/**
 * What only one family of types carries. No product is of two families, so
 * they share their room, and what a product would have spent on the other
 * two's elements is about a quarter of its size.
 */
typedef union {
    ItsoPurseTerms purse;
    ItsoIdTerms id;
    ItsoTicketTerms ticket;
} ItsoTerms;

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
    ItsoDts dts; /**< When the transaction was. */
    /* A type keeps money or a counter in its tail, never both, so the two share
     * their room and @c has_count says which is there. Read @c count only when
     * it is set and @c amount only when it is not: the other one is the same
     * bytes read as something they are not. */
    union {
        ItsoMoney amount; /**< Balance after it, for the types that keep money. */
        uint32_t count; /**< Counter after it; meaning per ItsoProduct::count_kind. */
    };
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
    /** The last read that found it on the card; 0 while it is. */
    ItsoUnixTime last_seen;

    uint16_t oid; /**< Operator that owns the product, after any EF extension. */
    bool oid_extended; /**< EF was set: the operator is in the extended IPE-owner range. */
    uint8_t typ;
    uint8_t ptyp;
    bool value_group; /**< VGP: a Value Record Data Group follows the IPE. */
    bool foreign_iin; /**< IINL: operator belongs to a different network. */
    ItsoDate expiry; /**< 0 means "no expiry" (decodes to 2041-11-10). */
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

    /* A deposit paid for the product: TYP 2, 4 and 5 (tables 2, 10 and 15),
     * and an ID's (table 22). */
    bool has_deposit;
    ItsoMoney deposit;
    uint8_t deposit_mop; /**< EN1545 PaymentMeansCode the deposit was paid by. */
    uint16_t deposit_vat; /**< DepositVATSalesTax in 0.01% steps. */

    /* What a machine should print on use: ITSO_PRINT_* in @c print_flags, for
     * the ones the type defines in @c print_defined (none, or ticket only on an
     * ID). */
    uint8_t print_defined;
    uint8_t print_flags;

    /* TYP 3's UserDefined: two bytes of the live value record the loyalty
     * scheme's owner uses as it likes (TS 1000-5 table 9). */
    bool has_owner_data;
    uint16_t owner_data;

    /* Concessionary Pass Issuer Identity, or the owner's cost centre or ticket
     * subtype: TYP 14, 16, 22 and 23 all carry one, not always in one place. */
    bool has_cpicc;
    uint16_t cpicc;

    /* The Value Group Extension a value record group carries, if any (TS 1000-2
     * clause 7.5). The extension itself is decoded by itso_parse_capping(). */
    uint8_t vgx_ref; /**< VGXRef, 0 when there is no extension. */

    /* Value Record Data Group (TS 1000-2 clause 7), present when VGP is set.
     * Every value record shares a common header (table 15); the five bytes after
     * it mean whatever the IPE type says they mean, which is what the
     * type-specific fields below are for. */
    bool value_parsed; /**< A live value record was found and decoded. */

    /**
     * Every record the group held, newest first, value_history[0] being the
     * live one the fields below describe. A saved card read again adds the
     * records the file already had, so this grows past what the card itself
     * keeps: see flipso_capture_merge_history().
     *
     * Allocated to fit, one record at a time, and NULL while there are none:
     * a card keeps two and an ID none, where a fixed array of
     * ITSO_MAX_VALUE_RECORDS spent most of its room on slots nothing filled.
     * The product owns it, so a product copied by assignment shares it with
     * the original - free only one of them, with itso_product_free(), which
     * itso_card_reset() does for every product a card holds.
     */
    ItsoValueRecord* value_history;
    uint8_t value_history_count;

    ItsoDts value_dts; /**< When the value record we picked was written. */
    uint8_t value_txn; /**< EventTypeCode: what the last transaction was. */
    uint16_t value_ts; /**< TS#: how many times the record has been written. */
    uint32_t value_isam; /**< ISAMIDModifier: the POST that wrote the record. */
    uint8_t value_action_seq; /**< ActionSequenceNumber, for action lists. */
    /** ValueCurrencyCode of the live record, scaling bits and all: TYP 2, 4 and 5
     *  price their IPE's limits in it (TS 1000-5 tables 2, 10 and 15). */
    uint8_t value_valc;

    /* A counter, where the type keeps one. */
    ItsoCountKind count_kind;
    uint32_t count;
    bool auto_renew; /**< TYP 22, 23, 25 and 26 value flags. */

    /* When it starts, and how long a POST must wait before it is used again:
     * elements an ID, a period ticket, a journey ticket and a purse place
     * differently, but mean the same by. */
    bool has_start;
    ItsoDate start; /**< Entitlement or validity start. */
    bool has_passback;
    uint8_t passback; /**< PassbackTime in minutes; 0 means the POST decides. */

    /**
     * What only its family carries; see ItsoFamily. Written by the decoders, and
     * read through itso_product_purse(), itso_product_id() and
     * itso_product_ticket(), which give a product of another family terms with
     * nothing set rather than another family's bytes.
     */
    ItsoTerms terms;

    /* A Space Saving IPE (TYP 27/28/29), the one product a CMD4 paper ticket
     * carries, decoded. Its own elements are in ItsoCard::space, which is set
     * whenever this is; see ItsoSpaceSaving. */
    bool space_saving;

    ItsoLocation from;
    ItsoLocation to;
} ItsoProduct;

/** Which ItsoTerms a product of type @p typ fills. */
ItsoFamily itso_product_family(uint8_t typ);

/*
 * A product's terms, read as its family's: what @p product holds when it is of
 * that family, and terms with nothing set when it is not - which is what a
 * product of the family whose dataset could not be read has too. Every reader
 * of ItsoProduct::terms outside the decoders goes through these, so no screen
 * can mistake one family's bytes for another's.
 */
const ItsoPurseTerms* itso_product_purse(const ItsoProduct* product);
const ItsoIdTerms* itso_product_id(const ItsoProduct* product);
const ItsoTicketTerms* itso_product_ticket(const ItsoProduct* product);

/**
 * Decode the IPE Data Group (and any Value Record Data Group) for one product.
 *
 * @p product's value history must start empty, as a fresh card slot's does. The
 * value records are allocated into ItsoProduct::value_history, so a product
 * that is not one of a card's must be released with itso_product_free().
 *
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
 * Locate the second copy of a product's Value Record Data Group, on media with
 * software anti-tear (TS 1000-10 annex A.3.2): CMD2, CMD9 and CMD10.
 *
 * Such a card keeps two copies, chained after the IPE in the order current then
 * previous, and writes each transaction into the previous copy before relinking
 * it as current. So the two hold alternate records - odd TS# in one, even in
 * the other - and the history is only whole with both. The previous copy starts
 * in the sector after the current one ends, and holds as many records as it
 * (clause A.3.2.2), which is how it is told from whatever else a chain might
 * run on into. A card with hardware anti-tear keeps one copy, and its chain ends
 * with it.
 *
 * @return records in the previous copy, 0 when there is none; @p offset is
 *         untouched in that case.
 */
uint8_t itso_previous_value_records(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    size_t* offset);

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
 * Mark a product, and everything it has decoded so far, as no longer on the
 * card.
 *
 * Called after itso_parse_ipe() rather than before it, because the value
 * records inside the group are read as live ones - they were, on the read that
 * captured them - and it is only knowing where the group came from that says
 * otherwise.
 *
 * @param last_seen the time of that read.
 */
void itso_product_off_card(ItsoProduct* product, ItsoUnixTime last_seen);

/** Release the value history @p product owns, leaving it with none. */
void itso_product_free(ItsoProduct* product);

/**
 * True when two products decoded to the same thing: every field, and the value
 * records themselves rather than where they happen to be allocated.
 */
bool itso_product_equal(const ItsoProduct* a, const ItsoProduct* b);

/** True when value record @p a was written later than @p b, by TS#. */
bool itso_value_record_newer(const uint8_t* a, const uint8_t* b);

/* RoundingFlagsEnable, RoundingFlag and RoundingValueFlag (TS 1000-5 table 22):
 * how a POST rounds a half or proportional fare for this holder. */
#define ITSO_ROUNDING_ENABLED 0x01
#define ITSO_ROUNDING_FLAG    0x02
#define ITSO_ROUNDING_VALUE   0x04

/**
 * The station that sold @p product, when its ProductRetailer names one rather
 * than an operator.
 *
 * Bit 15 alone cannot say so: TS 1000-2 table B2 lets 57344 to 65535 be a
 * retailer's OID. The range below that, 32768 to 57343, is a gap no OID may
 * use, and it holds every NLC whose first character is '0' to 'N' - every
 * numeric NLC among them - so there the element can only be rail's. It is read
 * that way on the three types RSPS3002 gives the form to, TYP 22, 23 and 24.
 * Above the gap it is a retailer's OID on every one of them: an NLC starting
 * 'O' to 'V' would land there too, but the station table names only numeric
 * NLCs, so none of those would have a name to show.
 *
 * @return false for a product sold by an operator, leaving @p out empty.
 */
bool itso_product_sold_at(const ItsoProduct* product, ItsoLocation* out);

/** IDFlags bit 0: the card surface carries a photo of the holder. */
static inline bool itso_id_personalised(uint8_t id_flags) {
    return (id_flags & 0x01) != 0;
}

/** IDFlags bit 4: a companion travels at the holder's concessionary rate. */
static inline bool itso_id_companion(uint8_t id_flags) {
    return (id_flags & 0x10) != 0;
}

#ifdef __cplusplus
}
#endif
