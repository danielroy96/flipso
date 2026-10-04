/**
 * @file flipso_product_id.c
 * @brief An ITSO ID or entitlement (TYP 16 and 14): the holder, the terms, the deposits.
 */
#include "flipso_product_i.h"

/** A deposit, how it was paid, and whether it comes back. */
static void flipso_cat_deposit(
    FuriString* out,
    const char* label,
    const ItsoMoney* amount,
    uint8_t mop,
    uint16_t vat,
    bool refundable) {
    flipso_cat_money(out, "", label, amount);
    if(mop) furi_string_cat_printf(out, "  Paid by: %s\n", itso_payment_name(mop));
    flipso_cat_vat(out, "  ", vat);
    furi_string_cat(
        out, refundable ? "  Refundable: Yes\n" : "  Refundable: If the operator agrees\n");
}

/**
 * The parts of an ITSO ID or entitlement beyond name and entitlement: the
 * language, which is about the holder, and the valid periods and fare
 * rounding, which are the terms. An entitlement (TYP 14) carries all of these
 * but the language. Its deposits are flipso_cat_id_deposits()', and its issuer
 * and holder numbers are under Technical, with the other numbers nothing names.
 */
void flipso_cat_id_details(FlipsoPages* p, const ItsoProduct* product) {
    const ItsoIdTerms* id = itso_product_id(product);
    if(!flipso_product_is_identity(product) || !product->body_parsed) return;
    FuriString* holder = flipso_pages_at(p, FlipsoSlotLeft);
    FuriString* rules = flipso_pages_at(p, FlipsoSlotRules);

    char code[3];
    if(itso_language_code(id->language, code)) {
        const char* name = itso_language_name(id->language);
        if(name) {
            furi_string_cat_printf(holder, "Language: %s\n", name);
        } else {
            /* ISO 639-1, upper cased so it reads as a code rather than a word. */
            furi_string_cat_printf(holder, "Language: %c%c\n", code[0] - 32, code[1] - 32);
        }
        /* IDFlags bit 3 points a POST at another application on the card, and
         * then Language "shall be ignored" (TS 1000-5 table 22). */
        if(id->id_flags & 0x08) furi_string_cat(holder, "  In use: No\n");
    }
    /* IDFlags bits 3, 6 and 7 (TS 1000-5 table 24); bit 5 is shown with the
     * other print flags, under Technical. */
    if(id->has_id_flags && (id->id_flags & 0x08)) {
        furi_string_cat(holder, "More details: In another app on the card\n");
    }

    /* HalfDayOfWeek: two network-defined periods per day (annex A.10). A zero
     * mask selects nothing, which on a real card means the element is unused. */
    if(id->has_half_days && id->half_days) {
        char days[40];
        uint8_t mask = itso_half_days_mask(id->half_days);
        itso_format_days(mask, days, sizeof(days));
        furi_string_cat_printf(rules, "Valid days: %s\n", days);
        static const char* const names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        for(int day = 0; day < 7; day++) {
            uint8_t pair = (id->half_days >> (14 - 2 * day)) & 0x03;
            if(pair == 0x02)
                furi_string_cat_printf(rules, "  %s: First period only\n", names[day]);
            if(pair == 0x01)
                furi_string_cat_printf(rules, "  %s: Second period only\n", names[day]);
        }
        if(mask & ITSO_DOW_SPECIAL) flipso_cat_flag(rules, "  ", "Special days", true);
    }

    /* How a machine rounds a half or proportional fare for this holder. */
    if(id->rounding & ITSO_ROUNDING_ENABLED) {
        furi_string_cat_printf(
            rules,
            "Fare rounding: %s to %s\n",
            (id->rounding & ITSO_ROUNDING_FLAG) ? "Up" : "Down",
            (id->rounding & ITSO_ROUNDING_VALUE) ? "5p" : "1p");
    }
}

/** An ID's or entitlement's deposits, and whether the card says they come back. */
void flipso_cat_id_deposits(FuriString* out, const ItsoProduct* product) {
    const ItsoIdTerms* id = itso_product_id(product);
    if(!flipso_product_is_identity(product) || !product->body_parsed) return;
    if(product->has_deposit) {
        flipso_cat_deposit(
            out,
            "Deposit",
            &product->deposit,
            product->deposit_mop,
            product->deposit_vat,
            (id->id_flags & 0x40) != 0);
    }
    if(id->has_shell_deposit) {
        flipso_cat_deposit(
            out,
            "Card deposit",
            &id->shell_deposit,
            id->shell_deposit_mop,
            id->shell_deposit_vat,
            (id->id_flags & 0x80) != 0);
    }
}
