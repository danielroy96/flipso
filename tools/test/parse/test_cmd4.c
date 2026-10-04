/**
 * @file test_cmd4.c
 * @brief CMD4 paper tickets: the compact shell and the Space Saving IPEs.
 */
#include "test_parse.h"

/*
 * The Compact ITSO Shell of a Type 2 tag (TS 1000-10 section 5, table 42): three
 * stored bytes, everything else implied by the CMD. What is checked is that it is
 * recognised, expanded to the fixed identity and geometry, and kept distinct from
 * a full shell in both directions.
 */
void compact_shell(void) {
    static ItsoCard card;
    itso_card_reset(&card);

    check(
        "compact shell looks like a shell", itso_looks_like_shell(cmd4_shell, sizeof(cmd4_shell)));
    check("compact shell parses", itso_parse_shell(&card, cmd4_shell, sizeof(cmd4_shell)));
    check("compact shell reports accepted", card.shell_reject == ItsoShellAccepted);
    check("compact shell is flagged compact", card.shell_compact && card.shell_valid);
    check("compact FVC is 4", card.fvc == 4 && card.format_rev == 1 && card.shell_len == 6);
    check("compact implied identity", card.iin == 633597 && card.oid == 8189);
    check(
        "compact implied geometry",
        card.sector_size == 32 && card.sector_count == 1 && card.dir_entries == 1 &&
            card.sct_len == 0);
    check("compact ISRN", strcmp(card.isrn, EXPECT_CMD4_ISRN) == 0 && card.isrn_check_ok);

    char number[ITSO_ISRN_DIGITS + 1];
    check(
        "compact card number read directly",
        itso_shell_card_number(cmd4_shell, sizeof(cmd4_shell), number) &&
            strcmp(number, EXPECT_CMD4_ISRN) == 0);

    /* Truncations must not read past the end, and a compact shell has no SECRC. */
    for(size_t len = 0; len <= sizeof(cmd4_shell); len++) {
        itso_card_reset(&card);
        itso_parse_shell(&card, cmd4_shell, len);
    }
    itso_card_reset(&card);
    itso_parse_shell(&card, cmd4_shell, sizeof(cmd4_shell));
    check("compact shell has no checksum", !card.secrc_checked);

    /* A full shell must not be taken for a compact one. */
    itso_card_reset(&card);
    check(
        "full shell not flagged compact",
        itso_parse_shell(&card, card_shell, sizeof(card_shell)) && !card.shell_compact);

    /* The whole Type 2 tag: the page memory decodes to the compact shell plus its
     * single directory entry, at the fixed offsets of TS 1000-10 table 46. */
    itso_card_reset(&card);
    check("Type 2 pages decode", itso_parse_type2(&card, cmd4_pages, sizeof(cmd4_pages)));
    check("Type 2 shell is compact", card.shell_compact && card.shell_valid);
    check("Type 2 has one product", card.product_count == 1);
    check(
        "Type 2 product is an SPT period ticket",
        card.products[0].oid == 8323 && card.products[0].oid_extended &&
            card.products[0].typ == 27 && card.products[0].dir_index == 1);
    /* No Sector Chain Table, so no status is claimed - only a zero Seal blocks. */
    check(
        "an unblocked Type 2 product claims no status",
        card.products[0].status == ItsoProductStatusUnknown);
    static const uint8_t uid[7] = {0x04, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7};
    check(
        "Type 2 keeps its chip UID, less BCC0",
        card.chip_uid_valid && memcmp(card.chip_uid, uid, sizeof(uid)) == 0);
    /* The lock bytes in page 2: pages 6-13, as TS 1000-10 clause 5.10.2 has an
     * issued CMD4 lock them, and nothing frozen. */
    check("Type 2 records the memory it read", card.chip_memory_len == 64);
    check(
        "Type 2 lock bits lock pages 6-13",
        itso_type2_locked_pages(card.chip_lock) == ITSO_CMD4_LOCKED_PAGES);
    check("Type 2 lock bits are not frozen", itso_type2_frozen_pages(card.chip_lock) == 0);
    /* The Ultralight layout bit by bit: byte 0 bits 3-7 lock pages 3-7, byte 1
     * bits 0-7 lock pages 8-15, byte 0 bits 0-2 freeze page 3, 4-9 and 10-15. */
    static const uint8_t otp_only[2] = {0x08, 0x00};
    static const uint8_t page15[2] = {0x00, 0x80};
    static const uint8_t frozen[2] = {0x07, 0x00};
    check("lock byte 0 bit 3 locks page 3", itso_type2_locked_pages(otp_only) == (1u << 3));
    check("lock byte 1 bit 7 locks page 15", itso_type2_locked_pages(page15) == (1u << 15));
    check(
        "block-lock bits freeze pages 3-15 and lock none",
        itso_type2_frozen_pages(frozen) == 0xFFF8 && itso_type2_locked_pages(frozen) == 0);
    /* The InstanceID at pages 8-9: an ISAM registered to SPT's OID 8323. */
    check(
        "Type 2 InstanceID names the ISAM that sold it",
        card.products[0].instance_valid && card.products[0].isam_id == 0x041C0099 &&
            itso_isam_oid(card.products[0].isam_id) == 8323 &&
            card.products[0].isam_seq == 0x001234);

    /* The TYP 27 dataset itself, spread across the static, dynamic and OTP page
     * regions, decodes to the fields a real day ticket carries. */
    const ItsoProduct* day = &card.products[0];
    check("TYP 27 is a decoded space-saving IPE", day->space_saving && day->body_parsed);
    check(
        "TYP 27 price is GBP 4.45",
        itso_product_ticket(day)->amount_paid.valid &&
            itso_product_ticket(day)->amount_paid.value == 445 &&
            itso_product_ticket(day)->amount_paid.currency == 0);
    check(
        "TYP 27 is an adult ticket",
        itso_product_ticket(day)->adults == 1 && itso_product_ticket(day)->children == 0);
    check(
        "TYP 27 carries reference fare code 0",
        card.space->area_kind == ItsoAreaFareCode && card.space->area_value == 0);
    check(
        "TYP 27 flags: owner expiry time, all-day, standard",
        (card.space->flags & ITSO_SS_EXPIRY_TIME) && !(card.space->flags & ITSO_SS_OFF_PEAK) &&
            !(card.space->flags & ITSO_SS_FIRST_CLASS));
    check("TYP 27 keeps its passback time", day->has_passback && day->passback == 7);
    check("TYP 27 records a last use", card.space->has_last_use && card.space->last_use_dts != 0);
    check(
        "TYP 27 keeps both event codes",
        card.space->has_events && card.space->event1 == 0 && card.space->event2 == 12);

    /* The compact shell's OID is the generic 8189, so the ticket's issuer - the
     * operator that titles it - is its product's owner. A full shell's is its own. */
    check("a compact ticket is issued by its product owner", itso_card_issuer_oid(&card) == 8323);
    static ItsoCard full;
    itso_card_reset(&full);
    itso_parse_shell(&full, card_shell, sizeof(card_shell));
    check("a full shell is issued by its shell owner", itso_card_issuer_oid(&full) == full.oid);

    /* Truncations must not read past the end - and are not a ticket at all: a
     * read that stopped short is a failed read, however much of it looks fine. */
    bool short_rejected = true;
    for(size_t len = 0; len <= sizeof(cmd4_pages); len++) {
        uint8_t* exact = malloc(len ? len : 1);
        memcpy(exact, cmd4_pages, len);
        itso_card_reset(&card);
        bool parsed = itso_parse_type2(&card, exact, len);
        if(len < ITSO_CMD4_MEMORY_LEN &&
           (parsed || itso_type2_kind(exact, len) != ItsoType2Incomplete)) {
            short_rejected = false;
        }
        free(exact);
    }
    check("a Type 2 read short of 64 bytes is incomplete, not a ticket", short_rejected);
    check(
        "a whole CMD4 is a compact ticket",
        itso_type2_kind(cmd4_pages, sizeof(cmd4_pages)) == ItsoType2Compact);
    check(
        "a rotated full shell from page 4 is a CMD9 or CMD10",
        itso_type2_kind(cmd9_pages, sizeof(cmd9_pages)) == ItsoType2FullShell);
    itso_card_reset(&card);
    check(
        "and is not decoded as a CMD4", !itso_parse_type2(&card, cmd9_pages, sizeof(cmd9_pages)));
    /* Page memory with no compact shell at page 6 is rejected, not misread. */
    uint8_t* blank_pages = calloc(1, sizeof(cmd4_pages));
    itso_card_reset(&card);
    check(
        "Type 2 without a shell is rejected",
        !itso_parse_type2(&card, blank_pages, sizeof(cmd4_pages)) &&
            itso_type2_kind(blank_pages, sizeof(cmd4_pages)) == ItsoType2NotItso);
    free(blank_pages);

    /* The SPT Subway station table: complete (15 stations), 1-based, and safe at
     * its edges. Names are the scheme's own, not NLCs. */
    check("SPT station 1 is Govan", strcmp(itso_spt_subway_station(1), "Govan") == 0);
    check("SPT station 4 is Hillhead", strcmp(itso_spt_subway_station(4), "Hillhead") == 0);
    check("SPT station 9 is St Enoch", strcmp(itso_spt_subway_station(9), "St Enoch") == 0);
    check("SPT station 15 is Ibrox", strcmp(itso_spt_subway_station(15), "Ibrox") == 0);
    check("SPT station 0 is out of range", itso_spt_subway_station(0) == NULL);
    check("SPT station 16 is out of range", itso_spt_subway_station(16) == NULL);
}

/* Decode @p pages from an exact-length heap copy, so an over-read is caught. */
static void parse_type2_exact(ItsoCard* card, const uint8_t* pages, size_t len) {
    uint8_t* exact = malloc(len ? len : 1);
    memcpy(exact, pages, len);
    itso_card_reset(card);
    itso_parse_type2(card, exact, len);
    free(exact);
}

/*
 * The Space Saving IPEs beyond TYP 27 (TS 1000-5 clauses 2.15-2.16). The return
 * is built in the shape of a real SPT Subway ticket from Ryan Murphy's dump, and
 * the builder reproduces his bytes; the carnet and multi-leg ticket follow the
 * spec alone.
 */
void space_saving_types(void) {
    static ItsoCard card;

    /* TYP 29 revision 1: a carnet of two rides with one left. */
    parse_type2_exact(&card, cmd4_return, sizeof(cmd4_return));
    const ItsoProduct* ret = &card.products[0];
    check("TYP 29 return decodes", ret->typ == 29 && ret->space_saving && ret->body_parsed);
    check("TYP 29 return has one ride left", ret->count_kind == ItsoCountRides && ret->count == 1);
    check("TYP 29 return cost GBP 3.30", itso_product_ticket(ret)->amount_paid.value == 330);
    check(
        "TYP 29 return last used getting off at stage 4",
        ret->from.valid && ret->from.def_type == 202 && card.space->usage_alighted);
    check(
        "an SPT fare stage is named as its station",
        strcmp(loc_text(&ret->from), "Hillhead") == 0);
    check("TYP 29 revision 1 has no passback", !ret->has_passback);

    /* TYP 28: two passes used, two ticks left and the expiry-day pass, two
     * never sold. */
    parse_type2_exact(&card, cmd4_carnet, sizeof(cmd4_carnet));
    const ItsoProduct* carnet = &card.products[0];
    check("TYP 28 carnet decodes", carnet->typ == 28 && carnet->space_saving);
    check(
        "TYP 28 counts the expiry-day pass among those left",
        carnet->count_kind == ItsoCountPasses && carnet->count == 3);
    check(
        "TYP 28 keeps the days its passes were used",
        card.space->carnet_ticks[0] == 20 && card.space->carnet_ticks[1] == 10 &&
            card.space->carnet_ticks[4] == 31);
    check(
        "TYP 28 day-of-issue and day-of-expiry flags",
        card.space->carnet_issue_day && card.space->carnet_expiry_day);
    check("TYP 28 is off-peak only", card.space->flags & ITSO_SS_OFF_PEAK);
    check("TYP 28 cost GBP 20.00", itso_product_ticket(carnet)->amount_paid.value == 2000);
    check("TYP 28 keeps its passback", carnet->has_passback && carnet->passback == 5);

    /* TYP 29 revision 2: multi-leg journeys, which carry no price. */
    parse_type2_exact(&card, cmd4_multileg, sizeof(cmd4_multileg));
    const ItsoProduct* legs = &card.products[0];
    check("TYP 29 revision 2 decodes", legs->format_rev == 2 && legs->space_saving);
    check("TYP 29 revision 2 has seven journeys left", legs->count == 7);
    check(
        "TYP 29 revision 2 journey counters",
        card.space->daily_journeys == 2 && card.space->max_daily_journeys == 4 &&
            card.space->transfers == 1 && itso_product_ticket(legs)->max_transfers == 2);
    check("TYP 29 revision 2 records when the journey began", card.space->journey_start_dts != 0);
    check("TYP 29 revision 2 carries no price", !itso_product_ticket(legs)->amount_paid.valid);
    check("TYP 29 revision 2 has no usage place", !legs->from.valid);

    /* Every truncation of every layout, from exact-length copies. */
    const uint8_t* all[] = {cmd4_return, cmd4_carnet, cmd4_multileg};
    for(size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        for(size_t len = 0; len <= sizeof(cmd4_return); len++) {
            parse_type2_exact(&card, all[i], len);
        }
    }
    check("truncated Space Saving IPEs survive", 1);

    /* An unknown revision is left as a directory-entry-only product. */
    uint8_t* odd = malloc(sizeof(cmd4_return));
    memcpy(odd, cmd4_return, sizeof(cmd4_return));
    odd[41] = (uint8_t)((odd[41] & 0xF0) | 0x09); /* IPEFormatRevision 9 */
    parse_type2_exact(&card, odd, sizeof(cmd4_return));
    check("an unknown revision is not decoded", !card.products[0].body_parsed);
    free(odd);

    /* TS 1000-10 clause 5.16: a zero Seal is a blocked product. */
    parse_type2_exact(&card, cmd4_blocked, sizeof(cmd4_blocked));
    check(
        "a CMD4 product with a zero Seal is blocked",
        card.products[0].status == ItsoProductStatusBlocked);

    /* Only TYP 27, 28 and 29 have a Space Saving layout. */
    parse_type2_exact(&card, cmd4_wrong_type, sizeof(cmd4_wrong_type));
    check(
        "another type on a CMD4 is not read through a Space Saving layout",
        card.product_count == 1 && card.products[0].typ == 22 && !card.products[0].space_saving &&
            !card.products[0].body_parsed && !itso_product_ticket(&card.products[0])->valid);

    parse_type2_exact(&card, cmd4_unused, sizeof(cmd4_unused));
    check(
        "an unused single has no usage place",
        card.products[0].count == 1 && !card.products[0].from.valid);

    parse_type2_exact(&card, cmd4_fare_value, sizeof(cmd4_fare_value));
    check(
        "GeoValidity can be a fare value",
        card.space->area_kind == ItsoAreaFareValue && card.space->area_value == 175);
    parse_type2_exact(&card, cmd4_location, sizeof(cmd4_location));
    check(
        "GeoValidity can be a location, kept by its LocDefType",
        card.space->area_kind == ItsoAreaLocation && card.space->area_value == 204);
    check(
        "a LOC4 zone map fills its origin slot with four zone bytes",
        card.space->area[0].valid && strcmp(loc_text(&card.space->area[0]), "Zones 1,2,3") == 0);
    check(
        "and leaves its empty destination and via absent",
        !card.space->area[1].valid && !card.space->area[2].valid);

    parse_type2_exact(&card, cmd4_journey_area, sizeof(cmd4_journey_area));
    check(
        "AreaValidity as a LOC3 has both ends",
        card.space->area_kind == ItsoAreaLocation && card.space->area_value == 203 &&
            strcmp(loc_text(&card.space->area[0]), "Station 1072") == 0 &&
            strcmp(loc_text(&card.space->area[1]), "Station 1444") == 0 &&
            loc_kind(&card.space->area[1]) == ItsoLocCodeNlc);
    check("a LOC3 has no via", !card.space->area[2].valid);

    parse_type2_exact(&card, cmd4_stage_area, sizeof(cmd4_stage_area));
    check(
        "a LOC3 fare stage's destination is on the origin's machine",
        strcmp(loc_text(&card.space->area[0]), "Fare stage 4 (6236160)") == 0 &&
            strcmp(loc_text(&card.space->area[1]), "Fare stage 9 (6236160)") == 0);

    /* ScaledQtyBackup (table 58b): a bit per m used, so it says what is left
     * to within m. */
    parse_type2_exact(&card, cmd4_return, sizeof(cmd4_return));
    check(
        "a Subway return's backup counts one ride, a bit to a ride",
        card.space->has_backup && card.space->backup_step == 1 && card.space->backup_count == 1);
    parse_type2_exact(&card, cmd4_backup_scaled, sizeof(cmd4_backup_scaled));
    check(
        "at ScalingFactor 4, ten left reads as up to twelve",
        card.products[0].count == 10 && card.space->backup_step == 4 &&
            card.space->backup_count == 12);
    parse_type2_exact(&card, cmd4_backup_torn, sizeof(cmd4_backup_torn));
    check(
        "a backup that disagrees is kept as it stands",
        card.products[0].count == 1 && card.space->backup_count == 3);
    parse_type2_exact(&card, cmd4_multileg, sizeof(cmd4_multileg));
    check(
        "revision 2 keeps its backup too",
        card.space->has_backup && card.space->backup_count == card.products[0].count);
    parse_type2_exact(&card, cmd4_carnet, sizeof(cmd4_carnet));
    check("a carnet has no backup", !card.space->has_backup);
}
