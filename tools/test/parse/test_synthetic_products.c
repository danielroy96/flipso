/**
 * @file test_synthetic_products.c
 * @brief The synthetic card's five products, each read through its IPE group.
 */
#include "test_parse.h"

/** E1: the pay-as-you-go purse (TYP 2). */
void synthetic_purse(ItsoCard* card) {
    /* E1: pay as you go. Chain is sector 1 then sector 9. */
    uint8_t group[ITSO_MAX_GROUP_LEN];
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector1, sizeof(card_sector1));
    memcpy(group + 64, card_sector9, sizeof(card_sector9));
    itso_parse_ipe(&card->products[0], group, 128, 64);

    char money[32];
    itso_format_money(&itso_product_purse(&card->products[0])->balance, money, sizeof(money));
    printf(
        "  E1 %s: balance %s at %s\n",
        itso_typ_name(card->products[0].typ),
        money,
        fmt_unix(itso_dts_to_unix(card->products[0].value_dts)));
    check(
        "balance is GBP 12.34",
        strcmp(
            money,
            "\xC2\xA3"
            "12.34") == 0);
    check("purse value record read", card->products[0].value_parsed);
    check(
        "balance timestamp is newest record",
        strcmp(fmt_unix(itso_dts_to_unix(card->products[0].value_dts)), "2026-09-14 08:41") == 0);

    /* The common value record header: every product with a value group carries
     * it, and it says what the last thing to happen to the product was. */
    printf(
        "      last action %s, TS# %u, ISAM %08lX\n",
        itso_transaction_name(card->products[0].value_txn),
        card->products[0].value_ts,
        (unsigned long)card->products[0].value_isam);
    check("last action is a fare deduction", card->products[0].value_txn == 7);
    check("TS# read from the live record", card->products[0].value_ts == 101);
    check("modifying POST ISAM read", card->products[0].value_isam == 0xC0FFEE01);
    check("action sequence number read", card->products[0].value_action_seq == 3);

    /* The records the live one displaced are the transactions before it, which
     * is the only statement a card keeps. The group holds two, so the purse
     * should offer the balance as it was as well as the balance as it is. */
    const ItsoProduct* purse = &card->products[0];
    check("purse keeps both value records", purse->value_history_count == 2);
    check(
        "newest history entry is the live record",
        purse->value_history[0].ts == purse->value_ts &&
            purse->value_history[0].dts == purse->value_dts);
    itso_format_money(&purse->value_history[0].amount, money, sizeof(money));
    check(
        "history[0] balance is GBP 12.34",
        strcmp(
            money,
            "\xC2\xA3"
            "12.34") == 0);
    itso_format_money(&purse->value_history[1].amount, money, sizeof(money));
    printf(
        "      previously %s at %s (TS# %u, %s)\n",
        money,
        fmt_unix(itso_dts_to_unix(purse->value_history[1].dts)),
        purse->value_history[1].ts,
        itso_transaction_name(purse->value_history[1].txn));
    check(
        "history[1] is the earlier balance of GBP 15.60",
        strcmp(
            money,
            "\xC2\xA3"
            "15.60") == 0);
    check(
        "history[1] keeps its own timestamp",
        strcmp(fmt_unix(itso_dts_to_unix(purse->value_history[1].dts)), "2026-09-01 12:00") == 0);
    check(
        "history[1] keeps its own transaction type and TS#",
        purse->value_history[1].txn == 4 && purse->value_history[1].ts == 100);
    check(
        "a purse history carries no counter",
        !purse->value_history[0].has_count && !purse->value_history[1].has_count);

    /* The IPE dataset: the commercial terms of the purse. */
    itso_format_money(&itso_product_purse(&card->products[0])->max_value, money, sizeof(money));
    check(
        "purse ceiling is GBP 90.00",
        itso_product_purse(&card->products[0])->has_limits && strcmp(
                                                                  money,
                                                                  "\xC2\xA3"
                                                                  "90.00") == 0);
    itso_format_money(&itso_product_purse(&card->products[0])->max_negative, money, sizeof(money));
    check(
        "overdraft is GBP 2.00",
        strcmp(
            money,
            "\xC2\xA3"
            "2.00") == 0);
    itso_format_money(
        &itso_product_purse(&card->products[0])->top_up_amount, money, sizeof(money));
    check(
        "auto top-up adds GBP 10.00",
        itso_product_purse(&card->products[0])->has_top_up && strcmp(
                                                                  money,
                                                                  "\xC2\xA3"
                                                                  "10.00") == 0);
    itso_format_money(
        &itso_product_purse(&card->products[0])->top_up_threshold, money, sizeof(money));
    check(
        "auto top-up triggers below GBP 5.00",
        strcmp(
            money,
            "\xC2\xA3"
            "5.00") == 0);
    check(
        "auto top-up is enabled in the value record",
        itso_product_purse(&card->products[0])->auto_top_up);
    itso_format_money(&card->products[0].deposit, money, sizeof(money));
    check(
        "deposit is GBP 5.00",
        card->products[0].has_deposit && strcmp(
                                             money,
                                             "\xC2\xA3"
                                             "5.00") == 0);
    check("deposit was paid in cash", card->products[0].deposit_mop == 1);
    check(
        "retailer is not the owner",
        card->products[0].has_retailer && card->products[0].retailer == 247);
    check(
        "remove date says owner only",
        card->products[0].has_remove_date && card->products[0].remove_date == 255);
    check(
        "auto top-up start date 2024-01-01",
        card->products[0].has_start &&
            strcmp(fmt_unix(itso_date_to_unix(card->products[0].start)), "2024-01-01 00:00") == 0);

    /* A journey in progress. */
    itso_format_money(
        &itso_product_purse(&card->products[0])->cumulative_fare, money, sizeof(money));
    check(
        "two journey legs so far",
        itso_product_purse(&card->products[0])->has_journey &&
            itso_product_purse(&card->products[0])->journey_legs == 2);
    check(
        "cumulative fare is GBP 2.65",
        strcmp(
            money,
            "\xC2\xA3"
            "2.65") == 0);

    /* The IPE InstanceID, the only unique identity a product has. */
    check(
        "instance id decoded",
        card->products[0].instance_valid && card->products[0].isam_id == 0x01020304 &&
            card->products[0].isam_seq == 1);
    check("seal key id decoded", card->products[0].key_id == 1);
}

/** E2: the ITSO ID (TYP 16). */
void synthetic_id(ItsoCard* card) {
    uint8_t group[ITSO_MAX_GROUP_LEN];
    /* E2: ITSO ID. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector2, sizeof(card_sector2));
    itso_parse_ipe(&card->products[1], group, 64, 64);
    printf(
        "  E2 %s: name '%s', entitlement %s, class %s\n",
        itso_typ_name(card->products[1].typ),
        itso_product_id(&card->products[1])->name,
        itso_entitlement_name(itso_product_id(&card->products[1])->entitlement_code),
        itso_profile_name(itso_product_id(&card->products[1])->concession_class));
    printf("      valid %s", fmt_unix(itso_date_to_unix(card->products[1].start)));
    printf(
        " to %s\n", fmt_unix(itso_date_to_unix(itso_product_id(&card->products[1])->sub_expiry)));
    check("holder name", strcmp(itso_product_id(&card->products[1])->name, "ALEX MORGAN") == 0);
    check(
        "entitlement is limited free ride",
        itso_product_id(&card->products[1])->entitlement_code == 2);
    check(
        "concession class is pensioner",
        itso_product_id(&card->products[1])->concession_class == 4);
    check(
        "entitlement start 2024-04-01",
        strcmp(fmt_unix(itso_date_to_unix(card->products[1].start)), "2024-04-01 00:00") == 0);
    check(
        "entitlement expiry 2029-03-31",
        strcmp(
            fmt_unix(itso_date_to_unix(itso_product_id(&card->products[1])->sub_expiry)),
            "2029-03-31 00:00") == 0);

    printf(
        "      born %04u-%02u-%02u, gender %s, passback %u min\n",
        itso_product_id(&card->products[1])->dob_year,
        itso_product_id(&card->products[1])->dob_month,
        itso_product_id(&card->products[1])->dob_day,
        itso_gender_name(itso_product_id(&card->products[1])->id_flags),
        card->products[1].passback);
    check(
        "date of birth is 1955-11-03",
        itso_product_id(&card->products[1])->has_dob &&
            itso_product_id(&card->products[1])->dob_year == 1955 &&
            itso_product_id(&card->products[1])->dob_month == 11 &&
            itso_product_id(&card->products[1])->dob_day == 3);
    check(
        "IDFlags say the card is photo personalised",
        itso_product_id(&card->products[1])->has_id_flags &&
            itso_id_personalised(itso_product_id(&card->products[1])->id_flags));
    check(
        "IDFlags allow a companion",
        itso_id_companion(itso_product_id(&card->products[1])->id_flags));
    check(
        "IDFlags record female",
        strcmp(itso_gender_name(itso_product_id(&card->products[1])->id_flags), "Female") == 0);
    check(
        "passback time is 30 minutes",
        card->products[1].has_passback && card->products[1].passback == 30);
    /* TYP 14 and 16 put an accounting reference where other types put the
     * retailer, so reading one as the other would name the wrong operator. */
    check("identity IPE reports no retailer", !card->products[1].has_retailer);
}

/** E3: the period ticket (TYP 22), and its other revisions. */
void synthetic_period(ItsoCard* card) {
    uint8_t group[ITSO_MAX_GROUP_LEN];
    /* E3: period ticket with rail NLC locations. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector3, sizeof(card_sector3));
    memcpy(group + 64, card_sector11, sizeof(card_sector11));
    itso_parse_ipe(&card->products[2], group, 128, 64);
    printf(
        "  E3 %s: expires %s (%s)\n",
        itso_typ_name(card->products[2].typ),
        fmt_unix(itso_date_to_unix(card->products[2].expiry)),
        itso_date_expired(card->products[2].expiry, 1789000000UL) ? "expired" : "valid");
    dump_location("from", &card->products[2].from);
    check(
        "period ticket's retailer is the OID it holds",
        card->products[2].has_retailer && card->products[2].retailer == 57345);
    dump_location("to", &card->products[2].to);
    check(
        "period ticket from NLC 1072",
        card->products[2].from.valid &&
            strcmp(loc_text(&card->products[2].from), "Station 1072") == 0);
    check(
        "period ticket to NLC 1444",
        card->products[2].to.valid &&
            strcmp(loc_text(&card->products[2].to), "Station 1444") == 0);
    check(
        "validity start 2025-01-01",
        card->products[2].has_start &&
            strcmp(fmt_unix(itso_date_to_unix(card->products[2].start)), "2025-01-01 00:00") == 0);
    check(
        "2025-12-31 expiry reads as expired in 2026",
        itso_date_expired(card->products[2].expiry, 1789000000UL));

    /* A period ticket counts unactivated passes, and expires the stock of them
     * separately from the pass currently in use. */
    printf(
        "      %u passes left, current pass to %s\n",
        (unsigned)card->products[2].count,
        fmt_unix(itso_date_to_unix(itso_product_ticket(&card->products[2])->current_expiry)));
    check("period value record read", card->products[2].value_parsed);
    check(
        "period ticket counts passes, not money",
        card->products[2].count_kind == ItsoCountPasses &&
            !itso_product_purse(&card->products[2])->balance.valid);
    check("four passes remain", card->products[2].count == 4);
    check(
        "the period ticket history shows five passes before that",
        card->products[2].value_history_count == 2 &&
            card->products[2].value_history[1].count == 5);
    check(
        "current pass expires 2025-02-28",
        itso_product_ticket(&card->products[2])->has_current_expiry &&
            strcmp(
                fmt_unix(
                    itso_date_to_unix(itso_product_ticket(&card->products[2])->current_expiry)),
                "2025-02-28 00:00") == 0);
    check(
        "unused passes expire 2025-12-31",
        itso_product_ticket(&card->products[2])->has_stored_expiry &&
            strcmp(
                fmt_unix(itso_date_to_unix(itso_product_ticket(&card->products[2])->stored_expiry)),
                "2025-12-31 00:00") == 0);
    check("period ticket auto-renews", card->products[2].auto_renew);

    /* The rest of the revision 3 dataset (table 3.27). CPICC is optional here
     * as in the earlier revisions, gated by bitmap bit 4: the locations above
     * only land on the right bytes if that is honoured. */
    {
        const ItsoProduct* p = &card->products[2];
        const ItsoTicketTerms* t = itso_product_ticket(p);
        char days[40], part[48];
        itso_format_days(itso_ticket_days(t->valid_days, t->flags), days, sizeof(days));
        itso_format_part_days(
            itso_ticket_days(t->valid_days, t->flags), t->flags, part, sizeof(part));
        printf("      days %s; %s\n", days, part);
        check("revision 3 ticket terms read", t->valid);
        check("revision 3 CPICC read", p->has_cpicc && p->cpicc == 0x0457);
        check("no duration group when bit 3 is clear", !t->has_pass_duration);
        check(
            "issued 2024-12-20",
            strcmp(fmt_unix(itso_date_to_unix(t->issue_date)), "2024-12-20 00:00") == 0);
        check("ends 04:30 the next day", t->expiry_time == 1440 + 270);
        check("valid from 09:30", t->has_start_time && t->start_time == 570);
        check("first class", t->travel_class == 1 && strcmp(itso_class_name(1), "First") == 0);
        check("one adult and two children", t->adults == 1 && t->children == 2);
        check(
            "paid GBP 123.45 by card at 20% VAT",
            t->amount_paid.valid && t->amount_paid.value == 12345 && t->paid_mop == 3 &&
                t->vat == 2000);
        check("renews 3 at a time", t->renew_quantity == 3);
        check(
            "transferable, off-peak only",
            (t->flags & ITSO_T22_TRANSFERABLE) && (t->flags & ITSO_T22_OFF_PEAK_ONLY));
        check("passback 20 minutes", p->has_passback && p->passback == 20);
        /* ValidOnDayCode drops Sunday even though TYP22Flags allows it, and
         * neither allows public holidays: both must say yes (rule 7). */
        check("valid Mon-Sat", strcmp(days, "Mon-Sat") == 0);
        check("Saturday afternoons only", strcmp(part, "Sat PM only") == 0);
    }

    /* Revision 1, shaped like a Reading Buses monthly pass: no locations, and
     * PassDuration found after where they would have been. */
    {
        uint8_t* buf = malloc(sizeof(period_rev1_group));
        memcpy(buf, period_rev1_group, sizeof(period_rev1_group));
        ItsoProduct p;
        memset(&p, 0, sizeof(p));
        p.typ = ItsoTypPeriodTicket;
        p.value_group = true;
        itso_parse_ipe(&p, buf, sizeof(period_rev1_group), 64);
        const ItsoTicketTerms* t = itso_product_ticket(&p);
        char days[40];
        itso_format_days(itso_ticket_days(t->valid_days, t->flags), days, sizeof(days));
        printf(
            "  rev 1 period: %u-day pass, ends %02u:%02u, %s\n",
            t->pass_duration,
            t->expiry_time / 60,
            t->expiry_time % 60,
            days);
        check("revision 1 ticket terms read", t->valid);
        check("revision 1 has no locations", !p.from.valid && !p.to.valid);
        check(
            "revision 1 PassDuration after the locations",
            t->has_pass_duration && t->pass_duration == 31 &&
                t->duration_unit == ItsoDurationDays);
        check("revision 1 has no CPICC", !p.has_cpicc);
        check("sold by operator 163", p.has_retailer && p.retailer == 163);
        check("ends 04:00 on the expiry date", t->expiry_time == 240);
        check("no issue date recorded", t->issue_date == 0);
        check("no validity start recorded", t->valid_from_dts == 0 && !p.has_start);
        check("valid every day", strcmp(days, "Every day") == 0);
        check("public holidays too", itso_ticket_days(t->valid_days, t->flags) & ITSO_DOW_SPECIAL);
        check("no amount paid recorded", !t->amount_paid.valid);
        check("one adult", t->adults == 1 && t->children == 0 && t->concessions == 0);
        check("stored-pass mode", itso_product_ticket(&p)->stored_passes && !p.auto_renew);
        check("pass activated, none left", p.value_parsed && p.value_txn == 13 && p.count == 0);
        check(
            "current pass to 2026-02-05",
            strcmp(
                fmt_unix(itso_date_to_unix(itso_product_ticket(&p)->current_expiry)),
                "2026-02-05 00:00") == 0);
        free(buf);
        itso_product_free(&p);
    }

    /* Revision 2, shaped like a South Western Railway annual season: AmountPaid
     * is four bytes here where revision 1 had two. */
    {
        uint8_t* buf = malloc(sizeof(period_rev2_group));
        memcpy(buf, period_rev2_group, sizeof(period_rev2_group));
        ItsoProduct p;
        memset(&p, 0, sizeof(p));
        p.typ = ItsoTypPeriodTicket;
        itso_parse_ipe(&p, buf, sizeof(period_rev2_group), 64);
        const ItsoTicketTerms* t = itso_product_ticket(&p);
        printf(
            "  rev 2 period: paid %ld, from %s to %s\n",
            (long)t->amount_paid.value,
            loc_text(&p.from),
            loc_text(&p.to));
        check("revision 2 ticket terms read", t->valid);
        check(
            "revision 2 paid GBP 4040.00 by card",
            t->amount_paid.valid && t->amount_paid.value == 404000 &&
                t->amount_paid.currency == 0 && t->paid_mop == 3);
        check("revision 2 standard class", t->travel_class == 2);
        check("revision 2 validity code", t->validity_code == 17);
        check(
            "revision 2 issued 2018-06-21",
            strcmp(fmt_unix(itso_date_to_unix(t->issue_date)), "2018-06-21 00:00") == 0);
        check(
            "revision 2 valid from 2018-06-25 00:00",
            strcmp(fmt_unix(itso_dts_to_unix(t->valid_from_dts)), "2018-06-25 00:00") == 0);
        check("revision 2 ends 04:30 the next day", t->expiry_time == 1710);
        check(
            "revision 2 locations behind RouteCode",
            p.from.valid && strcmp(loc_text(&p.from), "Station 5685") == 0 && p.to.valid &&
                strcmp(loc_text(&p.to), "Station 0035") == 0);
        free(buf);
        itso_product_free(&p);
    }

    /* Revision 3's duration group, which counts in a unit of its own. */
    {
        uint8_t* buf = malloc(sizeof(period_rev3_group));
        memcpy(buf, period_rev3_group, sizeof(period_rev3_group));
        ItsoProduct p;
        memset(&p, 0, sizeof(p));
        p.typ = ItsoTypPeriodTicket;
        itso_parse_ipe(&p, buf, sizeof(period_rev3_group), 64);
        const ItsoTicketTerms* t = itso_product_ticket(&p);
        check(
            "revision 3 pass lasts one month",
            t->has_pass_duration && t->pass_duration == 1 &&
                t->duration_unit == ItsoDurationMonths);
        check(
            "revision 3 stock renews for 365 days",
            t->has_stock_duration && t->stock_duration == 365);
        check("revision 3 without CPICC reads none", !p.has_cpicc);
        free(buf);
        itso_product_free(&p);
    }

    /* The day formatter on its own: ranges, lists and the empty case. */
    {
        char s[40];
        itso_format_days(ITSO_DOW_WEEKDAYS, s, sizeof(s));
        check("weekdays render as Mon-Fri", strcmp(s, "Mon-Fri") == 0);
        itso_format_days(ITSO_DOW_SATURDAY | ITSO_DOW_SUNDAY, s, sizeof(s));
        check("a two-day weekend is listed", strcmp(s, "Sat Sun") == 0);
        itso_format_days(0xA8, s, sizeof(s));
        check("scattered days are listed", strcmp(s, "Mon Wed Fri") == 0);
        itso_format_days(ITSO_DOW_SPECIAL, s, sizeof(s));
        check("holidays alone are no weekday", strcmp(s, "None") == 0);
        itso_format_days(ITSO_DOW_ALL_DAYS, s, 4);
        check("a short buffer truncates safely", strlen(s) < 4);
        check("unset filters mean every day", itso_ticket_days(0, 0) == 0xFF);
        itso_format_part_days(0xFF, 0, s, sizeof(s));
        check("flags that do not restrict say nothing", s[0] == '\0');
    }
}

/** E4: the journey ticket (TYP 23). */
void synthetic_journey(ItsoCard* card) {
    uint8_t group[ITSO_MAX_GROUP_LEN];
    /* E4: a journey ticket at format revision 2, with a value record that counts
     * rides. The chain is sector 4 (the IPE, spilling into a second sector) then
     * sector 10 (the value records). */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector4, sizeof(card_sector4));
    memcpy(group + sizeof(card_sector4), card_sector10, sizeof(card_sector10));
    itso_parse_ipe(&card->products[3], group, sizeof(card_sector4) + sizeof(card_sector10), 64);
    printf(
        "  E4 %s: rev %u, rides left %u, used %d\n",
        itso_typ_name(card->products[3].typ),
        card->products[3].format_rev,
        (unsigned)card->products[3].count,
        itso_product_ticket(&card->products[3])->ticket_used);
    dump_location("from", &card->products[3].from);
    dump_location("to", &card->products[3].to);
    check("journey ticket is revision 2", card->products[3].format_rev == 2);
    check(
        "journey ticket's retailer is rail's NLC",
        card->products[3].has_retailer &&
            card->products[3].retailer == (0x8000 | (5 << 10) | 631));
    check(
        "journey ticket from NLC 5631",
        card->products[3].from.valid &&
            strcmp(loc_text(&card->products[3].from), "Station 5631") == 0);
    check(
        "journey ticket to NLC 5685",
        card->products[3].to.valid &&
            strcmp(loc_text(&card->products[3].to), "Station 5685") == 0);

    /* Both value records carry the same DTS, so only TS# distinguishes them. The
     * live one is the later of the two: the ride has been spent. */
    check("journey value record read", card->products[3].value_parsed);
    check(
        "journey ticket counts rides, not money",
        card->products[3].count_kind == ItsoCountRides &&
            !itso_product_purse(&card->products[3])->balance.valid);
    check(
        "TS# picks the newer record over an equal DTS",
        card->products[3].count == 0 && itso_product_ticket(&card->products[3])->ticket_used);

    /* And the history has to be ordered the same way. Both records share a
     * timestamp to the minute, so a history sorted by time would put them in
     * either order and read as a ride being restored rather than spent. */
    check("journey ticket keeps both records", card->products[3].value_history_count == 2);
    check(
        "history is ordered by TS#, not by an equal DTS",
        card->products[3].value_history[0].ts == 5 && card->products[3].value_history[1].ts == 4);
    check(
        "the earlier record still had the ride",
        card->products[3].value_history[1].has_count &&
            card->products[3].value_history[1].count == 1 &&
            card->products[3].value_history[0].count == 0);
}

/** E5: the loyalty scheme (TYP 3). */
void synthetic_loyalty(ItsoCard* card) {
    uint8_t group[ITSO_MAX_GROUP_LEN];
    /* E5: loyalty. A points balance is three bytes wide, so decoding it as a
     * purse would silently truncate it to the low two. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector5, sizeof(card_sector5));
    memcpy(group + 64, card_sector12, sizeof(card_sector12));
    itso_parse_ipe(&card->products[4], group, 128, 64);
    printf(
        "  E5 %s: %lu points\n",
        itso_typ_name(card->products[4].typ),
        (unsigned long)card->products[4].count);
    check("loyalty value record read", card->products[4].value_parsed);
    check(
        "loyalty counts points",
        card->products[4].count_kind == ItsoCountPoints &&
            !itso_product_purse(&card->products[4])->balance.valid);
    check("74500 points does not truncate to 16 bits", card->products[4].count == 74500);
    check(
        "the loyalty history keeps the earlier points balance",
        card->products[4].value_history_count == 2 &&
            card->products[4].value_history[1].count == 1200);
    check(
        "loyalty remove date is 30 days",
        card->products[4].has_remove_date && card->products[4].remove_date == 30);
}
