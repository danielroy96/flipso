/**
 * @file itso_ipe_voucher.c
 * @brief TYP 25 and 26: the voucher and the tolling product (TS 1000-5 clauses 2.12 and
 * 2.13), which keep everything in their value records.
 */
#include "itso_ipe_i.h"

/** TYP 25 and 26: rides or tickets left. */
void itso_ipe_voucher_value(ItsoProduct* product, const uint8_t* newest) {
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 tables 38 and 42, which are identical. */
    product->count_kind = ItsoCountRides;
    product->count = live->count;
    product->auto_renew = (newest[11] & 0x01) != 0;
}
