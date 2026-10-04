/**
 * @file itso_ipe_journey.c
 * @brief TYP 23: the pre-defined specific journey ticket (TS 1000-5 clause 2.10).
 */
#include "itso_ipe_i.h"

/**
 * The terms of a TYP 23 journey ticket: tables 31, 31a and 31b.
 *
 * The same elements as a period ticket's, less the day filters, plus a
 * photocard number and the optional mode group. Revision 2 widened AmountPaid to
 * four bytes; revision 3 added a ValidityStartDTS after IssueDate, which pushes
 * everything from ValidityCode on three bytes later, and widened the ride value.
 */
static void itso_parse_journey_terms(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    ItsoTicketTerms* t = &product->terms.ticket;
    const size_t fixed = format_rev >= 3 ? 33 : format_rev == 2 ? 29 : 27;
    if(format_rev == 0 || len < fixed) return;

    if(data[5] & 0x02) t->ticket_used = true; /* TYP23Flags UsedChecked. */
    product->print_defined = ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT;
    if(data[5] & 0x20) product->print_flags |= ITSO_PRINT_TICKET;
    if(data[5] & 0x40) product->print_flags |= ITSO_PRINT_RECEIPT;
    product->passback = (uint8_t)itso_bits(data, 50, 6);
    product->has_passback = true;
    t->issue_date = (ItsoDate)itso_bits(data, 58, 14);

    /* Everything from ValidityCode on moves three bytes in revision 3. */
    const uint32_t shift = format_rev >= 3 ? 24 : 0;
    if(format_rev >= 3) t->valid_from_dts = itso_bits(data, 72, 24);
    t->validity_code = (uint8_t)itso_bits(data, 72 + shift, 5);
    t->expiry_time = (uint16_t)itso_bits(data, 77 + shift, 11);
    t->travel_class = (uint8_t)itso_bits(data, 93 + shift, 3);

    const size_t b = 12 + shift / 8; /* PartySizeAdult. */
    t->adults = data[b];
    t->children = data[b + 1];
    t->concessions = data[b + 2];
    const uint8_t valc = data[b + 3] & 0x0F;

    int32_t paid;
    size_t mop_byte;
    if(format_rev == 1) {
        paid = itso_uint16(data + 16);
        mop_byte = 18;
    } else {
        paid = (int32_t)itso_bits(data, (uint32_t)(b + 4) * 8, 32);
        mop_byte = b + 8;
    }
    if(paid) itso_decode_money(paid, valc, &t->amount_paid);
    t->paid_mop = data[mop_byte] >> 4;
    t->vat = (uint16_t)itso_bits(data, (uint32_t)mop_byte * 8 + 4, 12);
    t->photocard = itso_bits(data, (uint32_t)(mop_byte + 2) * 8, 32);
    t->promotion_code = data[mop_byte + 6];
    product->cpicc = (uint16_t)((data[mop_byte + 7] << 8) | data[mop_byte + 8]);
    product->has_cpicc = product->cpicc != 0;
    if(format_rev >= 3) t->renew_quantity = data[32];
    t->valid = true;

    /* Bit 3 of every revision: mode, transfer limit, time limit, and the value
     * of a ride in a currency of its own, the low nibble of the group's last
     * byte - six bytes in all, eight once revision 3 widened the value. */
    const size_t group = format_rev >= 3 ? 8 : 6;
    if((bitmap & (1 << 3)) && fixed + group <= len) {
        t->mode = data[fixed] & 0x0F;
        t->max_transfers = data[fixed + 1];
        t->time_limit = data[fixed + 2];
        int32_t ride = format_rev >= 3 ? (int32_t)itso_bits(data, (uint32_t)(fixed + 3) * 8, 32) :
                                         itso_uint16(data + fixed + 3);
        if(ride) itso_decode_money(ride, data[fixed + group - 1] & 0x0F, &t->ride_value);
        t->has_mode_group = true;
    }
}

/**
 * TYP 23: pre-defined specific journey ticket, all three format revisions.
 *
 * The three revisions carry the same elements in the same order; what moves is
 * where they start. Revision 2 added RouteCode and six bytes of mandatory
 * fields over revision 1, and revision 3 added AutoRenewQuantity and widened
 * ValueOfRideJourney from two bytes to four.
 */
void itso_ipe_journey_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    const uint8_t bitmap = product->bitmap;
    const uint8_t format_rev = product->format_rev;
    size_t pos;

    itso_parse_journey_terms(product, data, len, bitmap, format_rev);

    if(format_rev >= 3) {
        /* TS 1000-5 table 31b: AutoRenewQuantity ends the mandatory part. */
        pos = 33;
        if(bitmap & (1 << 3)) pos += 8; /* Mode, transfers, time limit, ride value. */
    } else if(format_rev == 2) {
        /* TS 1000-5 table 31a. */
        pos = 29;
        if(bitmap & (1 << 3)) pos += 6;
    } else if(format_rev == 1) {
        /* TS 1000-5 table 31. Revision 1 has no RouteCode and flags the two
         * locations independently - bit 2 the origin, bit 1 the destination -
         * where later revisions gate both on bit 1 together with RouteCode. */
        pos = 27;
        if(bitmap & (1 << 3)) pos += 6;

        if(bitmap & (1 << 2)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
        }
        if(bitmap & (1 << 1)) {
            if(pos >= len) return;
            itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
        }
        return;
    } else {
        return; /* Revision 0 is not defined; guessing an offset would invent a station. */
    }

    if(!(bitmap & (1 << 1))) return;
    pos = itso_parse_route(&product->terms.ticket, data, len, pos);
    if(pos) itso_parse_journey_ends(product, data, len, pos);
}

/** TYP 23: rides left, transfers, and whether the ticket has been used. */
void itso_ipe_journey_value(ItsoProduct* product, const uint8_t* newest) {
    ItsoTicketTerms* ticket = &product->terms.ticket;
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 tables 33, 33a and 33b agree on the first three bytes: a
     * journey ticket counts rides rather than money. Reading these bytes as a
     * purse would render the ride count and the transfer count as one
     * 16-bit amount, and take a currency from a flags byte. */
    product->count_kind = ItsoCountRides;
    product->count = live->count;
    ticket->transfers = newest[11];
    ticket->has_transfers = true;
    product->auto_renew = (newest[12] & 0x01) != 0;
    ticket->ticket_used = (newest[12] & 0x02) != 0; /* TYP23ValueFlags bit 1. */
    if(product->format_rev >= 3) {
        /* Table 33b added an expiry for the unactivated rides; in revisions 1
         * and 2 these bytes are RFU and reading them would invent a date. */
        ticket->stored_expiry = (ItsoDate)itso_bits(newest, 106, 14);
        ticket->has_stored_expiry = true;
    }
}
