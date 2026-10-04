/**
 * @file itso_capping.h
 * @brief The Complex Capping Value Group Extension (TS 1000-5 clause 4.1).
 */
#pragma once

#include "../itso_location.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ITSO_CAP_ACCUMULATORS 4

/** CapAccumulatorRule (TS 1000-5 tables AD1 and AD2). */
typedef enum {
    ItsoCapRuleNone = 0,
    ItsoCapRuleDay = 1, /**< Day capping only. */
    ItsoCapRuleShortPeriod = 2, /**< Accumulate for n days, n set by the strategy. */
    ItsoCapRuleLongPeriod = 3, /**< Accumulate for m days, m set by the strategy. */
} ItsoCapRule;

/** One of the four accumulator sets of a Complex Capping extension. */
typedef struct {
    ItsoMoney uncapped; /**< What the fares would have come to without a cap. */
    ItsoMoney day; /**< Spent towards today's cap. */
    ItsoMoney multiday; /**< Spent towards the multi-day cap. */
    ItsoMoney last_fare; /**< LastFarePaid; VGXRef 2 only. */
    ItsoDts cap_dts; /**< When the last cap was applied; VGXRef 2 only, 0 if never. */
    uint16_t day_count; /**< Days into a multi-day accumulation; 0 for single day. */
    ItsoCapRule rule; /**< A code the tables leave RFU is kept as it stands. */
    uint8_t last_txn; /**< EventTypeCode of the last fare paid. */
    ItsoLocation location; /**< Where the last cap was applied, or zones used. */
} ItsoCapAccumulator;

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

#ifdef __cplusplus
}
#endif
