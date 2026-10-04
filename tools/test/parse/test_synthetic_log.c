/**
 * @file test_synthetic_log.c
 * @brief The synthetic card's cyclic log: four taps, rail and bus.
 */
#include "test_parse.h"

/** The taps, newest first. */
void synthetic_log(ItsoCard* card) {
    char money[32];
    printf("\n== Taps ==\n");
    itso_parse_log(card, card_log, sizeof(card_log));
    printf("  %u tap(s)\n", card->tap_count);
    for(uint8_t i = 0; i < card->tap_count; i++) {
        const ItsoTap* tap = &card->taps[i];
        itso_format_money(&tap->amount, money, sizeof(money));
        printf(
            "  [%u] %s at %s, %s\n",
            i,
            itso_transaction_name(tap->transaction_type),
            fmt_unix(itso_dts_to_unix(tap->dts)),
            money);
        dump_location("from", &tap->origin);
        dump_location("to", &tap->destination);
    }
    check("four taps decoded", card->tap_count == 4);
    check("newest tap first is tap out", card->taps[0].transaction_type == 12);
    check(
        "tap out origin",
        card->taps[0].origin.valid &&
            strcmp(loc_text(&card->taps[0].origin), "Station 1072") == 0);
    check(
        "tap out destination",
        card->taps[0].destination.valid &&
            strcmp(loc_text(&card->taps[0].destination), "Station 1444") == 0);
    itso_format_money(&card->taps[0].amount, money, sizeof(money));
    check(
        "tap out fare GBP 2.65",
        strcmp(
            money,
            "\xC2\xA3"
            "2.65") == 0);
    check("older tap is tap in", card->taps[1].transaction_type == 11);

    /* The third record is on format revision 4, which a check-in/check-out
     * closed system writes on exit. It carries groups revisions 1 and 2 have no
     * bit for, so mis-sizing any of them would shift all that follow. */
    /* The oldest tap is a bus journey, so the log's LOC2 records are exercised
     * on a NaptanCode as well as on the rail NLCs above, and the stop code
     * reaches the app in the form the stop table is keyed on. */
    const ItsoTap* bus = &card->taps[3];
    check(
        "bus tap origin is a stop",
        bus->origin.valid && strcmp(loc_text(&bus->origin), "Stop 00062624") == 0);
    check(
        "bus tap origin offers a NaptanCode",
        strcmp(loc_code(&bus->origin), "00062624") == 0 &&
            loc_kind(&bus->origin) == ItsoLocCodeNaptan);
    check(
        "bus tap destination is a stop",
        bus->destination.valid && strcmp(loc_text(&bus->destination), "Stop 62697956") == 0);
    check(
        "bus tap destination offers a NaptanCode",
        strcmp(loc_code(&bus->destination), "62697956") == 0 &&
            loc_kind(&bus->destination) == ItsoLocCodeNaptan);

    const ItsoTap* rev4 = &card->taps[2];
    printf(
        "  rev%u: via %s, paid by %s, entry %s, entry op %u\n",
        rev4->format_rev,
        loc_text(&rev4->route),
        itso_payment_name(rev4->mop),
        fmt_unix(itso_dts_to_unix(rev4->entry_dts)),
        rev4->entry_oid);
    check("third record is format revision 4", rev4->format_rev == 4);
    check(
        "routing code is NLC 1444",
        rev4->route.valid && strcmp(loc_text(&rev4->route), "Station 1444") == 0);
    check(
        "destination survives the routing group",
        rev4->destination.valid && strcmp(loc_text(&rev4->destination), "Station 5685") == 0);
    itso_format_money(&rev4->amount, money, sizeof(money));
    check(
        "rev 4 fare GBP 4.80",
        strcmp(
            money,
            "\xC2\xA3"
            "4.80") == 0);
    check("fare was paid in cash", rev4->has_mop && rev4->mop == 1);
    check("fare was collected", !rev4->no_fare_charged);
    check("VAT is 20%", rev4->has_vat && rev4->vat == 2000);
    check("POST network IIN read as the BCD it is", rev4->has_iin && rev4->iin == 633597);
    check(
        "candidate IPEs read",
        rev4->has_cipe && rev4->cipe[0] == 1 && rev4->cipe[1] == 4 && rev4->cipe[2] == 0);
    check("inspection flag set, invalid travel clear", rev4->inspected && !rev4->invalid_travel);
    check(
        "entry timestamp is 2026-09-12 08:12",
        rev4->has_entry &&
            strcmp(fmt_unix(itso_dts_to_unix(rev4->entry_dts)), "2026-09-12 08:12") == 0);
    check("entry operator is 109", rev4->has_entry_oid && rev4->entry_oid == 109);
}
