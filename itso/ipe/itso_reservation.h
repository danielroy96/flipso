/**
 * @file itso_reservation.h
 * @brief The parts of a TYP 24 reserved journey decoded on demand (TS 1000-5 clause
 * 2.11 and table AD3).
 */
#pragma once

#include "../itso_location.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most of each optional group a TYP 24 dataset can count: the first four
 * counts are two bits wide and the other four three (TS 1000-5 table 136), and
 * NumberOfReservations is four (table 139). */
#define ITSO_T24_SMALL_GROUP_MAX 3
#define ITSO_T24_LARGE_GROUP_MAX 7
#define ITSO_T24_LEGS_MAX        15

/* DiscountCodeType. TS 1000-5 leaves it to the owner; RSPS3002 section 3.8.3,
 * National Rail's profile for ITSO, defines these three. */
#define ITSO_DISCOUNT_STATUS      1 /**< A railcard or status code: "YNG", "DIS". */
#define ITSO_DISCOUNT_LENNON      2 /**< A discount code from the rail settlement system. */
#define ITSO_DISCOUNT_ENTITLEMENT 3 /**< From a TYP 14 or 16 on the card: "XXXXX". */

/** One Discounts group of a TYP 24 (table 136): a railcard or promotion applied. */
typedef struct {
    uint8_t code[5]; /**< DiscountCode: a railcard code, three characters on rail. */
    uint8_t type; /**< DiscountCodeType, ITSO_DISCOUNT_* on rail. */
    /** DiscountPercentage as stored: tenths of a percent by TS 1000-5, a whole
     *  percent by RSPS3002, which rounds 33.3% to 33. See itso_discount_tenths(). */
    uint16_t percentage;
    ItsoMoney amount; /**< DiscountAmount, in the ticket's currency; 0 when a percentage. */
} ItsoDiscount;

/** True for a code type RSPS3002 defines, which marks a discount as rail's. */
static inline bool itso_discount_is_rail(const ItsoDiscount* discount) {
    return discount->type >= ITSO_DISCOUNT_STATUS && discount->type <= ITSO_DISCOUNT_ENTITLEMENT;
}

/** A discount's percentage in tenths of a percent, read by whose rules it follows. */
static inline uint16_t itso_discount_tenths(const ItsoDiscount* discount) {
    return itso_discount_is_rail(discount) ? (uint16_t)(discount->percentage * 10) :
                                             discount->percentage;
}

/* TransferEntitlementType 2, as RSPS3002 defines it: break of journey allowed,
 * with NumberOfTransfers at its 511 maximum. Its type 1, a cross-London
 * marker, is withdrawn in favour of the Interchange group. */
#define ITSO_TRANSFER_BREAK_OF_JOURNEY 2
#define ITSO_TRANSFERS_UNLIMITED       511

/** A TYP 24 Transfers group: transfers of one type, and how long they outlast it. */
typedef struct {
    uint8_t type; /**< TransferEntitlementType: owner-coded, ITSO_TRANSFER_* on rail. */
    uint16_t count; /**< NumberOfTransfers, 9 bits. */
    uint8_t hours; /**< ExtendedValidityPeriod: hours past the ticket's own end. */
} ItsoTransfer;

/** A TYP 24 Interchange group: a break of journey, out at one place and in at another. */
typedef struct {
    ItsoLocation exit; /**< OutOfLocationInterchangeExit. */
    ItsoLocation entry; /**< OutOfLocationInterchangeEntry; may be the exit. */
    uint8_t minutes; /**< PermittedInterchangeTime. */
} ItsoInterchange;

/* TimeBandOnOutOrReturn. Table 136 calls it a two-bit bitmap without assigning
 * the bits; read the way BerthUpperLower's two bits are assigned in table AD3. */
#define ITSO_T24_BAND_OUTWARD 0x01
#define ITSO_T24_BAND_RETURN  0x02

/** A TYP 24 Restriction1 group: a time band the ticket is, or is not, good in. */
typedef struct {
    uint8_t operator_code[2]; /**< OperatorApplicability, UD; zero for every operator. */
    ItsoLocation location; /**< SpecificLocationApplicability. */
    uint8_t portion; /**< TimeBandOnOutOrReturn, ITSO_T24_BAND_*. */
    uint16_t start; /**< TimeBandStart, minutes past midnight. */
    uint16_t end; /**< TimeBandEnd. */
    /* The two flags are named "XOrY", and a set bit read as X, as TYP24Flags'
     * own TestOrLive is defined (table 138): the table does not say. */
    bool arrival; /**< TimeBandOnArriveOrDepart: set, the band is on arrival. */
    bool include; /**< TimeBandIncludeExcludeFlag: set, valid within the band. */
} ItsoTimeBand;

/** A TYP 24 Restriction2 group: one train the ticket is, or is not, good on. */
typedef struct {
    ItsoLocation departs; /**< SpecificVehicleDepartureLocation: where it starts. */
    uint8_t service[6]; /**< SpecificServiceId, UD. */
    uint16_t time; /**< SpecificVehicleDepartureTime, minutes, from @c departs. */
    /** RestrictionOrEasementFlag: set, a restriction - not valid on it; clear,
     *  an easement - valid on it though it would not otherwise be. */
    bool restriction;
} ItsoServiceRule;

/** A TYP 24 Route group: a place the journey must, or must not, pass. */
typedef struct {
    ItsoLocation location; /**< RoutingLocation. */
    uint8_t via; /**< ViaNotVia, UD: 1 via, 0 not via, as the name orders them. */
} ItsoRoutePoint;

/* SeatDirection, EN1545 SeatPositionCode in two bits, in the order table AD3
 * lists them - "Facing, Back or Airline - or null if not used" - and as
 * RSPS3002 section 3.8.6 assigns them, 01, 10 and 11. */
#define ITSO_SEAT_FACING  1
#define ITSO_SEAT_BACK    2
#define ITSO_SEAT_AIRLINE 3

/** ReservationType: UD in table AD3, and defined this way by RSPS3002 3.8.6. */
typedef enum {
    ItsoPlaceSeat = 0,
    ItsoPlaceBerth = 1,
    ItsoPlaceBike = 2,
    ItsoPlaceNone = 3, /**< A reservation with no place: coach and seat are null. */
    ItsoPlaceWheelchair = 4,
} ItsoPlaceType;

/** One reserved leg, from a TYP 24's VGXRef 3 extension (table AD3). */
typedef struct {
    ItsoDts departs; /**< LegDepartureDateTime. */
    char service[7]; /**< LegServiceId: the retail service ID. */
    ItsoLocation from;
    ItsoLocation to;
    char coach[3];
    char seat[4];
    /** AccommodationAttribute: on rail four characters from the National
     *  Reservation System's reference data; see itso_seat_attribute_name(). */
    char attribute[5];
    uint8_t direction; /**< SeatDirection, ITSO_SEAT_*; 0 not used. */
    uint8_t berth; /**< BerthUpperLower: 1 lower, 2 upper, 0 not specified. */
    uint8_t type; /**< ReservationType, ItsoPlaceType on rail. */
    bool together; /**< TogetherFlag: a sleeper cabin shared. */
} ItsoReservedLeg;

/**
 * Everything a TYP 24 reserved journey holds beyond what ItsoProduct keeps of
 * it: the rest of its dataset, its eight optional groups, the passenger, and
 * the reservations its Value Group Extension carries (TS 1000-5 table 136 and
 * clause 4.1.3).
 *
 * Decoded on demand by itso_parse_reservation() rather than held in every
 * product: its locations alone are more than a product costs. The groups with
 * locations in them are allocated to fit, so a ticket costs what it carries -
 * release one with itso_reservation_free().
 */
typedef struct {
    bool valid; /**< The fixed part of the dataset, through VendorLoc, was read. */
    uint8_t ticket_number[4]; /**< TicketNumber, UD: the ticket's reference. */
    uint8_t operator_code[2]; /**< OperatorSpecificity, UD; zero for any operator. */
    uint8_t ftot[3]; /**< FaresTypeOfTicket, UD: the rail FTOT code. */
    uint8_t id_doc[4]; /**< IdDocumentReference, UD: a railcard or photocard. */
    uint8_t restriction_code[2]; /**< RestrictionCode, UD. */
    uint8_t valid_days; /**< DaysTravelPermitted, ITSO_DOW_*. */
    uint8_t restricted_days; /**< DaysRestrictionApplies, ITSO_DOW_*. */
    uint8_t renew_days; /**< AutoRenewTimeAfterExpiry, days. */
    /** AmountPaidCurrencyCode, which the discount amounts are taken to be in:
     *  table 136 gives them none of their own. */
    uint8_t valc;
    ItsoLocation alt_from; /**< AlternativeOrigin. */
    ItsoLocation alt_to; /**< AlternativeDestination. */
    ItsoLocation vendor; /**< VendorLoc: where it was sold. */

    /* IPEBitMap bit 2: the optional groups, in table 136's order, each counted
     * as far as the dataset holds it. */
    /** A group's count ran past the end of the dataset. Nothing from there on
     *  can be found, so the groups after it are left empty. */
    bool overrun;
    uint8_t associated[ITSO_T24_SMALL_GROUP_MAX]; /**< Directory entries. */
    uint8_t associated_count;
    ItsoDiscount discounts[ITSO_T24_SMALL_GROUP_MAX];
    uint8_t discount_count;
    char supplements[ITSO_T24_SMALL_GROUP_MAX][4]; /**< AssociatedSupplementCode. */
    uint8_t supplement_count;
    ItsoInterchange* interchanges;
    uint8_t interchange_count;
    ItsoTransfer transfers[ITSO_T24_SMALL_GROUP_MAX];
    uint8_t transfer_count;
    ItsoTimeBand* time_bands;
    uint8_t time_band_count;
    ItsoServiceRule* services;
    uint8_t service_count;
    ItsoRoutePoint* routes;
    uint8_t route_count;

    /* IPEBitMap bit 1: PaxDetail. Personal data, like an ID's name. */
    bool has_passenger;
    char passenger[21];
    uint8_t gender; /**< EN1545 GenderCode: 1 male, 2 female. */

    /* The VGXRef 3 extension (table AD3). */
    bool has_extension;
    ItsoDts last_validation; /**< DTSOfLastValidation; 0 never. */
    ItsoLocation last_validation_at;
    char booking[9]; /**< BookingReference. */
    ItsoReservedLeg* legs;
    uint8_t leg_count;
} ItsoReservation;

/**
 * Decode the parts of a TYP 24 reserved journey ItsoProduct does not keep -
 * see ItsoReservation - out of the same product group itso_parse_ipe() takes.
 *
 * @param reservations the live value record's NumberOfReservations, which says
 *                     how many legs the extension holds: ItsoTicketTerms keeps it.
 * @return false when the dataset's fixed part did not read. @p out may still
 *         own allocations either way: release it with itso_reservation_free().
 */
bool itso_parse_reservation(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    uint8_t reservations,
    ItsoReservation* out);

/** Release what an ItsoReservation owns, leaving it empty. */
void itso_reservation_free(ItsoReservation* res);

#ifdef __cplusplus
}
#endif
