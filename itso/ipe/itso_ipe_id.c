/**
 * @file itso_ipe_id.c
 * @brief TYP 14 and 16: the entitlement and the ITSO ID (TS 1000-5 tables 20 and 22).
 */
#include "itso_ipe_i.h"

/**
 * Shared tail of the TYP 14 and TYP 16 datasets: optional identity and location
 * elements, laid out back to back in bitmap order.
 *
 * @param pos     offset of the first optional element.
 * @param names   true for TYP 16, which carries the holder's name.
 */
static void itso_parse_id_optionals(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    size_t pos,
    uint8_t bitmap,
    bool names) {
    ItsoIdTerms* id = &product->terms.id;
    uint8_t bit = 1;

    if(bitmap & (1 << bit)) {
        if(pos + 4 > len) return;
        id->secondary_holder_id = itso_bits(data + pos, 0, 32);
        id->has_secondary_holder = true;
        pos += 4;
    }
    bit++;

    if(names) {
        if(bitmap & (1 << bit)) {
            /* Forename and surname are each a length byte followed by that many
             * ASCII bytes: TS 1000-5 says the elements are compressed to size. */
            if(pos >= len) return;
            uint8_t forename_len = data[pos++];
            if(pos + forename_len > len) return;
            size_t written = itso_copy_name(data + pos, forename_len, id->name, ITSO_NAME_LEN);
            pos += forename_len;

            if(pos >= len) return;
            uint8_t surname_len = data[pos++];
            if(pos + surname_len > len) return;
            if(written + 1 < ITSO_NAME_LEN) {
                if(written > 0) id->name[written++] = ' ';
                itso_copy_name(
                    data + pos, surname_len, id->name + written, ITSO_NAME_LEN - written);
            }
            pos += surname_len;

            id->has_name = id->name[0] != '\0';
        }
        bit++;
    }

    if(bitmap & (1 << bit)) {
        if(pos + 2 > len) return;
        id->half_days = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        id->has_half_days = true;
        pos += 2;
        if(pos >= len) return;
        pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    }
    bit++;

    if(bitmap & (1 << bit)) {
        if(pos >= len) return;
        itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
    }
}

/** TYP 16 (ITSO ID) and TYP 14 (Entitlement): holder identity and entitlement. */
void itso_ipe_id_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    ItsoIdTerms* id = &product->terms.id;
    const uint8_t bitmap = product->bitmap;
    const uint8_t format_rev = product->format_rev;
    size_t optionals;
    uint32_t start_bit = 0;
    uint32_t expiry_bit;
    size_t entitlement_offset;

    if(product->typ == ItsoTypId) {
        if(format_rev >= 2) {
            /* TS 1000-5 table 22a. */
            start_bit = 130; /* EntitlementStartDate at byte 16.25. */
            expiry_bit = 144; /* EntitlementExpiryDate at byte 18. */
            entitlement_offset = 29;
            optionals = 31;
        } else {
            /* TS 1000-5 table 22. */
            expiry_bit = 130; /* EntitlementExpiryDate at byte 16.25. */
            entitlement_offset = 27;
            optionals = 29;
        }
    } else {
        if(format_rev >= 2) {
            /* TS 1000-5 table 20a. */
            start_bit = 90; /* EntitlementStartDate at byte 11.25. */
            expiry_bit = 104; /* EntitlementExpiryDate at byte 13. */
            entitlement_offset = 20;
            optionals = 22;
        } else {
            /* TS 1000-5 table 20. */
            expiry_bit = 90;
            entitlement_offset = 18;
            optionals = 20;
        }
    }

    /* Fixed elements both revisions of both types share (tables 20 and 22).
     * IDFlags carries the things a concessionary pass is actually judged on at
     * the gate: whether the card is photo-personalised, and whether a companion
     * travels free with the holder. */
    if(len >= 7) {
        id->id_flags = data[5];
        id->has_id_flags = true;
        /* IDFlags bit 5, PrintTicket; an ID has no PrintReceipt (table 24). */
        product->print_defined = ITSO_PRINT_TICKET;
        if(data[5] & 0x20) product->print_flags |= ITSO_PRINT_TICKET;
        /* PassbackTime is six bits, two bits into byte 6. */
        product->passback = (uint8_t)itso_bits(data, 50, 6);
        product->has_passback = true;
    }

    /* DateOfBirth is only on the ID type; TYP 14 puts HolderID at byte 7. It is
     * a Datef (BCD yyyymmdd), not a DATE, so it can predate the DATE epoch. */
    if(product->typ == ItsoTypId && len >= 11) {
        char digits[9];
        itso_bcd(data, 56, 8, digits);
        bool numeric = true;
        for(uint8_t i = 0; i < 8; i++) {
            if(digits[i] == 'F') numeric = false;
        }
        /* An all-zero Datef is the documented "explicitly no date". */
        if(numeric && !itso_is_blank(data + 7, 4)) {
            id->dob_year = (uint16_t)((digits[0] - '0') * 1000 + (digits[1] - '0') * 100 +
                                      (digits[2] - '0') * 10 + (digits[3] - '0'));
            id->dob_month = (uint8_t)((digits[4] - '0') * 10 + (digits[5] - '0'));
            id->dob_day = (uint8_t)((digits[6] - '0') * 10 + (digits[7] - '0'));
            id->has_dob = id->dob_month >= 1 && id->dob_month <= 12 && id->dob_day >= 1 &&
                          id->dob_day <= 31;
        }
    }

    if(entitlement_offset + 2 > len) return;

    if(start_bit) {
        product->start = (ItsoDate)itso_bits(data, start_bit, 14);
        product->has_start = true;
    }
    id->sub_expiry = (ItsoDate)itso_bits(data, expiry_bit, 14);
    id->has_sub_expiry = true;

    id->entitlement_code = data[entitlement_offset];
    id->concession_class = data[entitlement_offset + 1];
    id->has_entitlement = true;

    /* Both types put CPICC where others put ProductRetailer, and both carry a
     * HolderID and the three rounding flags - the ID after its date of birth
     * and language, the entitlement straight after PassbackTime. */
    product->cpicc = (uint16_t)((data[3] << 8) | data[4]);
    product->has_cpicc = product->cpicc != 0;
    const size_t holder = product->typ == ItsoTypId ? 12 : 7;
    id->holder_id = itso_bits(data, (uint32_t)holder * 8, 32);
    id->has_holder_id = id->holder_id != 0;
    const uint8_t rounding = data[holder + 4]; /* RoundingFlag, RoundingValueFlag. */
    if(data[6] & 0x80) id->rounding |= ITSO_ROUNDING_ENABLED;
    if(rounding & 0x80) id->rounding |= ITSO_ROUNDING_FLAG;
    if(rounding & 0x40) id->rounding |= ITSO_ROUNDING_VALUE;

    if(product->typ == ItsoTypEntitlement) {
        /* Tables 20 and 20a: one deposit, its currency nibble ahead of the
         * payment method rather than after the VAT as on an ID. Revision 2's
         * EntitlementStartDate moves it along by two. */
        const size_t d = format_rev >= 2 ? 15 : 13; /* DepositCurrencyCode, low nibble. */
        product->deposit_mop = data[d + 1] >> 4;
        product->deposit_vat = (uint16_t)itso_bits(data, (uint32_t)(d + 1) * 8 + 4, 12);
        itso_decode_money(itso_uint16(data + d + 3), data[d] & 0x0F, &product->deposit);
        product->has_deposit = product->deposit.value != 0;
    } else {
        /* Tables 22 and 22a agree up to HolderID; revision 2 then inserts the
         * two-byte EntitlementStartDate, pushing both deposits along by two. */
        id->language = data[11];

        const size_t d = format_rev >= 2 ? 20 : 18; /* DepositMethodOfPayment. */
        const uint8_t valcs = data[d + 4];
        product->deposit_mop = data[d] >> 4;
        product->deposit_vat = (uint16_t)itso_bits(data, (uint32_t)d * 8 + 4, 12);
        id->shell_deposit_mop = data[d + 2] >> 4;
        id->shell_deposit_vat = (uint16_t)itso_bits(data, (uint32_t)(d + 2) * 8 + 4, 12);
        itso_decode_money(itso_uint16(data + d + 5), valcs >> 4, &product->deposit);
        itso_decode_money(itso_uint16(data + d + 7), valcs & 0x0F, &id->shell_deposit);
        product->has_deposit = product->deposit.value != 0;
        id->has_shell_deposit = id->shell_deposit.value != 0;
    }

    itso_parse_id_optionals(product, data, len, optionals, bitmap, product->typ == ItsoTypId);
}
