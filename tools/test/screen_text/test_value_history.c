/**
 * @file test_value_history.c
 * @brief What each transaction in a product's history did, as the screen says it.
 */
#include "test_format.h"

/** A record holding a balance of @p pence, in sterling. */
static ItsoValueRecord money(uint8_t txn, uint16_t ts, ItsoDts dts, int32_t pence) {
    ItsoValueRecord record = {.dts = dts, .ts = ts, .txn = txn, .on_card = true};
    record.amount = (ItsoMoney){.value = pence, .currency = 0, .valid = true};
    return record;
}

/** A record holding a counter of @p count. */
static ItsoValueRecord counter(uint8_t txn, uint16_t ts, ItsoDts dts, uint32_t count) {
    ItsoValueRecord record = {.dts = dts, .ts = ts, .txn = txn, .on_card = true};
    record.count = count;
    record.has_count = true;
    return record;
}

/** @p p, built from @p base, with @p records as its history and the first live. */
static void
    with_history(ItsoProduct* p, const ItsoProduct* base, ItsoValueRecord* records, uint8_t count) {
    *p = *base;
    p->value_history = records;
    p->value_history_count = count;
    p->value_parsed = true;
    p->value_txn = records[0].txn;
    p->value_ts = records[0].ts;
    p->value_dts = records[0].dts;
    if(!records[0].has_count) p->terms.purse.balance = records[0].amount;
    if(records[0].has_count) p->count = records[0].count;
}

void value_history_screens(const FlipsoFormat* f, const ItsoCard* card, FuriString* text) {
    const ItsoProduct* purse = &card->products[0];
    const ItsoDts when = purse->value_dts;
    ItsoProduct p;

    /* Newest first: a fare, the fare before it, then a gap where TS# 10 rolled
     * off between two reads, a top-up, and the oldest record. */
    ItsoValueRecord records[] = {
        money(7, 12, when, 2415),
        money(7, 11, when, 2765),
        money(4, 9, when, 3120),
        money(1, 8, when, 1120),
    };
    with_history(&p, purse, records, COUNT_OF(records));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &p);
    printf("\n%s\n", furi_string_get_cstr(text));
    house_style("value history", text);
    check(
        "the last transaction says what it cost",
        page_starts(text, "History", "Last transaction: Fare paid\n") &&
            on_page(
                text,
                "History",
                "\n  Amount: -\xC2\xA3"
                "3.50\n"));
    check(
        "a top-up says what it added, signed",
        on_page(
            text,
            "History",
            "\n  Amount: +\xC2\xA3"
            "20.00\n  Balance: \xC2\xA3"
            "31.20\n"));
    check(
        "a fare before a gap in TS# shows no amount",
        on_page(
            text,
            "History",
            "\n  When: 14/09/2026 08:41\n  Balance: \xC2\xA3"
            "27.65\n"));
    check(
        "nor does the oldest",
        on_page(
            text,
            "History",
            "\n  Balance: \xC2\xA3"
            "11.20\n") &&
            !shows(
                text,
                "+\xC2\xA3"
                "11.20"));

    /* A transaction that left the balance as it was has no sign to show. */
    ItsoValueRecord level[] = {money(0, 3, when, 500), money(7, 2, when, 500)};
    with_history(&p, purse, level, COUNT_OF(level));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &p);
    check(
        "a change of nothing is unsigned",
        on_page(
            text,
            "History",
            "\n  Amount: \xC2\xA3"
            "0.00\n"));

    /* TYP 4 counts spend up: a fare raises it, and is still money paid out. */
    ItsoValueRecord spend[] = {money(7, 51, when, 4620), money(7, 50, when, 2310)};
    with_history(&p, purse, spend, COUNT_OF(spend));
    p.typ = ItsoTypChargeToAccount1;
    p.terms.purse.balance_is_spend = true;
    furi_string_reset(text);
    flipso_format_product(text, f, card, &p);
    printf("\n%s\n", furi_string_get_cstr(text));
    house_style("charge to account history", text);
    check(
        "a TYP 4 fare is money paid out",
        on_page(
            text,
            "History",
            "\n  Amount: -\xC2\xA3"
            "23.10\n") &&
            on_page(
                text,
                "History",
                "\n  Spent so far: \xC2\xA3"
                "23.10\n"));

    /* A counter: rides used, and rides added. */
    const ItsoProduct* journey = flipso_find_product(card, ItsoTypJourneyTicket);
    ItsoValueRecord rides[] = {
        counter(7, 7, when, 7),
        counter(4, 6, when, 9),
        counter(7, 5, when, 6),
    };
    with_history(&p, journey, rides, COUNT_OF(rides));
    furi_string_reset(text);
    flipso_format_product(text, f, card, &p);
    printf("\n%s\n", furi_string_get_cstr(text));
    house_style("journey ticket history", text);
    check("two rides used are a change of -2", on_page(text, "History", "\n  Change: -2\n"));
    check("rides added are signed", on_page(text, "History", "\n  Change: +3\n  Rides left: 9\n"));
    check("the oldest has no change", occurrences_of(text, "  Change: ") == 2);
}
