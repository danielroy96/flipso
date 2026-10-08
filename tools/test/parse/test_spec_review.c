/**
 * @file test_spec_review.c
 * @brief Fields added from a review of TS 1000-5, each against a built group.
 */
#include "test_parse.h"

/*
 * Elements a review of Flipso against TS 1000-5 (2026-09-29) found missing or
 * misread: each is a case the decoder got wrong or left out.
 */
void spec_review_fields(void) {
    ItsoProduct p = {0};

    /* Limits, deposits and prices are VALI - unsigned (TS 1000-1 table 5) - so
     * GBP 400 is 40000 pence, not a negative number. A purse's balance is the
     * one amount that is signed. */
    {
        uint8_t purse[24] = {0x18, 0x01, 0xFF}; /* six blocks, revision 1 */
        purse[10] = 0x9C, purse[11] = 0x40; /* MaxValue2: 40000 */
        purse[14] = 0x9C, purse[15] = 0x40; /* DepositAmount: 40000 */
        parse_group(&p, ItsoTypStoredTravelRights, false, purse, sizeof(purse));
        check(
            "a purse limit over GBP 327.67 is not negative",
            itso_product_purse(&p)->max_value.valid &&
                itso_product_purse(&p)->max_value.value == 40000 && p.deposit.value == 40000);
        uint8_t account[20] = {0x14, 0x01, 0xFF}; /* five blocks, revision 1 */
        account[6] = 0x9C, account[7] = 0x40; /* MaxValue4: 40000 */
        parse_group(&p, ItsoTypChargeToAccount1, false, account, sizeof(account));
        check("nor is a charge-to-account's", itso_product_purse(&p)->max_value.value == 40000);
    }

    /* A ValueCurrencyCode's scaling applies to the limits it prices, not only
     * to the balance (TS 1000-5 table 2, annex A.21.2). */
    parse_group(
        &p, ItsoTypStoredTravelRights, true, purse_scaled_group, sizeof(purse_scaled_group));
    check("a scaled purse balance", itso_product_purse(&p)->balance.value == 12340);
    check(
        "the purse limits scale with it",
        itso_product_purse(&p)->max_value.value == 90000 &&
            itso_product_purse(&p)->max_negative.value == 2000 &&
            itso_product_purse(&p)->top_up_threshold.value == 5000 &&
            itso_product_purse(&p)->top_up_amount.value == 10000);
    check("the deposit keeps its own currency code", p.deposit.value == 500);
    check(
        "TYP2Flags print ticket and receipt",
        p.print_defined == (ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT) &&
            p.print_flags == (ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT));

    parse_group(&p, ItsoTypChargeToAccount1, true, charge1_group, sizeof(charge1_group));
    check(
        "TYP 4 spend scales",
        itso_product_purse(&p)->balance.value == 1230 && itso_product_purse(&p)->balance_is_spend);
    check("TYP 4 credit limit scales with it", itso_product_purse(&p)->max_value.value == 50000);
    check(
        "TYP 4 deposit has its VAT",
        p.deposit.value == 1500 && p.deposit_mop == 3 && p.deposit_vat == 1750);
    check("TYP 4 prints a ticket and no receipt", p.print_flags == ITSO_PRINT_TICKET);
    check("TYP 4 priority flag", itso_product_purse(&p)->priority_override);

    parse_group(&p, ItsoTypChargeToAccount2, true, charge2_group, sizeof(charge2_group));
    check(
        "MaxValue5 is priced in the value record's currency",
        itso_product_purse(&p)->max_value.value == 25000 &&
            itso_product_purse(&p)->max_value.currency == 1);
    check("TYP 5 deposit has its VAT", p.deposit.value == 800 && p.deposit_vat == 500);
    check("TYP5ValueFlags priority override", itso_product_purse(&p)->priority_override);
    check("TYP 5 prints a receipt only", p.print_flags == ITSO_PRINT_RECEIPT);
    check(
        "TYP 5 still counts transactions",
        p.count == 3 && itso_product_purse(&p)->journey_legs == 1);

    /* TYP 14 carries CPICC, a HolderID, rounding and a deposit as TYP 16 does,
     * at offsets of its own (tables 20 and 20a). */
    parse_group(
        &p, ItsoTypEntitlement, false, entitlement_rev1_group, sizeof(entitlement_rev1_group));
    check("TYP 14 rev 1 CPICC", p.has_cpicc && p.cpicc == 0x0321);
    check(
        "TYP 14 rev 1 HolderID",
        itso_product_id(&p)->has_holder_id && itso_product_id(&p)->holder_id == 55501);
    check(
        "TYP 14 rev 1 rounding up to 5p",
        itso_product_id(&p)->rounding ==
            (ITSO_ROUNDING_ENABLED | ITSO_ROUNDING_FLAG | ITSO_ROUNDING_VALUE));
    check(
        "TYP 14 rev 1 deposit",
        p.has_deposit && p.deposit.value == 250 && p.deposit_mop == 1 && p.deposit_vat == 0);
    check(
        "TYP 14 rev 1 dates and entitlement still in place",
        itso_product_id(&p)->entitlement_code == 14 &&
            itso_product_id(&p)->concession_class == 5 && !p.has_start &&
            strcmp(
                fmt_unix(itso_date_to_unix(itso_product_id(&p)->sub_expiry)),
                "2027-08-31 00:00") == 0 &&
            p.passback == 10);
    check(
        "an entitlement's IDFlags print ticket",
        p.print_defined == ITSO_PRINT_TICKET && p.print_flags == ITSO_PRINT_TICKET);
    check(
        "an entitlement has no language",
        itso_product_id(&p)->language == 0 && !itso_product_id(&p)->has_shell_deposit);

    parse_group(
        &p, ItsoTypEntitlement, false, entitlement_rev2_group, sizeof(entitlement_rev2_group));
    check("TYP 14 rev 2 CPICC", p.cpicc == 0x0654);
    check("TYP 14 rev 2 HolderID", itso_product_id(&p)->holder_id == 0x00ABCDEF);
    check(
        "TYP 14 rev 2 rounding down to 5p",
        itso_product_id(&p)->rounding == (ITSO_ROUNDING_ENABLED | ITSO_ROUNDING_VALUE));
    check(
        "TYP 14 rev 2 deposit by card at 20%",
        p.deposit.value == 1000 && p.deposit_mop == 3 && p.deposit_vat == 2000);
    check(
        "TYP 14 rev 2 entitlement",
        p.has_start && itso_product_id(&p)->entitlement_code == 11 &&
            itso_product_id(&p)->concession_class == 4);
    check("no print flag set", p.print_flags == 0);

    /* TYP 23 revision 3: mode 3, and a ride value in its own currency. */
    parse_group(&p, ItsoTypJourneyTicket, false, journey_rev3_group, sizeof(journey_rev3_group));
    const ItsoTicketTerms* t = itso_product_ticket(&p);
    check("a rev 3 return", t->has_mode_group && t->mode == ItsoJourneyModeReturn);
    check(
        "its legs within 45 min 30 s and one change",
        t->time_limit == 91 && t->max_transfers == 1);
    check(
        "the ride value takes its own currency code",
        t->unit_value.value == 6000 && t->unit_value.currency == 1);
    check(
        "and the price paid its own",
        t->amount_paid.value == 1200 && t->amount_paid.currency == 0);
    check("a rev 3 RouteCode", t->has_route_code && memcmp(t->route_code, "00700", 5) == 0);
    check(
        "a rev 3 journey's ends land after the route",
        strcmp(loc_text(&p.from), "Station 1072") == 0 &&
            strcmp(loc_text(&p.to), "Station 1444") == 0);
    check("TYP23Flags print receipt", p.print_flags == ITSO_PRINT_RECEIPT);
    check("AutoRenewQuantity", t->renew_quantity == 2);

    /* TYP 25: the voucher's dataset, and uses rather than rides. */
    parse_group(&p, ItsoTypVoucher, true, voucher_group, sizeof(voucher_group));
    t = itso_product_ticket(&p);
    check("a voucher's terms are read", t->valid);
    check("TYP25Flags print ticket", p.print_flags == ITSO_PRINT_TICKET);
    check("a voucher's PassbackTime", p.has_passback && p.passback == 15);
    check(
        "a voucher's IssueDate",
        strcmp(fmt_unix(itso_date_to_unix(t->issue_date)), "2026-09-01 00:00") == 0);
    check(
        "a voucher's ValidityStartDTS",
        strcmp(fmt_unix(itso_dts_to_unix(t->valid_from_dts)), "2026-09-02 07:30") == 0);
    check("a voucher's ExpiryTime runs past midnight", t->expiry_time == 1500);
    check("ServiceID and UserDefined", t->service_id == 42 && t->user_defined == 7);
    check(
        "MaxValue25 takes its own currency code",
        t->unit_value.valid && t->unit_value.value == 800 && t->unit_value.currency == 1);
    check(
        "and the price paid its own, by card at 20%",
        t->amount_paid.value == 2500 && t->amount_paid.currency == 0 && t->paid_mop == 3 &&
            t->vat == 2000);
    check("AutoRenewQuantity2", t->renew_quantity == 5);
    check(
        "uses left, auto-renewing", p.count_kind == ItsoCountUses && p.count == 4 && p.auto_renew);
    check("Uses left", strcmp(itso_count_name(p.count_kind), "Uses left") == 0);

    /* TYP 26: the toll pass's dataset, its class kept apart from EN1545's. */
    parse_group(&p, ItsoTypTolling, true, tolling_group, sizeof(tolling_group));
    t = itso_product_ticket(&p);
    check("a toll pass's terms are read", t->valid);
    check("a toll pass is a ticket", itso_product_family(ItsoTypTolling) == ItsoFamilyTicket);
    check("its ProductRetailer", p.has_retailer && p.retailer == 0x0456);
    check("its PassbackTime, two bits early", p.has_passback && p.passback == 45);
    check(
        "TYP26Flags print receipt",
        p.print_defined == (ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT) &&
            p.print_flags == ITSO_PRINT_RECEIPT);
    check("TYP26Class", t->vehicle_class == 3);
    check("and no EN1545 class", t->travel_class == 0);
    check(
        "a toll pass's IssueDate",
        strcmp(fmt_unix(itso_date_to_unix(t->issue_date)), "2026-08-03 00:00") == 0);
    check(
        "a toll pass's ValidityStartDTS",
        strcmp(fmt_unix(itso_dts_to_unix(t->valid_from_dts)), "2026-08-04 06:00") == 0);
    check("no ExpiryTime and no price", t->expiry_time == 0 && !t->amount_paid.valid);
    check("AutoRenewQuantity3", t->renew_quantity == 20);
    check(
        "crossings left, auto-renewing",
        p.count_kind == ItsoCountCrossings && p.count == 19 && p.auto_renew);
    check("Crossings left", strcmp(itso_count_name(p.count_kind), "Crossings left") == 0);
    {
        static const uint8_t expect[ITSO_TOLL_USER_DATA_LEN] = {
            0x00, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
        uint8_t ud[ITSO_TOLL_USER_DATA_LEN];
        check(
            "a toll pass's UserDefined",
            itso_toll_user_data(tolling_group, sizeof(tolling_group), ud) &&
                memcmp(ud, expect, sizeof(ud)) == 0);
        /* Each cut an exactly sized allocation, so ASan sees a read past it;
         * only a cut that keeps the whole dataset may find the bytes. */
        bool bounded = true;
        for(size_t cut = 0; cut < 24; cut++) {
            uint8_t* exact = malloc(cut ? cut : 1);
            memcpy(exact, tolling_group, cut);
            bounded = bounded && !itso_toll_user_data(exact, cut, ud);
            free(exact);
        }
        check("not past the end of a short dataset", bounded);
    }
    {
        /* Bitmap bit 1 clear: no renewal quantity, whatever byte 20 holds. */
        uint8_t bare[sizeof(tolling_group)];
        memcpy(bare, tolling_group, sizeof(bare));
        bare[1] &= ~0x20; /* IPEBitMap is header bits 6 to 11, bit 1 at bit 10 */
        parse_group(&p, ItsoTypTolling, true, bare, sizeof(bare));
        check(
            "AutoRenewQuantity3 only with its bitmap bit",
            itso_product_ticket(&p)->valid && itso_product_ticket(&p)->renew_quantity == 0);
    }

    /* TYP 22 revision 3 IdentityDocumentID, after the route and locations. */
    parse_group(
        &p, ItsoTypPeriodTicket, false, period_rev3_id_group, sizeof(period_rev3_id_group));
    check(
        "a period ticket's identity document, as text",
        itso_product_ticket(&p)->has_id_doc &&
            itso_product_ticket(&p)->id_doc_type == ItsoIdDocAscii &&
            itso_product_ticket(&p)->id_doc_len == 8 &&
            memcmp(itso_product_ticket(&p)->id_doc, "RC123456", 8) == 0);
    check("with the route before it", itso_product_ticket(&p)->has_route_code && p.to.valid);
    check(
        "TYP22Flags print ticket and keep expired passes",
        p.print_flags == ITSO_PRINT_TICKET &&
            (itso_product_ticket(&p)->flags & ITSO_T22_KEEP_EXPIRED));

    parse_group(
        &p,
        ItsoTypPeriodTicket,
        false,
        period_rev3_long_id_group,
        sizeof(period_rev3_long_id_group));
    check(
        "a long identity document keeps its length and what fits",
        itso_product_ticket(&p)->has_id_doc &&
            itso_product_ticket(&p)->id_doc_type == ItsoIdDocHex &&
            itso_product_ticket(&p)->id_doc_len == 20 && itso_product_ticket(&p)->id_doc[0] == 1 &&
            itso_product_ticket(&p)->id_doc[ITSO_ID_DOC_LEN - 1] == ITSO_ID_DOC_LEN);
    check("and has no route", !itso_product_ticket(&p)->has_route_code && !p.from.valid);

    /* Every truncation, each an exactly sized allocation, so ASan sees any
     * read past the end of the new elements. */
    const struct {
        uint8_t typ;
        bool vgp;
        const uint8_t* group;
        size_t len;
    } all[] = {
        {ItsoTypStoredTravelRights, true, purse_scaled_group, sizeof(purse_scaled_group)},
        {ItsoTypChargeToAccount1, true, charge1_group, sizeof(charge1_group)},
        {ItsoTypChargeToAccount2, true, charge2_group, sizeof(charge2_group)},
        {ItsoTypEntitlement, false, entitlement_rev1_group, sizeof(entitlement_rev1_group)},
        {ItsoTypEntitlement, false, entitlement_rev2_group, sizeof(entitlement_rev2_group)},
        {ItsoTypJourneyTicket, false, journey_rev3_group, sizeof(journey_rev3_group)},
        {ItsoTypVoucher, true, voucher_group, sizeof(voucher_group)},
        {ItsoTypTolling, true, tolling_group, sizeof(tolling_group)},
        {ItsoTypPeriodTicket, false, period_rev3_id_group, sizeof(period_rev3_id_group)},
        {ItsoTypPeriodTicket, false, period_rev3_long_id_group, sizeof(period_rev3_long_id_group)},
    };
    for(size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        for(size_t cut = 1; cut <= all[i].len; cut++) {
            parse_group(&p, all[i].typ, all[i].vgp, all[i].group, cut);
        }
    }
    check("the new elements survive every truncation", 1);
    itso_product_free(&p);
}

/*
 * What each bit of an IPEBitMap says is present, from the type's own table in
 * TS 1000-5: the Technical page names them rather than printing the byte.
 */
void bitmap_names(void) {
    check(
        "bit 0 is the owner's network on a full IPE",
        strcmp(itso_bitmap_element_name(ItsoTypStoredTravelRights, 1, 0), "Owner network") == 0);
    check(
        "and RFU on a Space Saving one (table 48a)",
        itso_bitmap_element_name(ItsoTypPeriodCompact, 1, 0) == NULL);
    check(
        "a purse defines no other bit (table 3)",
        itso_bitmap_element_name(ItsoTypStoredTravelRights, 1, 1) == NULL);
    check(
        "a revision 1 period ticket gives each end a bit (table 28)",
        strcmp(itso_bitmap_element_name(ItsoTypPeriodTicket, 1, 1), "Origin") == 0 &&
            strcmp(itso_bitmap_element_name(ItsoTypPeriodTicket, 1, 2), "Destination") == 0);
    check(
        "revision 2 puts both ends and the route under bit 1, and leaves bit 2 RFU (28a)",
        strcmp(itso_bitmap_element_name(ItsoTypPeriodTicket, 2, 1), "Locations and route") == 0 &&
            itso_bitmap_element_name(ItsoTypPeriodTicket, 2, 2) == NULL);
    check(
        "revision 3 gives bit 2 to an ID document (3.28)",
        strcmp(itso_bitmap_element_name(ItsoTypPeriodTicket, 3, 2), "ID document") == 0);
    check(
        "a revision 1 journey ticket has its destination before its origin (table 32)",
        strcmp(itso_bitmap_element_name(ItsoTypJourneyTicket, 1, 1), "Destination") == 0 &&
            strcmp(itso_bitmap_element_name(ItsoTypJourneyTicket, 1, 2), "Origin") == 0);
    check(
        "an ID's name is bit 2 (table 23), an entitlement's bit 2 its days (table 21)",
        strcmp(itso_bitmap_element_name(ItsoTypId, 2, 2), "Name") == 0 &&
            strcmp(itso_bitmap_element_name(ItsoTypEntitlement, 2, 2), "Valid days and area") ==
                0);
    check(
        "a multi-use ticket's backup is bit 3, its sequence number bit 4 (58a)",
        strcmp(itso_bitmap_element_name(ItsoTypMultiUse, 1, 3), "Backup count") == 0 &&
            strcmp(itso_bitmap_element_name(ItsoTypMultiUse, 1, 4), "Sequence number") == 0);
    check(
        "bit 5 is RFU everywhere",
        itso_bitmap_element_name(ItsoTypReservationTicket, 2, 5) == NULL);
}
