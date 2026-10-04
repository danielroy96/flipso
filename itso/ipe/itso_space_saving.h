/**
 * @file itso_space_saving.h
 * @brief The parts of a Space Saving IPE (TYP 27, 28, 29) a full IPE has no field for.
 */
#pragma once

#include "../itso_location.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The pass flags of the Space Saving IPEs, numbered from the least significant
 * bit: TYP27PassFlags, TYP28PassFlags and TYP29PassFlags (TS 1000-5 tables 49,
 * 52 and 56) share the one definition. */
#define ITSO_SS_OFF_PEAK    (1u << 0) /**< Valid off-peak only. */
#define ITSO_SS_WEEKDAY     (1u << 1) /**< Valid weekdays only. */
#define ITSO_SS_FIRST_CLASS (1u << 2) /**< First class rather than standard. */
/** Clear: the ticket ends at 23:59. Set: at a time the IPE owner configures in
 *  its readers, which may run past midnight or stop short of it. */
#define ITSO_SS_EXPIRY_TIME (1u << 3)

/**
 * How a Space Saving IPE's area element restricts where it is good (TS 1000-5
 * tables 50, 53 and 57). A reference fare code is the owner's own, so even code
 * 0 says only that the operator decides - a whole-network day ticket and a
 * one-zone ticket may both carry one.
 */
typedef enum {
    ItsoAreaFareCode, /**< An owner-defined reference fare code. */
    ItsoAreaFareValue, /**< An actual fare value, in minor currency units. */
    ItsoAreaLocation, /**< A LOC3 or LOC4 of the LocDefType held in the value. */
} ItsoAreaKind;

/**
 * The parts of a Space Saving IPE (TYP 27/28/29, TS 1000-5 clauses 2.14-2.16)
 * that a full IPE has no field for.
 *
 * Held once on the card rather than in every product slot: only a CMD4 carries
 * a Space Saving IPE, and it carries exactly one, so these would otherwise cost
 * their size twenty times over for one product that uses them. The elements
 * shared with a full ticket - price, issue date, class, travellers, passback -
 * go in the product's @c ticket as usual, and the place a TYP 29 was last used
 * in its @c from.
 */
typedef struct {
    uint8_t flags; /**< ITSO_SS_* pass flags. */
    bool euro; /**< Sterling/Euro flag: the currency of every amount. */
    uint8_t area_kind; /**< ItsoAreaKind. */
    uint32_t area_value; /**< Fare code, fare value, or the location's LocDefType. */
    /** An ItsoAreaLocation's origin, destination and via, as TS 1000-1 lays out
     *  a LOC4 (TYP 27's GeoValidity); a LOC3 (TYP 28 and 29) has no via. */
    ItsoLocation area[3];

    /* TYP 29's ScaledQtyBackup (tables 55, 55a and 58b): one-time-programmable
     * bits a POST sets one per @c backup_step rides or coupons used, from which
     * it can rebuild a torn QtyRemaining. */
    bool has_backup; /**< IPEBitMap bit 3 set and a non-zero ScalingFactor. */
    uint16_t backup_step; /**< m, the rides each bit stands for. */
    uint16_t backup_count; /**< What the backup says is left: m times the bits unset. */

    bool has_last_use; /**< The type carries a LastUseDTS (TYP 27, 28, 29 rev 2). */
    uint32_t last_use_dts; /**< Raw DTS of the last use; 0 is never used. */
    bool has_events; /**< TYP 27: Event1 and Event2 are present. */
    uint8_t event1; /**< EN1545 EventTypeCode. */
    uint8_t event2;

    /* TYP 29 revision 1: whether UsageRec is where the holder got on or got off
     * (table 58). The place is in the product's @c from. */
    bool usage_alighted;

    /* TYP 29 revision 2, multi-leg journeys (table 55a). */
    uint32_t journey_start_dts; /**< JnyComDTS: when the latest journey began. */
    uint8_t transfers; /**< TransferCounter: changes made on that journey. */
    uint8_t daily_journeys; /**< DailyJnyCounter: journeys begun that day. */
    uint8_t max_daily_journeys; /**< MaxDailyJourneys. */

    /* TYP 28, a carnet of day passes (clause 2.15.2): each tick is the day a
     * pass was used, as days before the directory expiry. 0 is a pass not yet
     * used and 31 one never sold; the two flags stand for passes on the first
     * and last days, which spend no tick. */
    uint8_t carnet_ticks[6];
    bool carnet_issue_day; /**< NDoIE: a pass is valid on the day of issue. */
    bool carnet_expiry_day; /**< NDoEE: a pass is valid on the day of expiry. */
} ItsoSpaceSaving;

#ifdef __cplusplus
}
#endif
