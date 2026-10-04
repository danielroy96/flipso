/**
 * @file itso_directory.c
 * @brief The Directory Data Group and Sector Chain Table (TS 1000-2 clause 5).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "../itso_i.h"

uint8_t itso_sct_bits(uint8_t sector_count) {
    /* psi is the smallest number of bits with S <= 2^psi (TS 1000-2 5.1.5.1). */
    uint8_t psi = 1;
    while((1u << psi) < sector_count && psi < 8) {
        psi++;
    }
    return psi;
}

/** Byte offset of the Sector Chain Table inside the directory. */
static uint16_t itso_sct_offset(const ItsoCard* card) {
    return 2 + ITSO_DIR_ENTRY_LEN * card->dir_entries;
}

uint8_t itso_sct_entry(const ItsoCard* card, const uint8_t* dir, size_t dir_len, uint8_t sector) {
    if(sector == 0) return 0;
    uint8_t psi = itso_sct_bits(card->sector_count);
    uint32_t bit = (uint32_t)itso_sct_offset(card) * 8 + (uint32_t)(sector - 1) * psi;
    if((bit + psi) > (uint32_t)((itso_sct_offset(card) + card->sct_len) * 8)) return 0;
    if((bit + psi) > dir_len * 8) return 0;
    return (uint8_t)itso_bits(dir, bit, psi);
}

size_t itso_read_chain(
    const ItsoCard* card,
    const uint8_t* dir,
    size_t dir_len,
    uint8_t start,
    ItsoSectorRead read,
    void* context,
    uint8_t* out,
    size_t capacity) {
    size_t total = 0;
    uint8_t sector = start;

    for(uint8_t hop = 0; hop < ITSO_MAX_CHAIN_HOPS; hop++) {
        if(sector == 0 || sector >= card->sector_count) break;
        if(total + card->sector_size > capacity) break;

        /* Stop on a sector that will not read as well as on a chain that has
         * ended: the bytes gathered so far are the front of a group, and
         * decoding them would report a half-read product as a whole one. */
        size_t got = read(context, sector, out + total, capacity - total);
        if(got == 0) break;
        total += got;

        uint8_t next = itso_sct_entry(card, dir, dir_len, sector);
        /* Terminators: itself (unused), S-2 (blocked) or S-1 (in use). */
        if(next == sector || next == 0 || next == card->sector_count - 2 ||
           next == card->sector_count - 1) {
            break;
        }
        sector = next;
    }

    return total;
}

size_t itso_read_log_sectors(
    const ItsoCard* card,
    const uint8_t* dir,
    size_t dir_len,
    ItsoSectorRead read,
    void* context,
    uint8_t* out,
    size_t capacity) {
    if(card->log_dir_index == 0) return 0;

    /* A data sector, never a directory copy: S-2 and S-1 are terminators in any
     * other chain, and in this one SCT(T1) is 0 (TS 1000-2 clause 5.1.5.5). The
     * cap is the four records a two-bit Record Offset can name, which also
     * stops a chain that loops. */
    size_t total = 0;
    uint8_t sector = card->log_dir_index;
    for(uint8_t record = 0; record < 4; record++) {
        if(sector == 0 || sector >= card->sector_count - 2) break;
        if(total + card->sector_size > capacity) break;

        size_t got = read(context, sector, out + total, capacity - total);
        if(got < ITSO_TAP_RECORD_LEN) break;
        total += ITSO_TAP_RECORD_LEN;

        uint8_t next = itso_sct_entry(card, dir, dir_len, sector);
        if(next == card->log_dir_index) break;
        sector = next;
    }
    return total;
}

/** Decode one 5-byte IPE Directory Entry (TS 1000-2 clause 6.1). */
void itso_parse_dir_entry(ItsoProduct* product, const uint8_t* entry, uint8_t index) {
    product->dir_index = index;
    product->on_card = true;

    /* OID is 13 bits spanning bytes 0 and 1, below the extension flag.
     *
     * The flag is not decoration: TS 1000-2 Annex B gives IPE owners two
     * numbering ranges, 1-8000 with the flag clear and 8192-16383 with it set.
     * Table B4 builds the extended value by prefixing the 13 bits with 0b001,
     * so the operator number is 8192 higher than the bits alone suggest.
     * Ignoring the flag silently reports a different, real operator. */
    product->oid_extended = (entry[0] & 0x80) != 0;
    uint16_t oid_bits = (uint16_t)itso_bits(entry, 1, 13);
    product->oid = product->oid_extended ? (uint16_t)(0x2000 | oid_bits) : oid_bits;
    product->typ = (uint8_t)itso_bits(entry, 14, 5);
    product->ptyp = (uint8_t)itso_bits(entry, 19, 5);
    product->value_group = (entry[3] & 0x80) != 0;
    product->foreign_iin = (entry[3] & 0x40) != 0;
    product->expiry = (ItsoDate)itso_bits(entry, 26, 14);
    product->status = ItsoProductStatusUnknown;
}

/** Decode the 5-byte Log Directory Entry (TS 1000-2 clause 8.1). */
static void itso_parse_log_entry(ItsoCard* card, const uint8_t* entry) {
    card->log_entry_valid = true;
    card->log_normal_mode = (entry[0] & 0x80) != 0;
    card->log_ptr = (uint8_t)itso_bits(entry, 1, 5);
    card->log_eei = (uint8_t)(entry[0] & 0x03);
    card->log_dts = itso_bits(entry, 8, 24);
    card->log_record_offset = (uint8_t)itso_bits(entry, 32, 2);
    card->log_passback = (uint8_t)(entry[4] & 0x3F);
}

bool itso_parse_directory(ItsoCard* card, const uint8_t* data, size_t len) {
    if(!card->shell_valid) return false;

    uint16_t needed = itso_sct_offset(card) + card->sct_len + 1;
    if(len < needed) return false;

    uint8_t bitmap = itso_bits(data, 6, 6);
    card->shell_blocked = (bitmap & 0x01) != 0;

    /* Bits 2:1 say whether the final directory entry is a log entry. Code 0b10 is
     * a legacy encoding that TS 1000-2 5.1.2 tells us to read as 0b01. */
    uint8_t log_config = (bitmap >> 1) & 0x03;
    card->log_dir_index = (log_config == 0) ? 0 : card->dir_entries;

    card->dir_sequence = data[itso_sct_offset(card) + card->sct_len];

    /* The Directory InstanceID follows DIRS# (TS 1000-2 table 8): key and shell
     * iteration nibbles, then the ISAM that last sealed the directory. */
    size_t instance = itso_sct_offset(card) + card->sct_len + 1;
    if(instance + 5 <= len) {
        card->dir_kid = data[instance] >> 4;
        card->shell_iteration = data[instance] & 0x0F;
        card->dir_isam = itso_bits(data + instance + 1, 0, 32);
        card->dir_instance_valid = true;
    }

    /* Exactly as many slots as the directory lists products, before any is
     * decoded, so the array is allocated once rather than grown per entry. */
    uint8_t listed = 0;
    for(uint8_t i = 1; i <= card->dir_entries; i++) {
        if(i == card->log_dir_index) continue;
        if(!itso_is_blank(data + 2 + (i - 1) * ITSO_DIR_ENTRY_LEN, ITSO_DIR_ENTRY_LEN)) listed++;
    }
    if(listed > ITSO_MAX_PRODUCTS) listed = ITSO_MAX_PRODUCTS;
    card->product_count = 0;
    itso_card_reserve(card, listed);

    for(uint8_t i = 1; i <= card->dir_entries; i++) {
        const uint8_t* entry = data + 2 + (i - 1) * ITSO_DIR_ENTRY_LEN;

        if(i == card->log_dir_index) {
            if(!itso_is_blank(entry, ITSO_DIR_ENTRY_LEN)) itso_parse_log_entry(card, entry);
            continue;
        }

        /* Unused directory entries are all zeros. */
        if(itso_is_blank(entry, ITSO_DIR_ENTRY_LEN)) continue;

        if(card->product_count >= ITSO_MAX_PRODUCTS) break;

        ItsoProduct* product = itso_card_next_product(card);
        if(!product) break;
        itso_parse_dir_entry(product, entry, i);

        /* Walk the chain terminator to learn whether the product was ever used.
         * TS 1000-2 5.1.5.2: self = unused, S-2 = blocked, S-1 = active. */
        uint8_t sector = i;
        for(uint8_t hop = 0; hop < card->sector_count; hop++) {
            uint8_t next = itso_sct_entry(card, data, len, sector);
            if(next == sector) {
                product->status = ItsoProductStatusUnused;
                break;
            } else if(next == card->sector_count - 2) {
                product->status = ItsoProductStatusBlocked;
                break;
            } else if(next == card->sector_count - 1) {
                product->status = ItsoProductStatusActive;
                break;
            } else if(next == 0) {
                break; /* Free sector: the chain is broken, leave status unknown. */
            }
            sector = next;
        }
    }

    card->dir_valid = true;
    return true;
}

const uint8_t* itso_dir_entry(const uint8_t* dir, size_t len, uint8_t index) {
    if(index == 0) return NULL;
    size_t offset = 2 + (size_t)(index - 1) * ITSO_DIR_ENTRY_LEN;
    if(offset + ITSO_DIR_ENTRY_LEN > len) return NULL;
    return dir + offset;
}
