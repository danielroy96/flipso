/**
 * @file flipso_product_history.c
 * @brief What has happened to a product: its value records, newest first.
 */
#include "flipso_product_i.h"

#include <ctype.h>

/**
 * What the transaction in value record @p index did, worked out from the record
 * before it: "  Amount: -£3.55", or "  Rides: -1" for a counter. Nothing when
 * itso_value_change() has no amount to give.
 */
static void flipso_cat_value_change(FuriString* out, const ItsoProduct* product, uint8_t index) {
    int32_t change;
    if(!itso_value_change(product, index, &change)) return;
    const ItsoValueRecord* record = &product->value_history[index];

    /* Always signed, whichever way it went: a fare and a top-up of the same
     * size are otherwise the same line. */
    if(!record->has_count) {
        const ItsoMoney amount = {
            .value = change, .currency = record->amount.currency, .valid = true};
        flipso_cat_money_change(out, "  ", "Amount", &amount);
    } else if(itso_count_unit(product->count_kind)) {
        /* In what the counter counts, as an amount is in pounds: "Rides: -2".
         * A counter with no name has no line of its own for it to explain. */
        furi_string_cat_printf(
            out,
            "  %s: %s%ld\n",
            itso_count_unit(product->count_kind),
            change > 0 ? "+" : "",
            (long)change);
    }
}

/** One transaction, as short lines: what it was, when, what it did, and what was left. */
static void flipso_cat_value_record(FuriString* out, const ItsoProduct* product, uint8_t index) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    const ItsoValueRecord* record = &product->value_history[index];
    /* Short lines rather than one wide one: a date and time is sixteen
     * characters, which leaves nothing for what happened or for what the
     * balance became. */
    furi_string_cat_printf(out, "%s\n", itso_transaction_name(record->txn));
    flipso_cat_datetime_line(out, "  ", "When", record->dts);
    flipso_cat_value_change(out, product, index);

    /* has_count first: the counter and the balance share their room, so the
     * balance is only there to be read when the counter is not. */
    if(record->has_count) {
        /* The counter means whatever the product's type says it means, and it
         * means the same thing in every record. */
        const char* label = itso_count_name(product->count_kind);
        const char* unit = itso_count_unit(product->count_kind);
        if(label && unit && strcmp(label, unit) == 0) {
            /* A counter named by its own unit - loyalty's "Points" - would
             * label the change above and the total here alike, so the total
             * is the balance, in that unit: "Balance: 4250 points". */
            furi_string_cat_printf(
                out,
                "  Balance: %lu %c%s\n",
                (unsigned long)record->count,
                tolower((unsigned char)unit[0]),
                unit + 1);
        } else if(label) {
            furi_string_cat_printf(out, "  %s: %lu\n", label, (unsigned long)record->count);
        }
    } else if(record->amount.valid) {
        flipso_cat_money(
            out, "  ", purse->balance_is_spend ? "Spent so far" : "Balance", &record->amount);
    }
}

/**
 * The newest transaction, the live record's, at the head of the history and
 * in the same shape as every entry under it: a list whose first entry alone
 * read "Last transaction: Fare paid" had two ways of saying one thing.
 */
void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product) {
    if(!product->value_parsed) return;
    /* The live record is the newest in the history, unless a file remembers
     * one newer still or the live one could not be kept; then the head of the
     * history is some other transaction, and only what the live record itself
     * says is shown. */
    const ItsoValueRecord* head = &product->value_history[0];
    if(product->value_history_count && head->ts == product->value_ts &&
       head->dts == product->value_dts) {
        flipso_cat_value_record(out, product, 0);
        return;
    }
    furi_string_cat_printf(out, "%s\n", itso_transaction_name(product->value_txn));
    if(product->value_dts) flipso_cat_datetime_line(out, "  ", "When", product->value_dts);
}

/**
 * The transactions before the live one, newest first.
 *
 * Index 0 is the live record, which the screen has already shown as the
 * balance or the counter, so a product whose group holds one written record
 * has no history to show rather than a page with one line on it. Unless
 * there is no live record: a saved card whose product group did not read still
 * has whatever records the file kept, and every one of those is earlier by
 * definition.
 */
void flipso_cat_value_history(FlipsoPages* p, const ItsoProduct* product) {
    uint8_t first = product->value_parsed ? 1 : 0;

    /* Two pages rather than one. A card keeps two value records and writes
     * each new one over the oldest, so anything before them survives only
     * because a file remembered it - and running the two together would present
     * what the file knows as what the card says. A product the card has dropped
     * has already said so on its first page, and every record it has is from
     * a file, so it keeps one list rather than being told the same thing twice. */
    const bool split = product->on_card;
    for(uint8_t i = first; i < product->value_history_count; i++) {
        const ItsoValueRecord* record = &product->value_history[i];
        const bool off_card = split && !record->on_card;
        flipso_cat_value_record(
            flipso_pages_at(p, off_card ? FlipsoSlotOffCard : FlipsoSlotHistory), product, i);
    }
}
