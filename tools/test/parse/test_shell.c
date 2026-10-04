/**
 * @file test_shell.c
 * @brief The shell's checksum, and why a shell is rejected.
 */
#include "test_parse.h"

/*
 * The shell's SECRC (TS 1000-2 clause 4.1.15), which is the one thing on a card
 * that can be checked without a key.
 *
 * The three CRC_B vectors come from Annex A of the same part, and pin the
 * algorithm independently of any card: a CRC that agrees with itself while
 * disagreeing with the specification would verify every synthetic shell and
 * reject every real one.
 */
void shell_checksum(void) {
    static const uint8_t v1[] = {0x00, 0x00, 0x00};
    static const uint8_t v2[] = {0x0F, 0xAA, 0xFF};
    static const uint8_t v3[] = {0x0A, 0x12, 0x34, 0x56};
    check("CRC_B Annex A example 1", itso_crc_b(v1, sizeof(v1)) == 0xC6CC);
    check("CRC_B Annex A example 2", itso_crc_b(v2, sizeof(v2)) == 0xD1FC);
    check("CRC_B Annex A example 3", itso_crc_b(v3, sizeof(v3)) == 0xF62C);

    static ItsoCard card;
    itso_card_reset(&card);
    itso_parse_shell(&card, card_shell, sizeof(card_shell));
    check("shell length is 6 blocks", card.shell_len == 6);
    check("shell checksum verifies", card.secrc_checked && card.secrc_valid);

    itso_card_reset(&card);
    itso_parse_shell(&card, cmd2_shell, sizeof(cmd2_shell));
    check("CMD2 shell checksum verifies", card.secrc_checked && card.secrc_valid);

    /* One flipped bit anywhere in the dataset has to be caught, including in
     * the elements below the checksum that nothing else on the card repeats. */
    uint8_t* damaged = malloc(sizeof(card_shell));
    memcpy(damaged, card_shell, sizeof(card_shell));
    damaged[17] ^= 0x01; /* Number of sectors: plausible, and wrong. */
    itso_card_reset(&card);
    itso_parse_shell(&card, damaged, sizeof(card_shell));
    check("a corrupted shell fails its checksum", card.secrc_checked && !card.secrc_valid);
    check(
        "a corrupted shell still reports both numbers", card.secrc_stored != card.secrc_computed);
    free(damaged);

    /* A shell whose declared length runs past what was read cannot be checked,
     * and must say so rather than checksumming whatever follows in memory. */
    uint8_t* truncated = malloc(sizeof(card_shell));
    memcpy(truncated, card_shell, sizeof(card_shell));
    truncated[0] = (uint8_t)((31 << 2) | (truncated[0] & 0x03)); /* ShellLength 31 blocks. */
    itso_card_reset(&card);
    itso_parse_shell(&card, truncated, 24);
    check("a shell shorter than it claims is not checked", !card.secrc_checked);
    free(truncated);
}

/*
 * A rejected shell has to say which test rejected it.
 *
 * The distinction is what separates a card whose layout Flipso does not know
 * from a card whose bytes did not arrive intact - the error screen shows one
 * of these, and a read that is retried on the second is a hang on the first.
 */
void shell_reject_reasons(void) {
    static ItsoCard card;

    itso_card_reset(&card);
    check(
        "a shell that parses reports no rejection",
        itso_parse_shell(&card, card_shell, sizeof(card_shell)) &&
            card.shell_reject == ItsoShellAccepted);

    /* A buffer too short to hold the header: what a truncated read looks like. */
    itso_card_reset(&card);
    check(
        "a short buffer is rejected as short",
        !itso_parse_shell(&card, card_shell, 23) && card.shell_reject == ItsoShellRejectShort);

    /* Long enough, but not ITSO's issuer number. */
    uint8_t* wrong_iin = malloc(sizeof(card_shell));
    memcpy(wrong_iin, card_shell, sizeof(card_shell));
    wrong_iin[3] ^= 0xFF;
    itso_card_reset(&card);
    check(
        "a foreign IIN is rejected as such",
        !itso_parse_shell(&card, wrong_iin, sizeof(card_shell)) &&
            card.shell_reject == ItsoShellRejectIin);
    free(wrong_iin);

    /* Bitmap bit 0 clear: a compact shell, with no directory behind it. */
    uint8_t* compact = malloc(sizeof(card_shell));
    memcpy(compact, card_shell, sizeof(card_shell));
    compact[0] &= (uint8_t)~0x02; /* Bitmap starts at bit 6, so bit 0 is byte 0 bit 1. */
    compact[1] &= (uint8_t)~0xF8;
    itso_card_reset(&card);
    check(
        "a compact shell is rejected as compact",
        !itso_parse_shell(&card, compact, sizeof(card_shell)) &&
            card.shell_reject == ItsoShellRejectCompact);
    free(compact);

    /* Header intact, geometry impossible. */
    uint8_t* geometry = malloc(sizeof(card_shell));
    memcpy(geometry, card_shell, sizeof(card_shell));
    geometry[16] = 0; /* Sector size zero. */
    itso_card_reset(&card);
    check(
        "impossible geometry is rejected as geometry",
        !itso_parse_shell(&card, geometry, sizeof(card_shell)) &&
            card.shell_reject == ItsoShellRejectGeometry);
    /* The checksum runs before the geometry check, so a shell rejected for its
     * geometry still says whether the bytes themselves arrived intact - which
     * is the whole point of reporting it on a failed read. */
    check(
        "a shell rejected for geometry still carries a checksum verdict",
        card.secrc_checked && !card.secrc_valid);
    free(geometry);

    /* A card number digit that is not decimal: the operator number would be
     * worked out from it as though it were. Byte 5 holds OID digits 1 and 2. */
    uint8_t* hex_digit = malloc(sizeof(card_shell));
    memcpy(hex_digit, card_shell, sizeof(card_shell));
    hex_digit[5] = (uint8_t)((hex_digit[5] & 0x0F) | 0xA0);
    itso_card_reset(&card);
    check(
        "a non-decimal card number digit is rejected as such",
        !itso_parse_shell(&card, hex_digit, sizeof(card_shell)) &&
            card.shell_reject == ItsoShellRejectNumber);
    check("and no operator number is made up from it", card.oid == 0);
    free(hex_digit);

    /* Nothing offered at all reads as "not read", not as an accepted shell. */
    itso_card_reset(&card);
    check("an untouched card reports no shell", card.shell_reject == ItsoShellRejectNone);
}
