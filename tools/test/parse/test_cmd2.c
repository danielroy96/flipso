/**
 * @file test_cmd2.c
 * @brief A CMD2 (ISO 7816) card: two directory copies and a log in sectors.
 */
#include "test_parse.h"

/*
 * ITSO's generic micro-processor media (CMD2) differs from DESFire in how the
 * card is addressed rather than in how it is encoded, so the decoder should need
 * nothing special for it. What is new is the geometry: 64 sectors push the
 * Sector Chain Table to six bits per entry, and 16 directory entries fill the
 * product array exactly.
 */
void cmd2_card(void) {
    static ItsoCard card;
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
    check(
        "CMD2 E1 chains to sector 18", itso_sct_entry(&card, cmd2_dir, sizeof(cmd2_dir), 1) == 18);

    uint8_t group[ITSO_MAX_GROUP_LEN];
    memcpy(group, cmd2_sector1, sizeof(cmd2_sector1));
    memcpy(group + sizeof(cmd2_sector1), cmd2_sector18, sizeof(cmd2_sector18));
    itso_parse_ipe(purse, group, sizeof(cmd2_sector1) + sizeof(cmd2_sector18), card.sector_size);

    /* The second value record has never been written. Its DTS of zero decodes to
     * 2028, so taking it as the newest would both hide the balance and date it
     * into the future. */
    check(
        "CMD2 balance read",
        itso_product_purse(purse)->balance.valid &&
            itso_product_purse(purse)->balance.value == 250);
    check(
        "CMD2 balance skips the unwritten record",
        strcmp(fmt_unix(itso_dts_to_unix(purse->value_dts)), "2025-04-16 14:31") == 0);
    /* And the unwritten record must not become a history entry either: a blank
     * record would read as a zero balance in 2028, which is both the wrong
     * amount and later than the record it sits beside. */
    check("CMD2 history holds only the written record", purse->value_history_count == 1);

    ItsoProduct* id = &card.products[1];
    memcpy(group, cmd2_sector2, sizeof(cmd2_sector2));
    itso_parse_ipe(id, group, sizeof(cmd2_sector2), card.sector_size);
    check("CMD2 E2 is an ITSO ID", id->typ == 16);
    check(
        "CMD2 E2 holder name",
        itso_product_id(id)->has_name && strcmp(itso_product_id(id)->name, "JO CLYDE") == 0);
    /* HalfDayOfWeek (annex A.10) sits after the names, then ValidAtOrFrom. */
    check(
        "CMD2 E2 half days read",
        itso_product_id(id)->has_half_days && itso_product_id(id)->half_days == 0xFFE0);
    check(
        "CMD2 E2 half days are Monday to Saturday",
        itso_half_days_mask(itso_product_id(id)->half_days) == 0xFC);
    check(
        "CMD2 E2 valid at NLC 5685",
        id->from.valid && strcmp(loc_text(&id->from), "Station 5685") == 0);
}
