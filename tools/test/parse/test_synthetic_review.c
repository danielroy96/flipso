/**
 * @file test_synthetic_review.c
 * @brief Elements added after a review of real cards against TS 1000 (2026-09-26).
 */
#include "test_parse.h"

/** Each element here was on a real card and not decoded. */
void synthetic_review(ItsoCard* card) {
    printf("\nISAM identities, IDs, journey terms, taps and capping\n");

    /* TS 1000-2 annex B: the OID inside an ISAM ID, in all four ranges. Two of
     * these are ISAMs read off real cards: Reading Buses and Reading's
     * concessionary pass issuer. */
    check("13-bit ISAM OID", itso_isam_oid(0x051844C0) == 163);
    check("14-bit extended ISAM OID", itso_isam_oid(0x0164000E) == 8236);
    check("16-bit ISAM OID from 24576", itso_isam_oid((9u << 19) | (0x6u << 16)) == 24585);
    check("16-bit ISAM OID from 57344", itso_isam_oid((1u << 19) | (0x7u << 16)) == 57345);

    check("directory InstanceID read", card->dir_instance_valid);
    check(
        "directory last written by operator 109",
        itso_isam_oid(card->dir_isam) == 109 && card->dir_kid == 1 && card->shell_iteration == 3);

    check("purse deposit VAT 20%", card->products[0].deposit_vat == 2000);
    check(
        "purse tops up from another purse",
        itso_product_purse(&card->products[0])->auto_top_up_internal);

    {
        const ItsoProduct* id16 = &card->products[1];
        char lang[3];
        check("ID CPICC", id16->has_cpicc && id16->cpicc == 0x9100);
        check(
            "ID language is Welsh",
            itso_product_id(id16)->language == 182 && itso_language_code(182, lang) &&
                strcmp(lang, "cy") == 0 && strcmp(itso_language_name(182), "Welsh") == 0);
        check(
            "ITSO language 44 is English",
            itso_language_code(44, lang) && strcmp(lang, "en") == 0);
        check(
            "the misprinted language 71 reads as Igbo",
            itso_language_code(71, lang) && strcmp(lang, "ig") == 0);
        check("language 0 is not a language", !itso_language_code(0, lang));
        check(
            "ID holder ID",
            itso_product_id(id16)->has_holder_id && itso_product_id(id16)->holder_id == 4078);
        check(
            "ID secondary holder",
            itso_product_id(id16)->has_secondary_holder &&
                itso_product_id(id16)->secondary_holder_id == 1234567);
        check(
            "names still found after the secondary holder",
            strcmp(itso_product_id(id16)->name, "ALEX MORGAN") == 0);
        check(
            "rounding enabled, flag set, value flag clear",
            itso_product_id(id16)->rounding == (ITSO_ROUNDING_ENABLED | ITSO_ROUNDING_FLAG));
        check(
            "ID deposit GBP 5.00 cash",
            id16->has_deposit && id16->deposit.value == 500 && id16->deposit_mop == 1);
        check(
            "ID shell deposit GBP 3.00 by card at 20%",
            itso_product_id(id16)->has_shell_deposit &&
                itso_product_id(id16)->shell_deposit.value == 300 &&
                itso_product_id(id16)->shell_deposit_mop == 3 &&
                itso_product_id(id16)->shell_deposit_vat == 2000);
    }

    {
        const ItsoProduct* j = &card->products[3];
        const ItsoTicketTerms* t = itso_product_ticket(j);
        check("journey terms read", t->valid);
        check(
            "journey issued 2026-09-14",
            strcmp(fmt_unix(itso_date_to_unix(t->issue_date)), "2026-09-14 00:00") == 0);
        check(
            "journey validity code and end time",
            t->validity_code == 25 && t->expiry_time == 1440 + 270);
        check(
            "journey standard class, adult and child",
            t->travel_class == 2 && t->adults == 1 && t->children == 1);
        check(
            "journey paid GBP 5.80 by card",
            t->amount_paid.valid && t->amount_paid.value == 580 && t->paid_mop == 3);
        check(
            "journey photocard, promotion and CPICC",
            t->photocard == 987654 && t->promotion_code == 7 && j->has_cpicc && j->cpicc == 0x12);
        check(
            "journey mode group",
            t->has_mode_group && t->mode == ItsoJourneyModeStoredJourneys &&
                t->max_transfers == 2 && t->time_limit == 120 && t->unit_value.valid &&
                t->unit_value.value == 250);
        check(
            "journey locations still land after the terms",
            j->from.valid && strcmp(loc_text(&j->from), "Station 5631") == 0);
        check(
            "journey RouteCode, revision 2",
            t->has_route_code && memcmp(t->route_code, "00000", 5) == 0);
        check(
            "period RouteCode, revision 3",
            itso_product_ticket(&card->products[2])->has_route_code &&
                memcmp(itso_product_ticket(&card->products[2])->route_code, "00000", 5) == 0);
        check(
            "a period ticket without bitmap bit 2 has no identity document",
            !itso_product_ticket(&card->products[2])->has_id_doc);
        check(
            "the loyalty scheme's own two bytes",
            card->products[4].has_owner_data && card->products[4].owner_data == 4660);
        check(
            "an ID defines PrintTicket and this one leaves it clear",
            card->products[1].print_defined == ITSO_PRINT_TICKET &&
                card->products[1].print_flags == 0);
    }

    {
        /* The taps are newest first: [0] the tap out, [1] the tap in. */
        const ItsoTap* out = &card->taps[0];
        const ItsoTap* in = NULL;
        for(uint8_t i = 0; i < card->tap_count; i++) {
            if(card->taps[i].transaction_type == 11 && card->taps[i].format_rev == 2)
                in = &card->taps[i];
        }
        check(
            "tap out names its reader's operator",
            out && out->has_writer && itso_isam_oid(out->writer_isam) == 9000);
        check(
            "tap out was a return with a companion", out && out->companion && out->return_ticket);
        check(
            "tap in names its reader's operator",
            in && in->has_writer && itso_isam_oid(in->writer_isam) == 109);
        check("tap in carried no companion", in && !in->companion && !in->return_ticket);
    }

    /* Complex capping, both forms. */
    for(int ref = 1; ref <= 2; ref++) {
        const uint8_t* src = ref == 1 ? capping1_group : capping2_group;
        size_t len = ref == 1 ? sizeof(capping1_group) : sizeof(capping2_group);
        uint8_t* buf = malloc(len);
        memcpy(buf, src, len);
        ItsoProduct p;
        memset(&p, 0, sizeof(p));
        p.typ = ItsoTypStoredTravelRights;
        p.value_group = true;
        itso_parse_ipe(&p, buf, len, 64);
        ItsoCapping* cap = malloc(sizeof(ItsoCapping));
        bool ok = itso_parse_capping(buf, len, 64, 0, cap);
        printf(
            "  VGXRef %d: strategy %u, day %ld, 7-day %ld after %u days\n",
            ref,
            cap->strategy,
            (long)cap->acc[0].day.value,
            (long)cap->acc[1].multiday.value,
            cap->acc[1].day_count);
        check(
            ref == 1 ? "reduced capping extension flagged" : "full capping extension flagged",
            p.vgx_ref == ref);
        check(
            "the extension leaves the balance alone",
            itso_product_purse(&p)->balance.valid &&
                itso_product_purse(&p)->balance.value == 1375);
        check("capping decoded", ok && cap->valid && cap->ref == ref && cap->strategy == 7);
        check(
            "day cap accumulator",
            cap->acc[0].rule == ItsoCapRuleDay && cap->acc[0].uncapped.value == 900 &&
                cap->acc[0].day.value == 700 && cap->acc[0].last_txn == 11);
        check(
            "multi-day accumulator",
            cap->acc[1].rule == ItsoCapRuleShortPeriod && cap->acc[1].multiday.value == 2500 &&
                cap->acc[1].day_count == 3);
        check("unused accumulators are empty", cap->acc[2].rule == ItsoCapRuleNone);
        check(
            "where the last cap applied",
            cap->acc[0].location.valid &&
                strcmp(loc_text(&cap->acc[0].location), "Station 1072") == 0);
        if(ref == 2) {
            check("full form keeps the last fare", cap->acc[0].last_fare.value == 185);
            check(
                "full form keeps when the cap applied",
                strcmp(fmt_unix(itso_dts_to_unix(cap->acc[0].cap_dts)), "2026-09-14 08:41") == 0);
            check("null locations stay absent", !cap->acc[1].location.valid);
        }
        /* A group cut short anywhere must fail cleanly, never over-read: each
         * truncation is its own exactly-sized allocation, so ASan sees any byte
         * read past the end. */
        for(size_t cut = 0; cut < len; cut++) {
            uint8_t* part = malloc(cut ? cut : 1);
            memcpy(part, buf, cut);
            itso_parse_capping(part, cut, 64, 0, cap);
            free(part);
        }
        check("capping survives every truncation", true);
        free(cap);
        free(buf);
        itso_product_free(&p);
    }
    {
        ItsoCapping cap;
        check(
            "a purse with no extension has no capping",
            !itso_parse_capping(capping1_group, 64, 64, 0, &cap) && !cap.valid);
    }
}
