/**
 * @file itso_type2.c
 * @brief Type 2 tag page media: telling a compact shell from a full one, and the full-shell CMD9 and CMD10 sector map (TS 1000-10 clauses 10 and 11).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "../itso_i.h"

#include <string.h>

ItsoType2Kind itso_type2_kind(const uint8_t* pages, size_t len) {
    /* Checked first, because a read cut short can still hold page 6: a compact
     * shell with its Seal missing would decode, and be saved, as a ticket whose
     * product is half there. */
    if(len < ITSO_CMD4_MEMORY_LEN) return ItsoType2Incomplete;

    const uint8_t* shell = pages + ITSO_TYPE2_SHELL_OFFSET;
    size_t shell_len = len - ITSO_TYPE2_SHELL_OFFSET;
    if(itso_shell_is_compact(shell, shell_len)) return ItsoType2Compact;

    /* A full shell is stored from page 4, rotated so that its FVC shares page 6
     * byte 2 with a compact shell's (TS 1000-10 clauses 10.11.3, 11.14.3). Clause
     * 10.24.1 warns that the FVC alone gives false positives, so the shell is
     * put back together and held to the IIN like any other. */
    uint8_t full[ITSO_TYPE2_FULL_SHELL_LEN];
    if(!itso_type2_full_shell(pages, len, full)) return ItsoType2NotItso;
    if(!itso_looks_like_shell(full, sizeof(full))) return ItsoType2NotItso;
    if(full[11] == ITSO_FVC_NTAG || full[11] == ITSO_FVC_ULTRALIGHT_EV1) {
        return ItsoType2FullShell;
    }
    return ItsoType2OtherShell;
}

bool itso_type2_full_shell(const uint8_t* pages, size_t len, uint8_t* out) {
    if(len < ITSO_TYPE2_FULL_SHELL_OFFSET + ITSO_TYPE2_FULL_SHELL_LEN) return false;
    const uint8_t* stored = pages + ITSO_TYPE2_FULL_SHELL_OFFSET;
    /* The omitted first byte is kept as the block's last ("Len" in figures 5
     * and 8), and everything else sits one byte early. */
    out[0] = stored[ITSO_TYPE2_FULL_SHELL_LEN - 1];
    memcpy(out + 1, stored, ITSO_TYPE2_FULL_SHELL_LEN - 1);
    return true;
}

/**
 * True for the geometry TS 1000-10 tables 104 (CMD9) and 109 (CMD10) fix, which
 * the sector map below is only valid for: neither CMD allows an override.
 */
static bool itso_type2_full_geometry(const ItsoCard* card) {
    if(card->sector_count != 9 || card->dir_entries != 2 || card->sct_len != 3) return false;
    if(card->fvc == ITSO_FVC_NTAG) return card->sector_size == 64 || card->sector_size == 128;
    if(card->fvc == ITSO_FVC_ULTRALIGHT_EV1) return card->sector_size == 128;
    return false;
}

bool itso_type2_sector(const ItsoCard* card, uint8_t sector, size_t* offset, size_t* len) {
    if(!card->shell_valid || !itso_type2_full_geometry(card)) return false;

    /* The two directory copies sit ahead of the data sectors, at the same place
     * on every chip of the family (figures 4.1, 4.2 and 7). */
    if(sector == card->sector_count - 2 || sector == card->sector_count - 1) {
        *offset = sector == card->sector_count - 2 ? ITSO_TYPE2_DIR_A_OFFSET :
                                                     ITSO_TYPE2_DIR_B_OFFSET;
        *len = ITSO_TYPE2_DIR_LEN;
        return true;
    }
    if(sector == 0 || sector > ITSO_TYPE2_DATA_SECTORS) return false;

    *offset = ITSO_TYPE2_SECTORS_OFFSET + (size_t)(sector - 1) * card->sector_size;
    *len = card->sector_size;
    return true;
}

size_t itso_type2_read_sector(void* context, uint8_t sector, uint8_t* out, size_t capacity) {
    const ItsoType2Pages* source = context;
    size_t offset = 0, len = 0;
    if(!itso_type2_sector(source->card, sector, &offset, &len)) return 0;
    if(offset + len > source->len || len > capacity) return 0;
    memcpy(out, source->pages + offset, len);
    return len;
}

size_t itso_type2_full_len(const ItsoCard* card) {
    size_t offset = 0, len = 0;
    if(!itso_type2_sector(card, ITSO_TYPE2_DATA_SECTORS, &offset, &len)) return 0;
    return offset + len;
}

const uint8_t* itso_type2_directory(const ItsoCard* card, const uint8_t* pages, size_t len) {
    if(len < ITSO_TYPE2_SECTORS_OFFSET) return NULL;
    const uint8_t* a = pages + ITSO_TYPE2_DIR_A_OFFSET;
    const uint8_t* b = pages + ITSO_TYPE2_DIR_B_OFFSET;

    /* A torn or never-written copy is all zeros, and a DIRS# of 00 would beat an
     * FF on the other copy by the rollover rule. */
    bool a_blank = itso_is_blank(a, ITSO_TYPE2_DIR_LEN);
    bool b_blank = itso_is_blank(b, ITSO_TYPE2_DIR_LEN);
    if(a_blank != b_blank) return a_blank ? b : a;

    /* DIRS# follows the entries and the Sector Chain Table (TS 1000-2 table 6).
     * Annex A.3.1.2 starts A at 00 and B at 01, and every update overwrites the
     * older copy, so the two never match on a card written as the spec says;
     * should they, B is taken, as the CMD2 transport does. */
    size_t sequence = 2 + (size_t)card->dir_entries * ITSO_DIR_ENTRY_LEN + card->sct_len;
    if(sequence >= ITSO_TYPE2_DIR_LEN) return NULL;
    return (uint8_t)(b[sequence] - a[sequence]) < 0x80 ? b : a;
}

const char* itso_type2_chip_name(const ItsoCard* card) {
    if(!itso_type2_full_geometry(card)) return NULL;
    if(card->fvc == ITSO_FVC_ULTRALIGHT_EV1) return "Ultralight EV1";
    return card->sector_size == 64 ? "NTAG215" : "NTAG216";
}

void itso_parse_type2_tag(ItsoCard* card, const uint8_t* pages, size_t len) {
    if(len < ITSO_TYPE2_TAG_LEN) return;

    /* Pages 0-2 are laid out as on the CMD4 Ultralight: the serial less BCC0,
     * then the two static lock bytes (TS 1000-10 clauses 10.5, 11.5). */
    const uint8_t uid[7] = {pages[0], pages[1], pages[2], pages[4], pages[5], pages[6], pages[7]};
    memcpy(card->chip_uid, uid, sizeof(uid));
    card->chip_uid_valid = true;
    card->chip_lock[0] = pages[10];
    card->chip_lock[1] = pages[11];

    /* What the chip has in all, from which chip the shell says it is: NTAG215
     * is 540 bytes, NTAG216 and the Ultralight EV1 924 (clauses 10.4, 11.4).
     * The read itself stops at the end of the ITSO sectors, well short. */
    const char* chip = itso_type2_chip_name(card);
    if(chip) card->chip_memory_len = card->sector_size == 64 ? 540 : 924;

    /* The Abacus is two of the four OTP bytes in page 3, and its value is the
     * count of bits set in them, so that a chip that arrives with different
     * bits preset still counts from where it is (clause 10.24.4). CMD10 keeps
     * the same count in a one-way counter instead, which a READ does not
     * reach, and leaves page 3 to the issuer. */
    if(card->fvc == ITSO_FVC_NTAG && chip) {
        uint8_t bits = 0;
        for(uint8_t mask = 0x80; mask; mask >>= 1) {
            if(pages[13] & mask) bits++;
            if(pages[15] & mask) bits++;
        }
        card->chip_abacus = bits;
        card->chip_abacus_valid = true;
    }
}

uint16_t itso_type2_locked_pages(const uint8_t lock[2]) {
    return (uint16_t)(((uint16_t)lock[1] << 8) | (lock[0] & 0xF8));
}

uint16_t itso_type2_frozen_pages(const uint8_t lock[2]) {
    uint16_t pages = 0;
    if(lock[0] & 0x01) pages |= 1u << 3; /* BL-OTP: page 3. */
    if(lock[0] & 0x02) pages |= 0x03F0; /* BL9-4: pages 4-9. */
    if(lock[0] & 0x04) pages |= 0xFC00; /* BL15-10: pages 10-15. */
    return pages;
}
