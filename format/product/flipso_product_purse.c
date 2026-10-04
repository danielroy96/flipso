/**
 * @file flipso_product_purse.c
 * @brief A purse or charge-to-account product (TYP 2, 4 and 5): its terms and fare capping.
 */
#include "flipso_product_i.h"

/**
 * The commercial terms of a purse or charge-to-account product: its ceiling,
 * any overdraft, the auto-top-up rule and the deposit paid for it.
 */
void flipso_cat_purse_terms(FuriString* out, const ItsoProduct* product) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    /* An ID's deposits are its own, and flipso_cat_id_details() shows them with
     * what the card says about getting them back. */
    if(flipso_product_is_identity(product)) return;

    if(purse->has_limits && purse->max_value.valid && purse->max_value.value) {
        flipso_cat_money(
            out,
            "",
            product->typ == ItsoTypStoredTravelRights ? "Balance limit" : "Spending limit",
            &purse->max_value);
    }
    if(purse->max_negative.valid && purse->max_negative.value) {
        flipso_cat_money(out, "", "Overdraft limit", &purse->max_negative);
    }

    if(purse->has_top_up) {
        furi_string_cat_printf(out, "Auto top-up: %s\n", purse->auto_top_up ? "On" : "Off");
        flipso_cat_money(out, "  ", "Amount", &purse->top_up_amount);
        flipso_cat_money(out, "  ", "When below", &purse->top_up_threshold);
        if(purse->auto_top_up_internal) flipso_cat_flag(out, "  ", "From another purse", true);
        /* On a purse the start date gates auto-top-up rather than the product:
         * TS 1000-5 table 2 is explicit that stored travel rights may be spent
         * at any time. */
        if(product->typ == ItsoTypStoredTravelRights && product->has_start) {
            flipso_cat_date_line(out, "  ", "Starts", product->start);
        }
    }

    if(product->has_deposit) {
        flipso_cat_money(out, "", "Deposit", &product->deposit);
        if(product->deposit_mop) {
            furi_string_cat_printf(
                out, "  Paid by: %s\n", itso_payment_name(product->deposit_mop));
        }
        flipso_cat_vat(out, "  ", product->deposit_vat);
    }
}

/*
 * A product's fare-capping progress is decoded from the capture on demand rather
 * than held in ItsoProduct: four locations make it the largest thing a product
 * could carry, and at most one product has one.
 */
bool flipso_decode_capping(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoCapping* cap) {
    if(product->vgx_ref != 1 && product->vgx_ref != 2) return false;
    if(!product->on_card || !f->capture) return false;
    size_t len = 0;
    const uint8_t* group = flipso_capture_product_group(f->capture, product->dir_index, &len);
    if(!group) return false;
    /* The whole ValueCurrencyCode, scaling bits and all, as the balance has. */
    uint8_t valc = product->value_parsed ? product->value_valc : 0;
    return itso_parse_capping(group, len, card->sector_size, valc, cap);
}

void flipso_cat_capping(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    ItsoCapping* cap = malloc(sizeof(ItsoCapping));
    if(flipso_decode_capping(f, card, product, cap)) {
        flipso_cat_page(out, FlipsoIconCap, "Fare capping");
        bool any = false;
        static const char* const rules[] = {NULL, "Daily", "Short period", "Long period"};
        for(uint8_t a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
            const ItsoCapAccumulator* acc = &cap->acc[a];
            if(acc->rule == ItsoCapRuleNone) continue;
            any = true;
            furi_string_cat_printf(
                out,
                "Cap %u: %s\n",
                a + 1,
                acc->rule < COUNT_OF(rules) ? rules[acc->rule] : "Other");
            if(acc->rule == ItsoCapRuleDay) {
                flipso_cat_money(out, "  ", "Spent today", &acc->day);
            } else {
                flipso_cat_money(out, "  ", "Spent so far", &acc->multiday);
                if(acc->day_count) furi_string_cat_printf(out, "  Days in: %u\n", acc->day_count);
            }
            flipso_cat_money(out, "  ", "Without capping", &acc->uncapped);
            if(acc->last_fare.valid && acc->last_fare.value) {
                flipso_cat_money(out, "  ", "Last fare", &acc->last_fare);
            }
            if(acc->last_txn) {
                furi_string_cat_printf(
                    out, "  Last fare type: %s\n", itso_transaction_name(acc->last_txn));
            }
            if(acc->cap_dts) flipso_cat_datetime_line(out, "  ", "Last capped", acc->cap_dts);
            /* The reduced form keeps one location for all four sets, in the
             * first; the full form keeps one per set. */
            flipso_cat_location(out, f, "  ", "Capped at", &acc->location);
        }
        /* An unused product holds the structure with every rule at zero. */
        if(!any) furi_string_cat(out, "Status: Not used yet\n");
    }
    free(cap);
}
