/**
 * @file itso_cmd4.c
 * @brief CMD4: a Compact-Shell Type 2 tag, the paper ticket media (TS 1000-10 section 5).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "../itso_i.h"

#include <string.h>

/*
 * A Compact-Shell Type 2 tag lays its data groups out at fixed pages rather than
 * behind a Sector Chain Table (TS 1000-10 clauses 5.5-5.6). Numbering user bytes
 * from page 4 as the spec's "Data(n)" does, physical byte = 16 + n, so:
 *
 *   page 0-2   (bytes 0..11)  the chip serial, its check bytes and the lock bytes
 *   page 3     (bytes 12..15) IPE dynamic data, one-time programmable
 *   page 4-5   (bytes 16..23) IPE dynamic data, rewritable
 *   page 6     (bytes 24..26) the Compact ITSO Shell (Data8..Data10)
 *   page 6-7   (bytes 27..31) the single IPE Directory Entry E1 (Data11..Data15)
 *   page 8-9   (bytes 32..39) the IPE InstanceID
 *   page 10-13 (bytes 40..55) IPE static data
 *   page 14-15 (bytes 56..63) the Seal
 */
#define ITSO_T2_DIR_OFFSET      27
#define ITSO_T2_INSTANCE_OFFSET 32
#define ITSO_T2_SEAL_OFFSET     56

/* The physical page offsets the Space Saving IPE's dataset is spread across
 * (TS 1000-10 clauses 5.6.2-5.6.3). Reassembling them in this order gives the
 * one contiguous dataset TS 1000-5 table 48 defines: the static elements, then
 * the rewritable dynamic elements, then the one-time-programmable ones. */
#define ITSO_T2_STATIC_OFFSET   40 /* Pages 10-13: 16 bytes of static data. */
#define ITSO_T2_DYN_DATA_OFFSET 16 /* Pages 4-5: 8 bytes of rewritable dynamic data. */
#define ITSO_T2_OTP_OFFSET      12 /* Page 3: 4 bytes of OTP dynamic data. */

bool itso_parse_type2(ItsoCard* card, const uint8_t* pages, size_t len) {
    /* Only a whole CMD4: a full shell (CMD9, CMD10) is laid out nothing like
     * this fixed mapping, and a short read is not a card at all. */
    if(itso_type2_kind(pages, len) != ItsoType2Compact) return false;
    if(!itso_parse_shell(card, pages + ITSO_TYPE2_SHELL_OFFSET, len - ITSO_TYPE2_SHELL_OFFSET)) {
        return false;
    }

    /* The chip serial, skipping BCC0 at byte 3: the one thing that tells two
     * tickets apart, since the compact shell's number is the same on all. */
    const uint8_t uid[7] = {pages[0], pages[1], pages[2], pages[4], pages[5], pages[6], pages[7]};
    memcpy(card->chip_uid, uid, sizeof(uid));
    card->chip_uid_valid = true;
    /* Page 2 bytes 2-3, which say which pages the issuer made read-only. */
    card->chip_lock[0] = pages[10];
    card->chip_lock[1] = pages[11];
    card->chip_memory_len = (uint16_t)len;

    /* The single IPE Directory Entry, decoded exactly as a full card's is - it is
     * the same five bytes (TS 1000-2 clause 6.1). An all-zero entry is a shell
     * that has been formatted but carries no product yet. */
    const uint8_t* entry = pages + ITSO_T2_DIR_OFFSET;
    if(itso_is_blank(entry, ITSO_DIR_ENTRY_LEN)) return true;

    ItsoProduct* product = itso_card_next_product(card);
    if(!product) return true; /* The shell still decoded; the ticket had no room. */
    itso_parse_dir_entry(product, entry, 1);
    card->dir_valid = true;

    /* There is no Sector Chain Table to give a status. A CMD4 product is blocked
     * by zeroing its Seal (TS 1000-10 clause 5.16); short of that, whether it is
     * still good is a matter of its own dates and counts, so no status is
     * claimed for it rather than an "Active" its expiry may contradict. */
    if(itso_is_blank(pages + ITSO_T2_SEAL_OFFSET, ITSO_SEAL_LEN)) {
        product->status = ItsoProductStatusBlocked;
    }

    /* The InstanceID has the full IPE's structure (TS 1000-10 clause 5.6.1): the
     * ISAM that created the ticket, which names the operator that sold it. */
    itso_parse_instance_id(product, pages, len, ITSO_T2_INSTANCE_OFFSET);

    if(len < ITSO_CMD4_MEMORY_LEN) return true;
    uint8_t ds[ITSO_SPACE_SAVING_LEN];
    memcpy(ds, pages + ITSO_T2_STATIC_OFFSET, ITSO_SPACE_SAVING_STATIC_LEN);
    memcpy(
        ds + ITSO_SPACE_SAVING_STATIC_LEN,
        pages + ITSO_T2_DYN_DATA_OFFSET,
        ITSO_SPACE_SAVING_DYN_LEN);
    memcpy(
        ds + ITSO_SPACE_SAVING_STATIC_LEN + ITSO_SPACE_SAVING_DYN_LEN,
        pages + ITSO_T2_OTP_OFFSET,
        ITSO_SPACE_SAVING_OTP_LEN);
    itso_parse_space_saving(card, product, ds);
    return true;
}
