/**
 * @file itso_ipe_loyalty.c
 * @brief TYP 3: the loyalty scheme (TS 1000-5 table 9), which keeps everything in its
 * value record.
 */
#include "itso_ipe_i.h"

/** TYP 3: a points balance, and the bytes the scheme keeps for itself. */
void itso_ipe_loyalty_value(ItsoProduct* product, const uint8_t* newest) {
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 table 9: points rather than money, and three bytes of them. */
    product->count_kind = ItsoCountPoints;
    product->count = live->count;
    product->owner_data = (uint16_t)((newest[13] << 8) | newest[14]);
    product->has_owner_data = true;
}
