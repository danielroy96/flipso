/**
 * @file itso_ipe_voucher.c
 * @brief TYP 25, the voucher (TS 1000-5 clause 2.12), and the value record it
 * shares with TYP 26, the toll pass (itso_ipe_tolling.c).
 */
#include "itso_ipe_i.h"

/**
 * TYP 25, Travel Related Voucher: TS 1000-5 table 36, IPE Format Revision 1,
 * the only one defined. Offsets are from the start of the IPE data group.
 *
 * The same shape as a journey ticket's (table 31b) as far as ValidityStartDTS,
 * then what the voucher is for and worth in place of who travels on it.
 */
void itso_ipe_voucher_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    if(product->typ != ItsoTypVoucher || product->format_rev != 1) return;
    /* The mandatory part ends with UserDefined at byte 22. */
    if(len < 23) return;
    ItsoTicketTerms* t = &product->terms.ticket;

    /* TYP25Flags, table 39: bits 5 and 6 are the print flags, the rest RFU. */
    product->print_defined = ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT;
    if(data[5] & 0x20) product->print_flags |= ITSO_PRINT_TICKET;
    if(data[5] & 0x40) product->print_flags |= ITSO_PRINT_RECEIPT;
    product->passback = (uint8_t)itso_bits(data, 50, 6);
    product->has_passback = true;
    t->issue_date = (ItsoDate)itso_bits(data, 58, 14);
    t->valid_from_dts = itso_bits(data, 72, 24);
    t->expiry_time = (uint16_t)itso_bits(data, 101, 11);
    t->service_id = data[14];

    /* MaxValue25 and AmountPaid are priced in currencies of their own, the
     * two nibbles of byte 17. A zero AmountPaid is the spec's "not used". */
    int32_t max_value = itso_uint16(data + 15);
    if(max_value) itso_decode_money(max_value, data[17] >> 4, &t->unit_value);
    int32_t paid = itso_uint16(data + 18);
    if(paid) itso_decode_money(paid, data[17] & 0x0F, &t->amount_paid);
    t->paid_mop = data[20] >> 4;
    t->vat = (uint16_t)itso_bits(data, 164, 12);
    t->user_defined = data[22];

    /* Bitmap bit 1, table 37: AutoRenewQuantity2, uses added per renewal. */
    if((product->bitmap & (1 << 1)) && len >= 24) t->renew_quantity = data[23];
    t->valid = true;
}

/** TYP 25 and 26: uses or rides left. */
void itso_ipe_voucher_value(ItsoProduct* product, const uint8_t* newest) {
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 tables 38 and 42, which are identical. */
    product->count_kind = product->typ == ItsoTypVoucher ? ItsoCountUses : ItsoCountCrossings;
    product->count = live->count;
    product->auto_renew = (newest[11] & 0x01) != 0;
}
