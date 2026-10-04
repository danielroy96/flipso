/**
 * @file itso_ipe_purse.c
 * @brief TYP 2, 4 and 5: the pay-as-you-go purse and the charge-to-account products
 * (TS 1000-5 tables 2, 10 and 15).
 */
#include "itso_ipe_i.h"

/**
 * TYP 2, 4 and 5: the purse and charge-to-account datasets.
 *
 * The three share a shape - flags, then limits, then a deposit and a validity
 * start - but not a set of offsets, so each is laid out separately rather than
 * parameterised into something that reads as though the spec were tidier than
 * it is. TS 1000-5 tables 2, 10 and 15.
 */
void itso_ipe_purse_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    ItsoPurseTerms* purse = &product->terms.purse;
    /* The currency for every amount in the dataset but the deposit is the value
     * record's ValueCurrencyCode, which the value record parser has already
     * read - the whole nibble, since its scaling bits apply to a limit as much
     * as to the balance. */
    uint8_t currency = product->value_parsed ? product->value_valc : 0;

    /* TYP2Flags, TYP4Flags and TYP5Flags define only these two (tables 5, 13
     * and 18). */
    if(len >= 6) {
        product->print_defined = ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT;
        if(data[5] & 0x20) product->print_flags |= ITSO_PRINT_TICKET;
        if(data[5] & 0x40) product->print_flags |= ITSO_PRINT_RECEIPT;
    }

    switch(product->typ) {
    case ItsoTypStoredTravelRights:
        if(len < 22) return;
        itso_decode_money(itso_uint16(data + 6), currency, &purse->top_up_threshold);
        itso_decode_money(itso_uint16(data + 8), currency, &purse->top_up_amount);
        purse->has_top_up = purse->top_up_amount.value != 0;
        itso_decode_money(itso_uint16(data + 10), currency, &purse->max_value);
        itso_decode_money(itso_uint16(data + 12), currency, &purse->max_negative);
        purse->has_limits = true;
        itso_decode_money(itso_uint16(data + 14), (data[20] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[19] & 0x0F;
        product->deposit_vat = (uint16_t)itso_bits(data, 164, 12);
        product->has_deposit = product->deposit.value != 0;
        /* StartDateAutoTopUp: a DATE at byte 16, followed by RFU to 19.5. */
        product->start = (uint16_t)itso_bits(data, 128, 14);
        product->has_start = product->start != 0;
        break;

    case ItsoTypChargeToAccount1:
        if(len < 16) return;
        itso_decode_money(itso_uint16(data + 6), currency, &purse->max_value);
        purse->has_limits = true;
        itso_decode_money(itso_uint16(data + 8), (data[14] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[13] & 0x0F;
        product->deposit_vat = (uint16_t)itso_bits(data, 116, 12); /* At byte 14.5. */
        product->has_deposit = product->deposit.value != 0;
        product->start = (uint16_t)itso_bits(data, 80, 14); /* StartDateCTA at byte 10. */
        product->has_start = product->start != 0;
        purse->end_date = (uint16_t)itso_bits(data, 94, 14); /* EndDate at byte 11.75. */
        purse->has_end_date = true;
        break;

    case ItsoTypChargeToAccount2:
        if(len < 18) return;
        purse->weeks_per_period = data[6];
        purse->max_transactions = data[7];
        purse->has_charge_period = true;
        itso_decode_money(itso_uint16(data + 8), currency, &purse->max_value);
        purse->has_limits = true;
        itso_decode_money(itso_uint16(data + 10), (data[16] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[15] & 0x0F;
        product->deposit_vat = (uint16_t)itso_bits(data, 132, 12); /* At byte 16.5. */
        product->has_deposit = product->deposit.value != 0;
        product->start = (uint16_t)itso_bits(data, 96, 14); /* StartDateCTA at byte 12. */
        product->has_start = product->start != 0;
        purse->end_date = (uint16_t)itso_bits(data, 110, 14); /* EndDate at byte 13.75. */
        purse->has_end_date = true;
        break;

    default:
        break;
    }
}

/** TYP 2: the balance, the journey in progress and the top-up flags. */
void itso_ipe_stored_travel_rights_value(ItsoProduct* product, const uint8_t* newest) {
    ItsoPurseTerms* purse = &product->terms.purse;
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 table 4. */
    purse->balance = live->amount;
    product->value_valc = newest[12] >> 4;
    purse->journey_legs = newest[12] & 0x0F;
    itso_decode_money(
        (int32_t)itso_bits(newest, 104, 13), (newest[12] >> 4) & 0x0F, &purse->cumulative_fare);
    purse->has_journey = true;
    /* TYP2ValueFlags is three bits wide; flag n is bit n of it. */
    uint8_t flags = (uint8_t)itso_bits(newest, 117, 3);
    purse->auto_top_up = (flags & 0x01) != 0;
    purse->priority_override = (flags & 0x02) != 0;
    purse->auto_top_up_internal = (flags & 0x04) != 0;
}

/** TYP 4: spend to date, read where TYP 2 keeps its balance. */
void itso_ipe_charge_to_account1_value(ItsoProduct* product, const uint8_t* newest) {
    ItsoPurseTerms* purse = &product->terms.purse;
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 table 12. The layout matches TYP 2 exactly; what differs is
     * the meaning, so the same bytes are read and flagged as spend. */
    purse->balance = live->amount;
    product->value_valc = newest[12] >> 4;
    purse->balance_is_spend = true;
    purse->journey_legs = newest[12] & 0x0F;
    itso_decode_money(
        (int32_t)itso_bits(newest, 104, 12), (newest[12] >> 4) & 0x0F, &purse->cumulative_fare);
    purse->has_journey = true;
    purse->priority_override = (itso_bits(newest, 116, 4) & 0x02) != 0;
}

/** TYP 5: transactions this charge period, and when the count was cleared. */
void itso_ipe_charge_to_account2_value(ItsoProduct* product, const uint8_t* newest) {
    ItsoPurseTerms* purse = &product->terms.purse;
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 table 17: a count of transactions in the charge period, and
     * the date that count was last cleared. */
    product->count_kind = ItsoCountTransactions;
    product->count = live->count;
    purse->last_reset = (uint16_t)itso_bits(newest, 90, 14);
    purse->has_last_reset = true;
    /* ValueCurrencyCode at offset 15 prices MaxValue5, though the record
     * itself holds no money; TYP5ValueFlags beside it has the priority bit
     * that TYP 4 keeps at the end of its record (table 19). */
    product->value_valc = newest[13] >> 4;
    purse->priority_override = (newest[13] & 0x02) != 0;
    purse->journey_legs = newest[14] & 0x0F;
    purse->has_journey = true;
}
