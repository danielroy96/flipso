/* Host-side smoke test for the ITSO decoder against a synthetic CMD7 card. */
#include "itso.h"
#include "itso_i.h"
#include "card_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static const char* fmt_unix(uint32_t t) {
    static char buf[32];
    time_t tt = (time_t)t;
    struct tm tm;
    gmtime_r(&tt, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

static void dump_location(const char* label, const ItsoLocation* loc) {
    if(loc->valid) printf("      %s: %s (type %u)\n", label, loc->text, loc->def_type);
}

/* Feed the decoder degenerate and hostile buffers; under ASan/UBSan any
 * out-of-bounds read or overflow here aborts the run. */
static void robustness(void) {
    static uint8_t junk[256];
    ItsoCard c;

    /* All zeros must be rejected everywhere. */
    memset(junk, 0, sizeof(junk));
    itso_card_reset(&c);
    check("all-zero shell rejected", !itso_parse_shell(&c, junk, sizeof(junk)));
    check("directory without shell rejected", !itso_parse_directory(&c, junk, sizeof(junk)));

    /* Truncations of a valid shell and directory must not read past the end. */
    for(size_t len = 0; len <= sizeof(card_shell); len++) {
        itso_card_reset(&c);
        itso_parse_shell(&c, card_shell, len);
    }
    check("truncated shells survive", 1);

    for(size_t len = 0; len <= sizeof(card_dir); len++) {
        itso_card_reset(&c);
        itso_parse_shell(&c, card_shell, sizeof(card_shell));
        itso_parse_directory(&c, card_dir, len);
    }
    check("truncated directories survive", 1);

    /* A deterministic pseudo-random sweep over every entry point. */
    uint32_t seed = 0x1234567u;
    for(int round = 0; round < 4000; round++) {
        for(size_t i = 0; i < sizeof(junk); i++) {
            seed = seed * 1103515245u + 12345u;
            junk[i] = (uint8_t)(seed >> 16);
        }
        /* Keep the ITSO issuer marker so the shell parser accepts the garbage. */
        junk[2] = 0x63;
        junk[3] = 0x35;
        junk[4] = 0x97;
        junk[1] |= 0x10; /* Force the "full shell" bitmap bit. */

        itso_card_reset(&c);
        size_t len = 24 + (seed % (sizeof(junk) - 24));
        if(itso_parse_shell(&c, junk, len)) {
            itso_parse_directory(&c, junk, len);
            for(uint8_t p = 0; p < c.product_count; p++) {
                itso_parse_ipe(&c.products[p], junk, len, c.sector_size);
            }
            itso_parse_log(&c, junk, len);
        }
    }
    check("4000 random buffers survive", 1);

    /* Every IPE type, format revision and bitmap against truncated buffers.
     * The random sweep above reaches these paths only by chance, and the
     * per-type decoders are exactly where a wrong offset reads off the end. */
    for(uint8_t typ = 0; typ < 32; typ++) {
        for(uint8_t rev = 0; rev < 16; rev++) {
            for(uint8_t bitmap = 0; bitmap < 64; bitmap++) {
                for(size_t avail = 0; avail <= 96; avail++) {
                    /* Exactly the size the decoder is told it has. A generous
                     * stack buffer would leave a read past the dataset landing
                     * on valid memory, which is how a bounds bug in one of the
                     * per-type decoders hid from this test. */
                    uint8_t* body = malloc(avail ? avail : 1);
                    for(size_t i = 0; i < avail; i++) {
                        seed = seed * 1103515245u + 12345u;
                        body[i] = (uint8_t)(seed >> 16);
                    }
                    if(avail >= 2) {
                        /* IPELength in blocks, so the dataset claims the whole
                         * buffer, then the six bitmap bits and the revision. */
                        body[0] = (uint8_t)((((avail / 4) << 2) & 0xFC) | ((bitmap >> 4) & 0x03));
                        body[1] = (uint8_t)(((bitmap & 0x0F) << 4) | rev);
                    }
                    ItsoProduct pr;
                    memset(&pr, 0, sizeof(pr));
                    pr.typ = typ;
                    pr.value_group = true;
                    /* Sector size 0 as well: the value group offset divides by it. */
                    itso_parse_ipe(&pr, body, avail, (uint8_t)(avail % 17));
                    free(body);
                }
            }
        }
    }
    check("every IPE type survives a truncated dataset", 1);

    /* Every transient ticket revision and optional group combination. */
    for(uint8_t rev = 0; rev < 16; rev++) {
        for(uint16_t bits = 0; bits < 0x1000; bits += 7) {
            uint8_t rec[48];
            for(size_t i = 0; i < sizeof(rec); i++) {
                seed = seed * 1103515245u + 12345u;
                rec[i] = (uint8_t)(seed >> 16);
            }
            rec[1] = (uint8_t)((rec[1] & 0xF0) | rev);
            rec[2] = (uint8_t)(bits >> 4);
            rec[3] = (uint8_t)((bits & 0x0F) << 4 | (rec[3] & 0x0F));
            itso_card_reset(&c);
            itso_parse_log(&c, rec, sizeof(rec));
        }
    }
    check("every tap revision and group set survives", 1);

    /* Every location type against a short buffer, again sized exactly. */
    for(int t = 0; t < 256; t++) {
        for(size_t avail = 0; avail < 24; avail++) {
            ItsoLocation loc;
            uint8_t* body = malloc(avail ? avail : 1);
            memset(body, 0xAA, avail);
            if(avail >= 1) body[0] = (uint8_t)t;
            if(avail >= 2) body[1] = (uint8_t)(avail > 2 ? avail - 2 : 0);
            itso_parse_location(body, avail, ItsoLocStructLoc1, &loc);
            itso_parse_location(body, avail, ItsoLocStructLoc2, &loc);
            free(body);
        }
    }
    check("all location types survive short buffers", 1);
}


/*
 * ITSO's generic micro-processor media (CMD2) differs from DESFire in how the
 * card is addressed rather than in how it is encoded, so the decoder should need
 * nothing special for it. What is new is the geometry: 64 sectors push the
 * Sector Chain Table to six bits per entry, and 16 directory entries fill the
 * product array exactly.
 */
static void cmd2_card(void) {
    ItsoCard card;
    itso_card_reset(&card);

    check("CMD2 shell parsed", itso_parse_shell(&card, cmd2_shell, sizeof(cmd2_shell)));
    check("CMD2 ISRN", strcmp(card.isrn, EXPECT_CMD2_ISRN) == 0 && card.isrn_check_ok);
    check("CMD2 FVC is 2", card.fvc == 2);
    check(
        "CMD2 geometry B=80 S=64 e#=16",
        card.sector_size == 80 && card.sector_count == 64 && card.dir_entries == 16);
    check("CMD2 SCT is six bits wide", itso_sct_bits(card.sector_count) == 6);

    check("CMD2 directory parsed", itso_parse_directory(&card, cmd2_dir, sizeof(cmd2_dir)));
    check("CMD2 two products", card.product_count == 2);
    check("CMD2 log entry present", card.log_entry_valid && card.log_dir_index == 16);

    /* E1: the purse, whose value record lives in the sector its chain points to. */
    ItsoProduct* purse = &card.products[0];
    check("CMD2 E1 is stored travel rights", purse->typ == 2 && purse->value_group);
    check("CMD2 E1 active", purse->status == ItsoProductStatusActive);
    check("CMD2 E1 chains to sector 18", itso_sct_entry(&card, cmd2_dir, sizeof(cmd2_dir), 1) == 18);

    uint8_t group[ITSO_MAX_GROUP_LEN];
    memcpy(group, cmd2_sector1, sizeof(cmd2_sector1));
    memcpy(group + sizeof(cmd2_sector1), cmd2_sector18, sizeof(cmd2_sector18));
    itso_parse_ipe(
        purse, group, sizeof(cmd2_sector1) + sizeof(cmd2_sector18), card.sector_size);

    /* The second value record has never been written. Its DTS of zero decodes to
     * 2028, so taking it as the newest would both hide the balance and date it
     * into the future. */
    check("CMD2 balance read", purse->balance.valid && purse->balance.value == 250);
    check(
        "CMD2 balance skips the unwritten record",
        strcmp(fmt_unix(itso_dts_to_unix(purse->value_dts)), "2025-04-16 14:31") == 0);

    ItsoProduct* id = &card.products[1];
    memcpy(group, cmd2_sector2, sizeof(cmd2_sector2));
    itso_parse_ipe(id, group, sizeof(cmd2_sector2), card.sector_size);
    check("CMD2 E2 is an ITSO ID", id->typ == 16);
    check("CMD2 E2 holder name", id->has_name && strcmp(id->name, "JO CLYDE") == 0);
}

/*
 * A shell may claim more directory entries than the product array holds - CMD7
 * allows up to 31, and a CMD2 card already uses 16. Every entry being a product
 * must fill the array and stop, not run off the end of it.
 */
static void oversized_directory(void) {
    ItsoCard card;
    itso_card_reset(&card);
    if(!itso_parse_shell(&card, cmd2_shell, sizeof(cmd2_shell))) {
        check("oversized directory needs a shell", 0);
        return;
    }
    card.dir_entries = 31;

    /* 2 header bytes, 31 five-byte entries, then the SCT and its sequence byte. */
    uint8_t dir[2 + 31 * 5 + 46 + 1];
    memset(dir, 0x11, sizeof(dir)); /* Non-blank everywhere: every entry counts. */
    dir[0] = 0x00;
    dir[1] = 0x01; /* DIRBitMap zero: no log entry, so all 31 are products. */

    check("oversized directory parsed", itso_parse_directory(&card, dir, sizeof(dir)));
    check("product count capped", card.product_count == ITSO_MAX_PRODUCTS);
}

int main(void) {
    ItsoCard card;
    itso_card_reset(&card);

    printf("== Shell ==\n");
    check("shell parses", itso_parse_shell(&card, card_shell, sizeof(card_shell)));
    printf("  ISRN        %s\n", card.isrn);
    printf("  check digit %s\n", card.isrn_check_ok ? "ok" : "BAD");
    printf("  expiry      %s\n", fmt_unix(itso_date_to_unix(card.expiry)));
    printf("  FVC %u KSC %u KVC %u  B=%u S=%u e#=%u SCTL=%u\n",
           card.fvc, card.ksc, card.kvc, card.sector_size,
           card.sector_count, card.dir_entries, card.sct_len);
    check("ISRN matches", strcmp(card.isrn, EXPECT_ISRN) == 0);
    check("check digit valid", card.isrn_check_ok);
    check("FVC is 7", card.fvc == 7);
    check("geometry B=64 S=16 e#=8", card.sector_size == 64 && card.sector_count == 16 && card.dir_entries == 8);

    printf("\n== Directory ==\n");
    check("directory parses", itso_parse_directory(&card, card_dir, sizeof(card_dir)));
    printf("  products %u, log entry at E%u, blocked=%d\n",
           card.product_count, card.log_dir_index, card.shell_blocked);
    check("five products found", card.product_count == 5);
    check("log entry is E8", card.log_dir_index == 8);
    check("log entry decoded", card.log_entry_valid);
    printf("  last tap flag: EEI=%u (%s), ptr=E%u, %s, RO=%u\n",
           card.log_eei, card.log_eei ? "checked in" : "not checked in",
           card.log_ptr, fmt_unix(itso_dts_to_unix(card.log_dts)), card.log_record_offset);
    check("EEI says checked in", card.log_eei == 1);
    check("log DTS is 2026-09-14 08:41",
          strcmp(fmt_unix(itso_dts_to_unix(card.log_dts)), "2026-09-14 08:41") == 0);

    printf("\n== Products ==\n");
    check("E1 is TYP 2 with value group",
          card.products[0].typ == 2 && card.products[0].value_group);
    check("E1 operator 1234", card.products[0].oid == 1234);
    check("E1 active", card.products[0].status == ItsoProductStatusActive);
    check("E2 is TYP 16", card.products[1].typ == 16);
    check("E3 is TYP 22 blocked",
          card.products[2].typ == 22 && card.products[2].status == ItsoProductStatusBlocked);
    check("E4 is TYP 23 with value group",
          card.products[3].typ == 23 && card.products[3].value_group);
    check("E5 is TYP 3 loyalty", card.products[4].typ == 3);
    /* TS 1000-2 Annex B: the extension flag shifts an IPE owner into 8192-16383. */
    check("E1 operator is in the base range", !card.products[0].oid_extended);
    printf("  E3 operator %u (extended=%d)\n",
           card.products[2].oid, card.products[2].oid_extended);
    check("E3 operator uses the extended range", card.products[2].oid_extended);
    check("E3 operator is 5678 + 8192", card.products[2].oid == 13870);

    /* E1: pay as you go. Chain is sector 1 then sector 9. */
    uint8_t group[ITSO_MAX_GROUP_LEN];
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector1, sizeof(card_sector1));
    memcpy(group + 64, card_sector9, sizeof(card_sector9));
    itso_parse_ipe(&card.products[0], group, 128, 64);

    char money[32];
    itso_format_money(&card.products[0].balance, money, sizeof(money));
    printf("  E1 %s: balance %s at %s\n", itso_typ_name(card.products[0].typ), money,
           fmt_unix(itso_dts_to_unix(card.products[0].value_dts)));
    check("balance is GBP 12.34", strcmp(money, "GBP 12.34") == 0);
    check("purse value record read", card.products[0].value_parsed);
    check("balance timestamp is newest record",
          strcmp(fmt_unix(itso_dts_to_unix(card.products[0].value_dts)), "2026-09-14 08:41") == 0);

    /* The common value record header: every product with a value group carries
     * it, and it says what the last thing to happen to the product was. */
    printf("      last action %s, TS# %u, ISAM %08lX\n",
           itso_transaction_name(card.products[0].value_txn), card.products[0].value_ts,
           (unsigned long)card.products[0].value_isam);
    check("last action is a fare deduction", card.products[0].value_txn == 7);
    check("TS# read from the live record", card.products[0].value_ts == 101);
    check("modifying POST ISAM read", card.products[0].value_isam == 0xC0FFEE01);
    check("action sequence number read", card.products[0].value_action_seq == 3);

    /* The IPE dataset: the commercial terms of the purse. */
    itso_format_money(&card.products[0].max_value, money, sizeof(money));
    check("purse ceiling is GBP 90.00",
          card.products[0].has_limits && strcmp(money, "GBP 90.00") == 0);
    itso_format_money(&card.products[0].max_negative, money, sizeof(money));
    check("overdraft is GBP 2.00", strcmp(money, "GBP 2.00") == 0);
    itso_format_money(&card.products[0].top_up_amount, money, sizeof(money));
    check("auto top-up adds GBP 10.00",
          card.products[0].has_top_up && strcmp(money, "GBP 10.00") == 0);
    itso_format_money(&card.products[0].top_up_threshold, money, sizeof(money));
    check("auto top-up triggers below GBP 5.00", strcmp(money, "GBP 5.00") == 0);
    check("auto top-up is enabled in the value record", card.products[0].auto_top_up);
    itso_format_money(&card.products[0].deposit, money, sizeof(money));
    check("deposit is GBP 5.00",
          card.products[0].has_deposit && strcmp(money, "GBP 5.00") == 0);
    check("deposit was paid in cash", card.products[0].deposit_mop == 1);
    check("retailer is not the owner",
          card.products[0].has_retailer && card.products[0].retailer == 247);
    check("remove date says owner only",
          card.products[0].has_remove_date && card.products[0].remove_date == 255);
    check("auto top-up start date 2024-01-01",
          card.products[0].has_start &&
          strcmp(fmt_unix(itso_date_to_unix(card.products[0].start)), "2024-01-01 00:00") == 0);

    /* A journey in progress. */
    itso_format_money(&card.products[0].cumulative_fare, money, sizeof(money));
    check("two journey legs so far",
          card.products[0].has_journey && card.products[0].journey_legs == 2);
    check("cumulative fare is GBP 2.65", strcmp(money, "GBP 2.65") == 0);

    /* The IPE InstanceID, the only unique identity a product has. */
    check("instance id decoded",
          card.products[0].instance_valid && card.products[0].isam_id == 0x01020304 &&
          card.products[0].isam_seq == 1);
    check("seal key id decoded", card.products[0].key_id == 1);

    /* E2: ITSO ID. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector2, sizeof(card_sector2));
    itso_parse_ipe(&card.products[1], group, 64, 64);
    printf("  E2 %s: name '%s', entitlement %s, class %s\n",
           itso_typ_name(card.products[1].typ), card.products[1].name,
           itso_entitlement_name(card.products[1].entitlement_code),
           itso_profile_name(card.products[1].concession_class));
    printf("      valid %s", fmt_unix(itso_date_to_unix(card.products[1].start)));
    printf(" to %s\n", fmt_unix(itso_date_to_unix(card.products[1].sub_expiry)));
    check("holder name", strcmp(card.products[1].name, "ALEX MORGAN") == 0);
    check("entitlement is limited free ride", card.products[1].entitlement_code == 2);
    check("concession class is pensioner", card.products[1].concession_class == 4);
    check("entitlement start 2024-04-01",
          strcmp(fmt_unix(itso_date_to_unix(card.products[1].start)), "2024-04-01 00:00") == 0);
    check("entitlement expiry 2029-03-31",
          strcmp(fmt_unix(itso_date_to_unix(card.products[1].sub_expiry)), "2029-03-31 00:00") == 0);

    printf("      born %04u-%02u-%02u, gender %s, passback %u min\n",
           card.products[1].dob_year, card.products[1].dob_month, card.products[1].dob_day,
           itso_gender_name(card.products[1].id_flags), card.products[1].passback);
    check("date of birth is 1955-11-03",
          card.products[1].has_dob && card.products[1].dob_year == 1955 &&
          card.products[1].dob_month == 11 && card.products[1].dob_day == 3);
    check("IDFlags say the card is photo personalised",
          card.products[1].has_id_flags && itso_id_personalised(card.products[1].id_flags));
    check("IDFlags allow a companion", itso_id_companion(card.products[1].id_flags));
    check("IDFlags record female",
          strcmp(itso_gender_name(card.products[1].id_flags), "Female") == 0);
    check("passback time is 30 minutes",
          card.products[1].has_passback && card.products[1].passback == 30);
    /* TYP 14 and 16 put an accounting reference where other types put the
     * retailer, so reading one as the other would name the wrong operator. */
    check("identity IPE reports no retailer", !card.products[1].has_retailer);

    /* E3: period ticket with rail NLC locations. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector3, sizeof(card_sector3));
    memcpy(group + 64, card_sector11, sizeof(card_sector11));
    itso_parse_ipe(&card.products[2], group, 128, 64);
    printf("  E3 %s: expires %s (%s)\n", itso_typ_name(card.products[2].typ),
           fmt_unix(itso_date_to_unix(card.products[2].expiry)),
           itso_date_expired(card.products[2].expiry, 1789000000UL) ? "expired" : "valid");
    dump_location("from", &card.products[2].from);
    dump_location("to", &card.products[2].to);
    check("period ticket from NLC 1072",
          card.products[2].from.valid && strcmp(card.products[2].from.text, "NLC 1072") == 0);
    check("period ticket to NLC 1444",
          card.products[2].to.valid && strcmp(card.products[2].to.text, "NLC 1444") == 0);
    check("validity start 2025-01-01",
          card.products[2].has_start &&
          strcmp(fmt_unix(itso_date_to_unix(card.products[2].start)), "2025-01-01 00:00") == 0);
    check("2025-12-31 expiry reads as expired in 2026",
          itso_date_expired(card.products[2].expiry, 1789000000UL));

    /* A period ticket counts unactivated passes, and expires the stock of them
     * separately from the pass currently in use. */
    printf("      %u passes left, current pass to %s\n", (unsigned)card.products[2].count,
           fmt_unix(itso_date_to_unix(card.products[2].current_expiry)));
    check("period value record read", card.products[2].value_parsed);
    check("period ticket counts passes, not money",
          card.products[2].count_kind == ItsoCountPasses &&
          !card.products[2].balance.valid);
    check("four passes remain", card.products[2].count == 4);
    check("current pass expires 2025-02-28",
          card.products[2].has_current_expiry &&
          strcmp(fmt_unix(itso_date_to_unix(card.products[2].current_expiry)),
                 "2025-02-28 00:00") == 0);
    check("unused passes expire 2025-12-31",
          card.products[2].has_stored_expiry &&
          strcmp(fmt_unix(itso_date_to_unix(card.products[2].stored_expiry)),
                 "2025-12-31 00:00") == 0);
    check("period ticket auto-renews", card.products[2].auto_renew);

    /* E4: a journey ticket at format revision 2, with a value record that counts
     * rides. The chain is sector 4 (the IPE, spilling into a second sector) then
     * sector 10 (the value records). */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector4, sizeof(card_sector4));
    memcpy(group + sizeof(card_sector4), card_sector10, sizeof(card_sector10));
    itso_parse_ipe(
        &card.products[3], group, sizeof(card_sector4) + sizeof(card_sector10), 64);
    printf("  E4 %s: rev %u, rides left %u, used %d\n",
           itso_typ_name(card.products[3].typ), card.products[3].format_rev,
           (unsigned)card.products[3].count, card.products[3].ticket_used);
    dump_location("from", &card.products[3].from);
    dump_location("to", &card.products[3].to);
    check("journey ticket is revision 2", card.products[3].format_rev == 2);
    check("journey ticket from NLC 5631",
          card.products[3].from.valid &&
          strcmp(card.products[3].from.text, "NLC 5631") == 0);
    check("journey ticket to NLC 5685",
          card.products[3].to.valid && strcmp(card.products[3].to.text, "NLC 5685") == 0);

    /* Both value records carry the same DTS, so only TS# distinguishes them. The
     * live one is the later of the two: the ride has been spent. */
    check("journey value record read", card.products[3].value_parsed);
    check("journey ticket counts rides, not money",
          card.products[3].count_kind == ItsoCountRides &&
          !card.products[3].balance.valid);
    check("TS# picks the newer record over an equal DTS",
          card.products[3].count == 0 && card.products[3].ticket_used);

    /* E5: loyalty. A points balance is three bytes wide, so decoding it as a
     * purse would silently truncate it to the low two. */
    memset(group, 0, sizeof(group));
    memcpy(group, card_sector5, sizeof(card_sector5));
    memcpy(group + 64, card_sector12, sizeof(card_sector12));
    itso_parse_ipe(&card.products[4], group, 128, 64);
    printf("  E5 %s: %lu points\n", itso_typ_name(card.products[4].typ),
           (unsigned long)card.products[4].count);
    check("loyalty value record read", card.products[4].value_parsed);
    check("loyalty counts points",
          card.products[4].count_kind == ItsoCountPoints &&
          !card.products[4].balance.valid);
    check("74500 points does not truncate to 16 bits", card.products[4].count == 74500);
    check("loyalty remove date is 30 days",
          card.products[4].has_remove_date && card.products[4].remove_date == 30);

    printf("\n== Taps ==\n");
    itso_parse_log(&card, card_log, sizeof(card_log));
    printf("  %u tap(s)\n", card.tap_count);
    for(uint8_t i = 0; i < card.tap_count; i++) {
        const ItsoTap* tap = &card.taps[i];
        itso_format_money(&tap->amount, money, sizeof(money));
        printf("  [%u]%s %s at %s, %s\n", i, tap->latest ? " *" : "  ",
               itso_transaction_name(tap->transaction_type),
               fmt_unix(itso_dts_to_unix(tap->dts)), money);
        dump_location("from", &tap->origin);
        dump_location("to", &tap->destination);
    }
    check("three taps decoded", card.tap_count == 3);
    check("newest tap first is tap out", card.taps[0].transaction_type == 12);
    check("newest tap flagged latest", card.taps[0].latest);
    check("tap out origin", card.taps[0].origin.valid &&
          strcmp(card.taps[0].origin.text, "NLC 1072") == 0);
    check("tap out destination", card.taps[0].destination.valid &&
          strcmp(card.taps[0].destination.text, "NLC 1444") == 0);
    itso_format_money(&card.taps[0].amount, money, sizeof(money));
    check("tap out fare GBP 2.65", strcmp(money, "GBP 2.65") == 0);
    check("older tap is tap in", card.taps[1].transaction_type == 11);

    /* The third record is on format revision 4, which a check-in/check-out
     * closed system writes on exit. It carries groups revisions 1 and 2 have no
     * bit for, so mis-sizing any of them would shift all that follow. */
    const ItsoTap* rev4 = &card.taps[2];
    printf("  rev%u: via %s, paid by %s, entry %s, entry op %u\n", rev4->format_rev,
           rev4->route.text, itso_payment_name(rev4->mop),
           fmt_unix(itso_dts_to_unix(rev4->entry_dts)), rev4->entry_oid);
    check("third record is format revision 4", rev4->format_rev == 4);
    check("routing code is NLC 1444",
          rev4->route.valid && strcmp(rev4->route.text, "NLC 1444") == 0);
    check("destination survives the routing group",
          rev4->destination.valid && strcmp(rev4->destination.text, "NLC 5685") == 0);
    itso_format_money(&rev4->amount, money, sizeof(money));
    check("rev 4 fare GBP 4.80", strcmp(money, "GBP 4.80") == 0);
    check("fare was paid in cash", rev4->has_mop && rev4->mop == 1);
    check("fare was collected", !rev4->no_fare_charged);
    check("VAT is 20%", rev4->has_vat && rev4->vat == 2000);
    check("POST network IIN read", rev4->has_iin && rev4->iin == 0x633597);
    check("candidate IPEs read",
          rev4->has_cipe && rev4->cipe[0] == 1 && rev4->cipe[1] == 4 && rev4->cipe[2] == 0);
    check("inspection flag set, invalid travel clear",
          rev4->inspected && !rev4->invalid_travel);
    check("entry timestamp is 2026-09-12 08:12",
          rev4->has_entry &&
          strcmp(fmt_unix(itso_dts_to_unix(rev4->entry_dts)), "2026-09-12 08:12") == 0);
    check("entry operator is 109", rev4->has_entry_oid && rev4->entry_oid == 109);

    printf("\n== CMD2 card ==\n");
    cmd2_card();

    printf("\n== Robustness ==\n");
    oversized_directory();
    robustness();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
