/**
 * @file test_synthetic_card.c
 * @brief The synthetic CMD7 card from build_card.py: its shell, directory and entries.
 */
#include "test_parse.h"

/** The Shell Environment. */
void synthetic_shell(ItsoCard* card) {
    printf("== Shell ==\n");
    check("shell parses", itso_parse_shell(card, card_shell, sizeof(card_shell)));
    printf("  ISRN        %s\n", card->isrn);
    printf("  check digit %s\n", card->isrn_check_ok ? "ok" : "BAD");
    printf("  expiry      %s\n", fmt_unix(itso_date_to_unix(card->expiry)));
    printf(
        "  FVC %u KSC %u KVC %u  B=%u S=%u e#=%u SCTL=%u\n",
        card->fvc,
        card->ksc,
        card->kvc,
        card->sector_size,
        card->sector_count,
        card->dir_entries,
        card->sct_len);
    check("ISRN matches", strcmp(card->isrn, EXPECT_ISRN) == 0);
    check("check digit valid", card->isrn_check_ok);
    check("FVC is 7", card->fvc == 7);
    check(
        "geometry B=64 S=16 e#=8",
        card->sector_size == 64 && card->sector_count == 16 && card->dir_entries == 8);
}

/** The Directory: entries, the log entry and the blocking bit. */
void synthetic_directory(ItsoCard* card) {
    printf("\n== Directory ==\n");
    check("directory parses", itso_parse_directory(card, card_dir, sizeof(card_dir)));
    printf(
        "  products %u, log entry at E%u, blocked=%d\n",
        card->product_count,
        card->log_dir_index,
        card->shell_blocked);
    check("five products found", card->product_count == 5);
    check("log entry is E8", card->log_dir_index == 8);
    check("log entry decoded", card->log_entry_valid);
    printf(
        "  last tap flag: EEI=%u (%s), ptr=E%u, %s, RO=%u\n",
        card->log_eei,
        card->log_eei ? "checked in" : "not checked in",
        card->log_ptr,
        fmt_unix(itso_dts_to_unix(card->log_dts)),
        card->log_record_offset);
    check("EEI says checked in", card->log_eei == 1);
    check(
        "log DTS is 2026-09-14 08:41",
        strcmp(fmt_unix(itso_dts_to_unix(card->log_dts)), "2026-09-14 08:41") == 0);

    /* The blocking indicator is one bit of DIRBitMap and stops the whole shell,
     * not a product, so both of its states are pinned here: reading the
     * neighbouring bit would either condemn a live card or lose the log entry.
     * TS 1000-2 clause 5.1.2. */
    check("this shell is not blocked", !card->shell_blocked);
    static ItsoCard blocked;
    itso_card_reset(&blocked);
    itso_parse_shell(&blocked, card_shell, sizeof(card_shell));
    check(
        "blocked directory parses",
        itso_parse_directory(&blocked, card_dir_blocked, sizeof(card_dir_blocked)));
    check("blocking indicator read", blocked.shell_blocked);
    check(
        "blocking indicator leaves the rest of the bitmap alone",
        blocked.log_dir_index == 8 && blocked.product_count == 5);
}

/** What each directory entry says before its IPE is read. */
void synthetic_entries(ItsoCard* card) {
    printf("\n== Products ==\n");
    check(
        "E1 is TYP 2 with value group",
        card->products[0].typ == 2 && card->products[0].value_group);
    check("E1 operator 1234", card->products[0].oid == 1234);
    check("E1 active", card->products[0].status == ItsoProductStatusActive);
    check("E2 is TYP 16", card->products[1].typ == 16);
    check(
        "E3 is TYP 22 blocked",
        card->products[2].typ == 22 && card->products[2].status == ItsoProductStatusBlocked);
    check(
        "E4 is TYP 23 with value group",
        card->products[3].typ == 23 && card->products[3].value_group);
    check("E5 is TYP 3 loyalty", card->products[4].typ == 3);
    /* TS 1000-2 Annex B: the extension flag shifts an IPE owner into 8192-16383. */
    check("E1 operator is in the base range", !card->products[0].oid_extended);
    printf(
        "  E3 operator %u (extended=%d)\n", card->products[2].oid, card->products[2].oid_extended);
    check("E3 operator uses the extended range", card->products[2].oid_extended);
    check("E3 operator is 5678 + 8192", card->products[2].oid == 13870);
}
