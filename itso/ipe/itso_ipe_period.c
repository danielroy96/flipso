/**
 * @file itso_ipe_period.c
 * @brief TYP 22: the pre-defined area-based period ticket (TS 1000-5 clause 2.9).
 */
#include "itso_ipe_i.h"

#include <string.h>

/**
 * TYP 22: pre-defined area-based ticket, all three format revisions.
 *
 * Tables 27, 27a and 3.27 agree on everything up to byte 13: flags, passback,
 * issue date, expiry time, class. After that each revision moves things - a
 * DTS start in revisions 1 and 2 against a date and a time in 3, a two-byte
 * AmountPaid in revision 1 against four bytes later - and the optional elements
 * come in a different order in each.
 */
void itso_ipe_period_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    const uint8_t bitmap = product->bitmap;
    const uint8_t format_rev = product->format_rev;
    ItsoTicketTerms* t = &product->terms.ticket;
    /* Where the mandatory part ends, which is where the optional elements start. */
    const size_t fixed = format_rev >= 3 ? 29 : format_rev == 2 ? 28 : 26;
    if(format_rev == 0 || len < fixed) return;

    t->flags = (uint16_t)itso_bits(data, 40, 16);
    product->print_defined = ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT;
    if(t->flags & ITSO_T22_PRINT_TICKET) product->print_flags |= ITSO_PRINT_TICKET;
    if(t->flags & ITSO_T22_PRINT_RECEIPT) product->print_flags |= ITSO_PRINT_RECEIPT;
    product->passback = (uint8_t)itso_bits(data, 58, 6);
    product->has_passback = true;
    t->issue_date = (ItsoDate)itso_bits(data, 64, 14);
    t->expiry_time = (uint16_t)itso_bits(data, 78, 11);
    t->renew_quantity = (uint8_t)itso_bits(data, 90, 6);
    t->travel_class = (uint8_t)itso_bits(data, 96, 3);
    t->validity_code = (uint8_t)itso_bits(data, 99, 5);

    /* Everything from ValidityStartDTS on sits one byte later in revision 3,
     * whose start date and time take four bytes where the DTS took three. */
    size_t b; /* Byte of PromotionCode. */
    if(format_rev >= 3) {
        product->start = (ItsoDate)itso_bits(data, 106, 14); /* ValidityStartDate. */
        product->has_start = true;
        t->start_time = (uint16_t)itso_bits(data, 125, 11);
        t->has_start_time = true;
        b = 17;
    } else {
        t->valid_from_dts = itso_bits(data, 104, 24);
        b = 16;
    }
    t->promotion_code = data[b];
    t->valid_days = data[b + 1];
    t->adults = data[b + 2];
    t->children = data[b + 3];
    t->concessions = data[b + 4];

    /* Zero in both the amount and its currency is the documented "not used",
     * and a real zero fare is not worth a line either. */
    const uint8_t valc = data[b + 5] & 0x0F;
    int32_t paid;
    size_t mop_byte;
    if(format_rev == 1) {
        paid = itso_uint16(data + 22);
        mop_byte = 24;
    } else {
        paid = (int32_t)itso_bits(data, (uint32_t)(b + 6) * 8, 32);
        mop_byte = b + 10;
    }
    if(paid) itso_decode_money(paid, valc, &t->amount_paid);
    t->paid_mop = data[mop_byte] >> 4;
    t->vat = (uint16_t)itso_bits(data, (uint32_t)mop_byte * 8 + 4, 12);
    t->valid = true;

    size_t pos = fixed;
    if(bitmap & (1 << 4)) {
        if(pos + 2 > len) return;
        product->cpicc = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        product->has_cpicc = true;
        pos += 2;
    }

    if(format_rev == 1) {
        /* Revision 1 flags the two locations independently and puts PassDuration
         * after them, so it cannot be found without walking both. */
        if(bitmap & (1 << 1)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
        }
        if(bitmap & (1 << 2)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
        }
        if((bitmap & (1 << 3)) && pos < len) {
            t->pass_duration = data[pos];
            t->has_pass_duration = true;
        }
        return;
    }

    if(bitmap & (1 << 3)) {
        if(format_rev == 2) {
            if(pos + 1 > len) return;
            t->pass_duration = data[pos];
            pos += 1;
        } else {
            /* Table 3.27: a unit code, a 12-bit count of it, then the days the
             * stock of passes is extended by on auto-renew. */
            if(pos + 4 > len) return;
            t->duration_unit = data[pos] >> 4;
            t->pass_duration = (uint16_t)itso_bits(data, (uint32_t)pos * 8 + 4, 12);
            t->stock_duration = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
            t->has_stock_duration = true;
            pos += 4;
        }
        t->has_pass_duration = true;
    }

    if(bitmap & (1 << 1)) {
        pos = itso_parse_route(t, data, len, pos);
        if(!pos) return;
        pos = itso_parse_journey_ends(product, data, len, pos);
        if(!pos) return;
    }

    /* Revision 3 bit 2: the identity document the holder must carry with the
     * ticket, after everything else (table 3.27). */
    if(format_rev >= 3 && (bitmap & (1 << 2)) && pos < len) {
        t->id_doc_type = (uint8_t)itso_bits(data, (uint32_t)pos * 8, 3);
        t->id_doc_len = (uint8_t)itso_bits(data, (uint32_t)pos * 8 + 3, 5);
        pos++;
        if(t->id_doc_len == 0 || pos + t->id_doc_len > len) return;
        uint8_t kept = t->id_doc_len < ITSO_ID_DOC_LEN ? t->id_doc_len : ITSO_ID_DOC_LEN;
        memcpy(t->id_doc, data + pos, kept);
        t->has_id_doc = true;
    }
}

/** TYP 22: the stock of passes and the two expiries it keeps. */
void itso_ipe_period_value(ItsoProduct* product, const uint8_t* newest) {
    ItsoTicketTerms* ticket = &product->terms.ticket;
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 tables 29, 29a and 3.29, which agree across all three
     * revisions. A period ticket keeps a stock of unactivated passes, and
     * two expiry dates: one for the stock, one for the pass in use. */
    product->count_kind = ItsoCountPasses;
    product->count = live->count;
    uint8_t flags = (uint8_t)itso_bits(newest, 86, 6);
    product->auto_renew = (flags & 0x01) != 0;
    /* Clear, the ticket is one continuous period and AutoRenewQuantity1
     * counts days rather than passes (rules 5 and 6 of 2.9.1.4). */
    ticket->stored_passes = (flags & 0x02) != 0;
    ticket->stored_expiry = (ItsoDate)itso_bits(newest, 92, 14);
    ticket->has_stored_expiry = true;
    ticket->current_expiry = (ItsoDate)itso_bits(newest, 106, 14);
    ticket->has_current_expiry = true;
}
