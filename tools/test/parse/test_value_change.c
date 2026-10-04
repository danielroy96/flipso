/**
 * @file test_value_change.c
 * @brief The amount of a transaction, worked out from two value records.
 */
#include "test_parse.h"

/** One value record, as TS 1000-2 table 15 lays out its common header. */
static void
    value_record(uint8_t* out, uint8_t txn, uint16_t ts, uint32_t dts, uint16_t tail, uint8_t valc) {
    memset(out, 0, ITSO_VALUE_RECORD_LEN);
    out[0] = (uint8_t)(txn << 4 | ts >> 8);
    out[1] = (uint8_t)ts;
    out[2] = (uint8_t)(dts >> 16);
    out[3] = (uint8_t)(dts >> 8);
    out[4] = (uint8_t)dts;
    /* The tail: a TYP 2 or 4 balance in the first two bytes and its
     * ValueCurrencyCode in the next nibble (TS 1000-5 tables 4 and 12); a
     * journey ticket's rides left in the first byte (table 33). */
    out[10] = (uint8_t)(tail >> 8);
    out[11] = (uint8_t)tail;
    out[12] = (uint8_t)(valc << 4);
}

/** @p p's history, decoded from @p count records as a saved file holds them. */
static void history(ItsoProduct* p, uint8_t typ, const uint8_t* records, uint8_t count) {
    itso_product_free(p);
    memset(p, 0, sizeof(*p));
    p->typ = typ;
    /* An exact-length heap copy, so an over-read is the sanitiser's to catch. */
    size_t len = (size_t)count * ITSO_VALUE_RECORD_LEN;
    uint8_t* copy = malloc(len);
    memcpy(copy, records, len);
    itso_parse_value_history(p, copy, len);
    free(copy);
}

void value_changes(const ItsoCard* card) {
    ItsoProduct p = {0};
    const size_t L = ITSO_VALUE_RECORD_LEN;
    uint8_t r[3 * ITSO_VALUE_RECORD_LEN];
    int32_t change = 0;

    /* The synthetic purse: a top-up to GBP 15.60, then a fare to GBP 12.34. */
    const ItsoProduct* purse = &card->products[0];
    check(
        "the synthetic purse's live record cost GBP 3.26",
        itso_value_change(purse, 0, &change) && change == -326);
    check("its oldest record has nothing to compare with", !itso_value_change(purse, 1, &change));

    /* TS# 4095 then 0: the counter wraps, and the two are still one write apart. */
    value_record(r, 4, 4094, 1000, 1000, 0);
    value_record(r + L, 7, 4095, 2000, 645, 0);
    value_record(r + 2 * L, 7, 0, 3000, 290, 0);
    history(&p, ItsoTypStoredTravelRights, r, 3);
    check(
        "the history is newest first across the wrap",
        p.value_history_count == 3 && p.value_history[0].ts == 0 && p.value_history[2].ts == 4094);
    check("a fare across the TS# wrap", itso_value_change(&p, 0, &change) && change == -355);
    check("and the one before it", itso_value_change(&p, 1, &change) && change == -355);
    check("the oldest has no amount", !itso_value_change(&p, 2, &change));
    check("nor does an index past the history", !itso_value_change(&p, 3, &change));

    /* A record rolled off between two reads: TS# 11 is missing, and what
     * 10 to 12 did was two transactions, not one. */
    value_record(r, 4, 10, 1000, 2000, 0);
    value_record(r + L, 7, 12, 2000, 1290, 0);
    history(&p, ItsoTypStoredTravelRights, r, 2);
    check("no amount across a gap in TS#", !itso_value_change(&p, 0, &change));

    /* A top-up, in the other direction. */
    value_record(r, 7, 20, 1000, 150, 0);
    value_record(r + L, 4, 21, 2000, 2150, 0);
    history(&p, ItsoTypStoredTravelRights, r, 2);
    check("a top-up adds", itso_value_change(&p, 0, &change) && change == 2000);

    /* Sterling then euros (TS 1000-5 annex A.21): no difference to give. */
    value_record(r, 4, 30, 1000, 2000, 0);
    value_record(r + L, 7, 31, 2000, 1500, 1);
    history(&p, ItsoTypStoredTravelRights, r, 2);
    check("no amount across two currencies", !itso_value_change(&p, 0, &change));

    /* The same currency at two scales: the amounts are already scaled, so the
     * difference is in pence either way. */
    value_record(r, 4, 40, 1000, 2000, 0);
    value_record(r + L, 7, 41, 2000, 150, 1 << 2); /* times ten: GBP 15.00 */
    history(&p, ItsoTypStoredTravelRights, r, 2);
    check("the same currency at two scales", itso_value_change(&p, 0, &change) && change == -500);

    /* TYP 4 counts spend up, and a fare is still money the holder paid. */
    value_record(r, 7, 50, 1000, 2310, 0);
    value_record(r + L, 7, 51, 2000, 4620, 0);
    history(&p, ItsoTypChargeToAccount1, r, 2);
    check("a TYP 4 fare is money paid", itso_value_change(&p, 0, &change) && change == -2310);

    /* TYP 5's count is cleared by a new charge period, maybe in a fare's write. */
    value_record(r, 7, 70, 1000, 22 << 8, 0);
    value_record(r + L, 7, 71, 2000, 1 << 8, 0);
    history(&p, ItsoTypChargeToAccount2, r, 2);
    check("no change in a count a charge period clears", !itso_value_change(&p, 0, &change));

    /* Next by TS# and earlier by time: a lap of the counter apart. */
    value_record(r, 4, 80, 3000, 2000, 0);
    value_record(r + L, 7, 81, 2000, 1500, 0);
    history(&p, ItsoTypStoredTravelRights, r, 2);
    check("no amount for a pair written out of time order", !itso_value_change(&p, 0, &change));

    /* A counter: rides left on a journey ticket. */
    value_record(r, 1, 5, 1000, 10 << 8, 0);
    value_record(r + L, 7, 6, 2000, 8 << 8, 0);
    value_record(r + 2 * L, 7, 8, 3000, 7 << 8, 0);
    history(&p, ItsoTypJourneyTicket, r, 3);
    check("a counter's change", itso_value_change(&p, 1, &change) && change == -2);
    check("and none across a gap in it", !itso_value_change(&p, 0, &change));

    /* A type whose tail is not decoded holds neither a balance nor a count. */
    value_record(r, 1, 60, 1000, 500, 0);
    value_record(r + L, 7, 61, 2000, 400, 0);
    history(&p, ItsoTypEntitlement, r, 2);
    check("no amount where the record holds no value", !itso_value_change(&p, 0, &change));

    /* A change can be one token either way, and a token is singular both ways. */
    char text[24];
    ItsoMoney token = {.value = -1, .currency = 2, .valid = true};
    itso_format_money(&token, text, sizeof(text));
    check("one token spent is one token", strcmp(text, "-1 token") == 0);

    itso_product_free(&p);
}
