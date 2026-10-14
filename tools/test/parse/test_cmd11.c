/**
 * @file test_cmd11.c
 * @brief A CMD11 (Programmable Intelligent Media) card, as its applet answers
 * before authentication: the FCI, the whole-group reads and the OID list.
 */
#include "test_parse.h"

/* An exact-length heap copy, so that reading one byte past what the card sent
 * is the sanitiser's to see. */
static uint8_t* dup_exact(const uint8_t* src, size_t len) {
    uint8_t* out = malloc(len);
    memcpy(out, src, len);
    return out;
}

/* The FCI: BER-TLV with long-form lengths, searched only where ITSO puts its
 * objects (TS 1000-10 figures 9 and 10). */
static void cmd11_fci(void) {
    uint8_t* fci = dup_exact(cmd11_fci_static, sizeof(cmd11_fci_static));
    size_t len = 0;
    const uint8_t* shell = itso_fci_find(fci, sizeof(cmd11_fci_static), 0xC0, &len);
    check("CMD11 FCI past 127 bytes still has its shell", shell && len == 24);
    check(
        "CMD11 FCI shell is the shell",
        shell && memcmp(shell, cmd11_shell, sizeof(cmd11_shell)) == 0);
    const uint8_t* dir = itso_fci_find(fci, sizeof(cmd11_fci_static), 0xC2, &len);
    check("CMD11 FCI has a directory", dir && len == 52);
    check(
        "CMD11 FCI has an OID list of one per entry",
        itso_fci_find(fci, sizeof(cmd11_fci_static), 0xC3, &len) && len == 16);
    check(
        "CMD11 static-MID FCI has no pseudo-MID",
        !itso_fci_find(fci, sizeof(cmd11_fci_static), 0xC6, &len));
    free(fci);

    /* A two-byte long form inside A5 as well as in 6F. */
    fci = dup_exact(cmd11_fci_pseudo_dir, sizeof(cmd11_fci_pseudo_dir));
    check(
        "CMD11 random-MID FCI has its pseudo-MID",
        itso_fci_find(fci, sizeof(cmd11_fci_pseudo_dir), 0xC6, &len) && len == 8);
    check(
        "CMD11 random-MID FCI with a long A5 has its directory",
        itso_fci_find(fci, sizeof(cmd11_fci_pseudo_dir), 0xC2, &len) && len == 52);
    free(fci);

    /* CMD2's Parameter EF reuses C3 for a short file id. */
    fci = dup_exact(cmd11_fci_e0, sizeof(cmd11_fci_e0));
    check(
        "FCI search does not descend into a CMD2 Parameter EF",
        !itso_fci_find(fci, sizeof(cmd11_fci_e0), 0xC3, &len));
    check(
        "FCI search still finds the Parameter EF itself",
        itso_fci_find(fci, sizeof(cmd11_fci_e0), 0xE0, &len) && len == 3);
    free(fci);

    /* Each truncation of a valid FCI ends the search cleanly. */
    bool clean = true;
    for(size_t cut = 0; cut < sizeof(cmd11_fci_static); cut++) {
        fci = dup_exact(cmd11_fci_static, cut ? cut : 1);
        const uint8_t* found = itso_fci_find(fci, cut, 0xC5, &len);
        if(found && found + len > fci + cut) clean = false;
        free(fci);
    }
    check("FCI search stays inside a cut-off response", clean);
}

void cmd11_card(void) {
    cmd11_fci();

    static ItsoCard card;
    itso_card_reset(&card);
    check("CMD11 shell parsed", itso_parse_shell(&card, cmd11_shell, sizeof(cmd11_shell)));
    check("CMD11 ISRN", strcmp(card.isrn, EXPECT_CMD11_ISRN) == 0 && card.isrn_check_ok);
    check("CMD11 FVC is 11", card.fvc == ITSO_FVC_PIM);
    check(
        "CMD11 geometry B=0 S=11 e#=8",
        card.sector_size == 0 && card.sector_count == 11 && card.dir_entries == 8);
    check("CMD11 shell checksum", card.secrc_checked && card.secrc_valid);

    /* B = 0 is CMD11's alone: on any other media it is a shell with nowhere to
     * put a product. */
    uint8_t not_pim[sizeof(cmd11_shell)];
    memcpy(not_pim, cmd11_shell, sizeof(not_pim));
    not_pim[11] = 2;
    static ItsoCard other;
    itso_card_reset(&other);
    check(
        "A CMD2 shell with no sector size is refused",
        !itso_parse_shell(&other, not_pim, sizeof(not_pim)) &&
            other.shell_reject == ItsoShellRejectGeometry);

    size_t dir_len = 0;
    const uint8_t* dir = itso_fci_find(cmd11_fci_static, sizeof(cmd11_fci_static), 0xC2, &dir_len);
    uint8_t* dir_copy = dup_exact(dir, dir_len);
    check("CMD11 directory parsed", itso_parse_directory(&card, dir_copy, dir_len));
    check("CMD11 three products", card.product_count == 3);
    check("CMD11 log entry", card.log_entry_valid && card.log_dir_index == 8);
    check(
        "CMD11 products in use",
        card.products[0].status == ItsoProductStatusActive &&
            card.products[2].status == ItsoProductStatusActive);
    check(
        "CMD11 directory InstanceID read",
        card.dir_instance_valid && !card.dir_instance_hidden &&
            itso_isam_oid(card.dir_isam) == 1234);
    check("CMD11 E3's entry alone says 8483", card.products[2].oid == (0x2000 | 0x0123));

    /* A list that agrees with no entry changes none of them. */
    itso_parse_oid_list(&card, cmd11_oids_wrong, sizeof(cmd11_oids_wrong));
    check(
        "CMD11 OID list that disagrees is ignored",
        card.products[0].oid == 1234 && card.products[1].oid == 13870 &&
            card.products[2].oid == 0x2123);

    size_t list_len = 0;
    const uint8_t* list =
        itso_fci_find(cmd11_fci_static, sizeof(cmd11_fci_static), 0xC3, &list_len);
    /* One entry short: the last product's OID is the directory's, not a read
     * past the end of the list. */
    uint8_t* short_list = dup_exact(list, 4);
    itso_parse_oid_list(&card, short_list, 4);
    check("CMD11 OID list cut short leaves the rest", card.products[2].oid == 0x2123);
    free(short_list);
    uint8_t* list_copy = dup_exact(list, list_len);
    itso_parse_oid_list(&card, list_copy, list_len);
    check("CMD11 OID list agrees with E1", card.products[0].oid == 1234);
    check("CMD11 OID list agrees with E2", card.products[1].oid == 13870);
    check("CMD11 OID list names E3's 16-bit owner", card.products[2].oid == 24867);
    free(list_copy);

    /* E1: ReadIPE less its entry, then ReadVRG straight after it, which is
     * how a read saves the product and where B = 0 finds the records. */
    ItsoProduct* purse = &card.products[0];
    const uint8_t* ipe = NULL;
    size_t ipe_len = itso_cmd11_ipe_group(cmd11_ipe1, sizeof(cmd11_ipe1), &ipe);
    check("CMD11 ReadIPE less its entry", ipe == cmd11_ipe1 + 5 && ipe_len == 24 + 16);
    const uint8_t* values = NULL;
    size_t values_len = itso_cmd11_value_group(cmd11_vrg1, sizeof(cmd11_vrg1), &values);
    check("CMD11 ReadVRG read bare", values == cmd11_vrg1 && values_len == sizeof(cmd11_vrg1));
    const uint8_t* entry_values = NULL;
    size_t entry_values_len =
        itso_cmd11_value_group(cmd11_vrg1_entry, sizeof(cmd11_vrg1_entry), &entry_values);
    check(
        "CMD11 ReadVRG with an entry in front",
        entry_values == cmd11_vrg1_entry + 5 && entry_values_len == values_len);

    size_t group_len = ipe_len + values_len;
    uint8_t* group = malloc(group_len);
    memcpy(group, ipe, ipe_len);
    memcpy(group + ipe_len, values, values_len);
    itso_parse_ipe(purse, group, group_len, card.sector_size);
    free(group);
    check("CMD11 purse decoded", purse->body_parsed && purse->typ == 2);
    check(
        "CMD11 purse balance from the newest record",
        itso_product_purse(purse)->balance.valid &&
            itso_product_purse(purse)->balance.value == 1230 && purse->value_ts == 7);
    check("CMD11 purse keeps both records", purse->value_history_count == 2);
    check(
        "CMD11 redacted InstanceID is hidden, not an ISAM",
        purse->instance_hidden && !purse->instance_valid && purse->isam_id == 0);
    check("CMD11 value records keep the machine that wrote them", purse->value_isam == 0xC0FFEE01);

    ItsoProduct* id = &card.products[1];
    ipe_len = itso_cmd11_ipe_group(cmd11_ipe2, sizeof(cmd11_ipe2), &ipe);
    uint8_t* id_group = dup_exact(ipe, ipe_len);
    itso_parse_ipe(id, id_group, ipe_len, card.sector_size);
    free(id_group);
    check(
        "CMD11 ID holder name",
        itso_product_id(id)->has_name && strcmp(itso_product_id(id)->name, "ALEX MORGAN") == 0);
    check("CMD11 ID InstanceID hidden", id->instance_hidden);

    /* Too short to hold even the IPE's header after the entry. */
    check("CMD11 ReadIPE that is only an entry", itso_cmd11_ipe_group(cmd11_ipe1, 6, &ipe) == 0);

    /* ReadLog with a count of five: every record, newest first. */
    uint8_t* log = dup_exact(cmd11_log, sizeof(cmd11_log));
    itso_parse_log(&card, log, sizeof(cmd11_log));
    free(log);
    check("CMD11 log holds five journeys", card.tap_count == 5);
    check(
        "CMD11 journeys are newest first",
        strcmp(fmt_unix(itso_dts_to_unix(card.taps[0].dts)), "2026-10-09 08:15") == 0);

    /* A directory sent with its InstanceID blanked, as an IPE's is. */
    dir_copy[dir_len - 5] = dir_copy[dir_len - 4] = dir_copy[dir_len - 3] = dir_copy[dir_len - 2] =
        dir_copy[dir_len - 1] = 0xFF;
    static ItsoCard hidden;
    itso_card_reset(&hidden);
    itso_parse_shell(&hidden, cmd11_shell, sizeof(cmd11_shell));
    itso_parse_directory(&hidden, dir_copy, dir_len);
    check(
        "CMD11 blanked directory InstanceID is hidden",
        hidden.dir_instance_hidden && !hidden.dir_instance_valid && hidden.dir_isam == 0);
    free(dir_copy);
    itso_card_reset(&hidden);
    itso_card_reset(&other);
    itso_card_reset(&card);
}
