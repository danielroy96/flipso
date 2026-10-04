/**
 * @file flipso_product_history.c
 * @brief What has happened to a product: its value records, newest first.
 */
#include "flipso_product_i.h"

/** What the product's newest value record says the last transaction was. */
void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product) {
    if(!product->value_parsed) return;
    furi_string_cat_printf(
        out, "Last transaction: %s\n", itso_transaction_name(product->value_txn));
    if(product->value_dts) flipso_cat_datetime_line(out, "  ", "When", product->value_dts);
}

/** One transaction, as three short lines. */
static void flipso_cat_value_record(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoValueRecord* record) {
    const ItsoPurseTerms* purse = itso_product_purse(product);
    /* Three short lines rather than one wide one: a date and time is sixteen
     * characters, which leaves nothing for what happened or for what the
     * balance became. */
    furi_string_cat_printf(out, "%s\n", itso_transaction_name(record->txn));
    flipso_cat_datetime_line(out, "  ", "When", record->dts);

    if(record->amount.valid) {
        flipso_cat_money(
            out, "  ", purse->balance_is_spend ? "Spent so far" : "Balance", &record->amount);
    } else if(record->has_count) {
        /* The counter means whatever the product's type says it means, and it
         * means the same thing in every record. */
        const char* label = itso_count_name(product->count_kind);
        if(label) furi_string_cat_printf(out, "  %s: %lu\n", label, (unsigned long)record->count);
    }
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
            flipso_pages_at(p, off_card ? FlipsoSlotOffCard : FlipsoSlotHistory), product, record);
    }
}
