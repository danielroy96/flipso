/**
 * @file test_type2_full.c
 * @brief Full-shell Type 2 cards: CMD9 (NTAG) and CMD10 (Ultralight EV1).
 */
#include "test_parse.h"

/**
 * Decode a whole full-shell card from its page memory with the decoder's own
 * pieces: shell, chip pages, the live directory, E1's chain and the log. (The
 * device builds saved-card blocks from the same pieces, which test_capture.c
 * checks.) Works on an exact-length heap copy so an over-read is caught.
 *
 * @return false at the first step that fails.
 */
static bool decode_full(ItsoCard* card, const uint8_t* pages, size_t len) {
    uint8_t* exact = malloc(len ? len : 1);
    memcpy(exact, pages, len);
    itso_card_reset(card);

    bool ok = false;
    uint8_t shell[ITSO_TYPE2_FULL_SHELL_LEN];
    const uint8_t* dir = NULL;
    if(itso_type2_kind(exact, len) != ItsoType2FullShell) goto done;
    if(!itso_type2_full_shell(exact, len, shell)) goto done;
    if(!itso_parse_shell(card, shell, sizeof(shell))) goto done;
    if(itso_type2_full_len(card) > len) goto done;
    itso_parse_type2_tag(card, exact, len);
    dir = itso_type2_directory(card, exact, len);
    if(!dir || !itso_parse_directory(card, dir, ITSO_TYPE2_DIR_LEN)) goto done;

    ItsoType2Pages source = {card, exact, len};
    uint8_t group[ITSO_MAX_GROUP_LEN];
    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        size_t got = itso_read_chain(
            card,
            dir,
            ITSO_TYPE2_DIR_LEN,
            product->dir_index,
            itso_type2_read_sector,
            &source,
            group,
            sizeof(group));
        if(got) itso_parse_ipe(product, group, got, card->sector_size);
    }
    size_t log = itso_read_log_sectors(
        card, dir, ITSO_TYPE2_DIR_LEN, itso_type2_read_sector, &source, group, sizeof(group));
    if(log) itso_parse_log(card, group, log);
    ok = true;

done:
    free(exact);
    return ok;
}

void full_shell_type2(void) {
    static ItsoCard card;

    /* --- CMD9 on an NTAG215 --- */
    uint8_t shell[ITSO_TYPE2_FULL_SHELL_LEN];
    check(
        "CMD9 shell comes back from its rotation",
        itso_type2_full_shell(cmd9_pages, sizeof(cmd9_pages), shell) && shell[0] == 0x18 &&
            shell[2] == 0x63 && shell[11] == 9);
    check("CMD9 page 6 byte 2 is the FVC", cmd9_pages[26] == 9);
    check("CMD9 card decodes", decode_full(&card, cmd9_pages, sizeof(cmd9_pages)));
    check("CMD9 ISRN", strcmp(card.isrn, EXPECT_CMD9_ISRN) == 0 && card.isrn_check_ok);
    check("CMD9 is not a compact shell", card.shell_valid && !card.shell_compact);
    check("CMD9 shell checksum verifies", card.secrc_checked && card.secrc_valid);
    check(
        "CMD9 geometry is TS 1000-10 table 104's",
        card.fvc == 9 && card.sector_size == 64 && card.sector_count == 9 &&
            card.dir_entries == 2 && card.sct_len == 3);
    check(
        "CMD9 on 64-byte sectors is an NTAG215",
        strcmp(itso_type2_chip_name(&card), "NTAG215") == 0);
    check("CMD9 NTAG215 reads to page 0x80", itso_type2_full_len(&card) == 512);

    /* The chip pages. */
    static const uint8_t uid[7] = {0x04, 0x19, 0x09, 0x21, 0x5A, 0x6B, 0x7C};
    check(
        "CMD9 keeps its chip UID, less BCC0",
        card.chip_uid_valid && memcmp(card.chip_uid, uid, sizeof(uid)) == 0);
    check("CMD9 NTAG215 has 540 bytes", card.chip_memory_len == 540);
    /* TS 1000-10 clause 10.23.1's F7 0F: the shell's pages 4-11, and the three
     * block-lock bits that freeze them so. */
    check(
        "CMD9 lock bytes F7 0F lock the shell's pages 4-11",
        itso_type2_locked_pages(card.chip_lock) == ITSO_TYPE2_FULL_LOCKED_PAGES &&
            itso_type2_frozen_pages(card.chip_lock) == 0xFFF8);
    check("CMD9 Abacus D000 is state 3", card.chip_abacus_valid && card.chip_abacus == 3);
    check("CMD9 at state 3 is not retired", !itso_card_retired(&card));

    /* The directory: copy B, DIRS# 7, beats copy A's 6. */
    check("CMD9 live directory is copy B", card.dir_valid && card.dir_sequence == 7);
    check("CMD9 has one product and a log", card.product_count == 1 && card.log_dir_index == 2);
    check(
        "CMD9 directory InstanceID",
        card.dir_instance_valid && itso_isam_oid(card.dir_isam) == 1234);

    /* E1: the journey ticket over sectors 1 and 4, then its value groups. */
    const ItsoProduct* ride = &card.products[0];
    check("CMD9 E1 is a journey ticket", ride->typ == 23 && ride->value_group);
    check("CMD9 E1 is active", ride->status == ItsoProductStatusActive);
    check(
        "CMD9 E1 decodes across two IPE sectors",
        ride->body_parsed && ride->from.valid && ride->to.valid);
    check("CMD9 E1 live record is TS#3, from copy B", ride->value_parsed && ride->value_ts == 3);
    check("CMD9 E1 has 7 rides left", ride->count == 7);
    check(
        "CMD9 E1 history takes both copies, newest first",
        ride->value_history_count == 3 && ride->value_history[0].ts == 3 &&
            ride->value_history[1].ts == 2 && ride->value_history[2].ts == 1);
    check(
        "CMD9 E1 history is all on the card",
        ride->value_history[0].on_card && ride->value_history[1].on_card &&
            ride->value_history[2].on_card);

    /* The log: T0 in sector 2, T1 in sector 3, RO 0 so T1 is newest. */
    check("CMD9 log has both records", card.tap_count == 2);
    check(
        "CMD9 newest tap is T1, listed first",
        card.taps[0].transaction_type == 11 &&
            itso_dts_to_unix(card.taps[0].dts) > itso_dts_to_unix(card.taps[1].dts));

    /* A torn transaction: TS#4 sits in the previous copy, but the directory still
     * names copy B first, so B's TS#3 is live and TS#4 is not history at all. */
    check("torn CMD9 decodes", decode_full(&card, cmd9_torn, sizeof(cmd9_torn)));
    ride = &card.products[0];
    check("an orphan record is not live", ride->value_ts == 3 && ride->count == 7);
    bool orphan = false;
    for(uint8_t i = 0; i < ride->value_history_count; i++) {
        if(ride->value_history[i].ts == 4) orphan = true;
    }
    check("nor is it history", !orphan && ride->value_history_count == 3);

    /* --- CMD10 on an Ultralight EV1 --- */
    check(
        "CMD10 is a full shell",
        itso_type2_kind(cmd10_pages, sizeof(cmd10_pages)) == ItsoType2FullShell);
    check("CMD10 card decodes", decode_full(&card, cmd10_pages, sizeof(cmd10_pages)));
    check("CMD10 ISRN", strcmp(card.isrn, EXPECT_CMD10_ISRN) == 0);
    check(
        "CMD10 is an Ultralight EV1", strcmp(itso_type2_chip_name(&card), "Ultralight EV1") == 0);
    check("CMD10 has 924 bytes", card.chip_memory_len == 924);
    check("CMD10 reads to page 0xE0", itso_type2_full_len(&card) == ITSO_TYPE2_FULL_MAX_LEN);
    check("CMD10 has no Abacus", !card.chip_abacus_valid);
    check("CMD10 blank directory copy loses", card.dir_valid && card.dir_sequence == 1);
    const ItsoProduct* purse = &card.products[0];
    check(
        "CMD10 purse balance from the current copy",
        itso_product_purse(purse)->balance.valid &&
            itso_product_purse(purse)->balance.value == 1500);
    check("CMD10 purse history takes the previous copy", purse->value_history_count == 2);
    check("CMD10 empty log has no taps", card.tap_count == 0);

    /* --- What is not a CMD9 or CMD10 --- */
    uint8_t* other = malloc(sizeof(cmd9_pages));
    memcpy(other, cmd9_pages, sizeof(cmd9_pages));
    other[26] = 11; /* The FVC, page 6 byte 2: a media definition not yet written. */
    check(
        "an unknown FVC is some other shell",
        itso_type2_kind(other, sizeof(cmd9_pages)) == ItsoType2OtherShell);
    other[26] = 9;
    other[17] = 0x64; /* IIN, now 643597: the FVC alone is not a shell (10.24.1). */
    check(
        "an FVC without the IIN is not ITSO",
        itso_type2_kind(other, sizeof(cmd9_pages)) == ItsoType2NotItso);
    free(other);

    /* A shell stating a geometry the CMD does not have: no sector map. */
    static ItsoCard odd;
    itso_card_reset(&odd);
    itso_type2_full_shell(cmd9_pages, sizeof(cmd9_pages), shell);
    itso_parse_shell(&odd, shell, sizeof(shell));
    odd.sector_count = 10;
    size_t offset = 0, len = 0;
    check(
        "an overridden geometry has no sector map",
        itso_type2_full_len(&odd) == 0 && !itso_type2_sector(&odd, 1, &offset, &len));
    odd.sector_count = 9;
    odd.fvc = 10;
    odd.sector_size = 64;
    check("a CMD10 has no 64-byte sectors", itso_type2_full_len(&odd) == 0);

    /* The sector map at its edges: directory copies are sectors 7 and 8. */
    itso_card_reset(&odd);
    itso_parse_shell(&odd, shell, sizeof(shell));
    check(
        "sector 1 is page 0x20",
        itso_type2_sector(&odd, 1, &offset, &len) && offset == 128 && len == 64);
    check("sector 6 ends the map", itso_type2_sector(&odd, 6, &offset, &len) && offset == 448);
    check(
        "sector 7 is directory A",
        itso_type2_sector(&odd, 7, &offset, &len) && offset == 48 && len == 40);
    check(
        "sector 8 is directory B",
        itso_type2_sector(&odd, 8, &offset, &len) && offset == 88 && len == 40);
    check(
        "sector 0 and 9 are not data",
        !itso_type2_sector(&odd, 0, &offset, &len) && !itso_type2_sector(&odd, 9, &offset, &len));

    /* Directory choice: DIRS# wraps, and a blank copy never wins. */
    uint8_t* pages = malloc(sizeof(cmd9_pages));
    memcpy(pages, cmd9_pages, sizeof(cmd9_pages));
    pages[ITSO_TYPE2_DIR_A_OFFSET + 15] = 0x00;
    pages[ITSO_TYPE2_DIR_B_OFFSET + 15] = 0xFF;
    check(
        "DIRS# 00 is newer than FF",
        itso_type2_directory(&odd, pages, sizeof(cmd9_pages)) == pages + ITSO_TYPE2_DIR_A_OFFSET);
    memset(pages + ITSO_TYPE2_DIR_B_OFFSET, 0, ITSO_TYPE2_DIR_LEN);
    pages[ITSO_TYPE2_DIR_A_OFFSET + 15] = 0x80;
    check(
        "a blank copy loses to any other",
        itso_type2_directory(&odd, pages, sizeof(cmd9_pages)) == pages + ITSO_TYPE2_DIR_A_OFFSET);
    check(
        "no directory without both copies",
        itso_type2_directory(&odd, pages, ITSO_TYPE2_SECTORS_OFFSET - 1) == NULL);
    free(pages);

    /* Retired: an Abacus of 16. */
    pages = malloc(ITSO_TYPE2_TAG_LEN);
    memcpy(pages, cmd9_pages, ITSO_TYPE2_TAG_LEN);
    pages[13] = 0xFF;
    pages[15] = 0xFF;
    decode_full(&card, cmd9_pages, sizeof(cmd9_pages));
    itso_parse_type2_tag(&card, pages, ITSO_TYPE2_TAG_LEN);
    check("an Abacus of FFFF is retired", card.chip_abacus == 16 && itso_card_retired(&card));
    free(pages);

    /* Every truncation: never an over-read, and never a card short of its
     * sectors. */
    bool short_ok = true;
    for(size_t cut = 0; cut <= sizeof(cmd9_pages); cut++) {
        bool decoded = decode_full(&card, cmd9_pages, cut);
        if(decoded != (cut == sizeof(cmd9_pages))) short_ok = false;
        uint8_t* exact = malloc(cut ? cut : 1);
        memcpy(exact, cmd9_pages, cut);
        itso_type2_kind(exact, cut);
        itso_parse_type2_tag(&card, exact, cut);
        itso_type2_directory(&card, exact, cut);
        free(exact);
    }
    check("truncated CMD9 reads survive, and only a whole one decodes", short_ok);
}
