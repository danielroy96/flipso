/**
 * @file itso_parse.c
 * @brief Decoders for the ITSO Shell data groups.
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed shell
 * must produce an empty result rather than a crash.
 */
#include "itso_i.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ITSO_IPE_BLOCK_LEN   4 /* BL for every IPE type we decode. */
#define ITSO_INSTANCE_ID_LEN 8
#define ITSO_SEAL_LEN        8

/* Format Version Code of the Compact-Shell page media (TS 1000-10 clause 5.3):
 * a MIFARE Ultralight / Infineon my-d, the family SPT's paper tickets use. The
 * other Type 2 tag CMDs (9 NTAG, 10 Ultralight EV1) carry a full shell, so a
 * compact shell is specifically this one. */
#define ITSO_FVC_ULTRALIGHT 4

void itso_card_init(ItsoCard* card) {
    memset(card, 0, sizeof(ItsoCard));
}

void itso_card_reset(ItsoCard* card) {
    free(card->products);
    free(card->taps);
    memset(card, 0, sizeof(ItsoCard));
}

void itso_card_free(ItsoCard* card) {
    itso_card_reset(card);
}

bool itso_card_equal(const ItsoCard* a, const ItsoCard* b) {
    if(a->product_count != b->product_count || a->tap_count != b->tap_count) return false;
    if(a->product_count &&
       memcmp(a->products, b->products, (size_t)a->product_count * sizeof(ItsoProduct)) != 0) {
        return false;
    }
    if(a->tap_count && memcmp(a->taps, b->taps, (size_t)a->tap_count * sizeof(ItsoTap)) != 0) {
        return false;
    }
    /* Everything else, less where the arrays live and how much room is spare
     * behind them, which depend on how the card was built rather than what it
     * says. */
    ItsoCard x = *a, y = *b;
    x.products = y.products = NULL;
    x.product_capacity = y.product_capacity = 0;
    x.taps = y.taps = NULL;
    x.tap_capacity = y.tap_capacity = 0;
    return memcmp(&x, &y, sizeof(ItsoCard)) == 0;
}

/**
 * Make room for @p count products in all, keeping the ones already there.
 * The slots beyond them are zeroed. Capped at ITSO_MAX_CARD_PRODUCTS.
 *
 * @return false when the room could not be had; the card is unchanged then.
 */
static bool itso_card_reserve(ItsoCard* card, uint8_t count) {
    if(count > ITSO_MAX_CARD_PRODUCTS) count = ITSO_MAX_CARD_PRODUCTS;
    if(count <= card->product_capacity) return true;
    ItsoProduct* grown = realloc(card->products, (size_t)count * sizeof(ItsoProduct));
    if(!grown) return false;
    memset(
        grown + card->product_capacity,
        0,
        (size_t)(count - card->product_capacity) * sizeof(ItsoProduct));
    card->products = grown;
    card->product_capacity = count;
    return true;
}

/** The next product slot, zeroed and counted, or NULL when there is no room. */
static ItsoProduct* itso_card_next_product(ItsoCard* card) {
    if(card->product_count >= ITSO_MAX_CARD_PRODUCTS) return NULL;
    if(!itso_card_reserve(card, (uint8_t)(card->product_count + 1))) return NULL;
    ItsoProduct* product = &card->products[card->product_count++];
    memset(product, 0, sizeof(*product));
    return product;
}

/* ------------------------------------------------------------------ */
/* ITSO Shell Environment Data Group (TS 1000-2 clause 4)             */
/* ------------------------------------------------------------------ */

/**
 * True for a Compact ITSO Shell (TS 1000-2 table 4): three bytes holding only
 * ShellLength, ShellBitMap, ShellFormatRevision and FVC, the rest of the shell
 * implied by the CMD.
 *
 * The three stored bytes are a prefix of the full shell's, so a full shell
 * cannot be told from a compact one by those alone - a full shell's byte 2 is
 * the first BCD pair of the IIN (0x63), where a compact shell's is the FVC. That
 * is the discriminator: an empty ShellBitMap (no full-shell directory), the
 * fixed ShellLength and ShellFormatRevision of TS 1000-10 table 42, and an FVC
 * that names a compact-shell platform rather than looking like the IIN.
 */
static bool itso_shell_is_compact(const uint8_t* data, size_t len) {
    if(len < 3) return false;
    uint8_t shell_len = (uint8_t)itso_bits(data, 0, 6);
    uint8_t bitmap = (uint8_t)itso_bits(data, 6, 6);
    uint8_t format_rev = (uint8_t)itso_bits(data, 12, 4);
    return shell_len == 6 && bitmap == 0 && format_rev == 1 && data[2] == ITSO_FVC_ULTRALIGHT;
}

/**
 * The header tests, reported individually.
 *
 * itso_looks_like_shell() and itso_shell_card_number() both answer yes or no;
 * this is the same work with the reason kept, so that a rejected shell can say
 * which test it failed. ItsoShellAccepted means only that the header is
 * plausible - the geometry has not been looked at yet.
 */
static ItsoShellReject itso_shell_header_reject(const uint8_t* data, size_t len) {
    /* A compact shell has no IIN to check and is only three bytes long, so it is
     * settled before the full-shell tests that would reject it as short. */
    if(itso_shell_is_compact(data, len)) return ItsoShellAccepted;
    /* The IIN is the only fixed marker: ITSO's registered issuer number, 633597,
     * held as six BCD digits at byte 2. */
    if(len < 24) return ItsoShellRejectShort;
    if(data[2] != 0x63 || data[3] != 0x35 || data[4] != 0x97) return ItsoShellRejectIin;
    /* A full-shell bitmap with bit 0 clear is a shell with no directory to walk,
     * so there is nothing for us to show. (A genuine compact shell, caught
     * above, is a different thing that we do decode.) */
    if((itso_bits(data, 6, 6) & 0x01) == 0) return ItsoShellRejectCompact;
    return ItsoShellAccepted;
}

bool itso_looks_like_shell(const uint8_t* data, size_t len) {
    ItsoShellReject reject = itso_shell_header_reject(data, len);
    return reject != ItsoShellRejectShort && reject != ItsoShellRejectIin;
}

/**
 * The Luhn "double-add-double" check digit for the 17 ISRN digits before it
 * (ISO/IEC 7812-1), as an ASCII char. Returns 0 for a non-digit in the input.
 */
static char itso_isrn_check_digit(const char* isrn) {
    uint32_t sum = 0;
    bool doubled = true; /* Start doubling from the digit left of the check digit. */
    for(int8_t i = ITSO_ISRN_DIGITS - 2; i >= 0; i--) {
        if(isrn[i] < '0' || isrn[i] > '9') return 0;
        uint8_t digit = isrn[i] - '0';
        if(doubled) {
            digit *= 2;
            if(digit > 9) digit -= 9;
        }
        sum += digit;
        doubled = !doubled;
    }
    return (char)('0' + (10 - (sum % 10)) % 10);
}

/** Luhn check over the 18 ISRN digits (ISO/IEC 7812-1). */
static bool itso_isrn_check(const char* isrn) {
    char expected = itso_isrn_check_digit(isrn);
    return expected && isrn[ITSO_ISRN_DIGITS - 1] == expected;
}

bool itso_shell_card_number(const uint8_t* data, size_t len, char* out) {
    if(itso_shell_header_reject(data, len) != ItsoShellAccepted) return false;

    if(itso_shell_is_compact(data, len)) {
        /* A compact shell stores no identity: it is implied by the CMD and is the
         * same for every card of it (TS 1000-10 table 42 - IIN 633597, OID 8189,
         * ISSN 0). So this is the media type's number, not a per-card one; the
         * card's real serial is its chip UID. */
        /* IIN 633597, OID 8189, ISSN 0000000 - 17 digits, then the check below. */
        memcpy(out, "63359781890000000", 17);
        out[17] = itso_isrn_check_digit(out);
        out[ITSO_ISRN_DIGITS] = '\0';
        return true;
    }

    /* ISRN = IIN(6) + OID(4) + ISSN(7) + check digit, all BCD. TS 1000-2 4.1.4. */
    itso_bcd(data, 16, 6, out);
    itso_bcd(data, 40, 4, out + 6);
    itso_bcd(data, 56, 7, out + 10);
    itso_bcd(data, 84, 1, out + 17);
    return true;
}

/**
 * Expand a Compact ITSO Shell into the card, filling in the platform parameters
 * the CMD implies rather than stores (TS 1000-10 table 42 for CMD4).
 *
 * The three stored bytes give ShellLength, ShellBitMap, ShellFormatRevision and
 * FVC; everything else - the identity, the geometry, the expiry - is fixed by
 * the CMD. There is no SCT and a single directory entry, so the sector-chain
 * machinery the full shell drives does not apply and the geometry check that
 * guards it is not run.
 */
static bool itso_parse_compact_shell(ItsoCard* card, const uint8_t* data, size_t len) {
    card->shell_compact = true;
    itso_shell_card_number(data, len, card->isrn);
    card->isrn_check_ok = itso_isrn_check(card->isrn);

    card->shell_len = (uint8_t)itso_bits(data, 0, 6); /* 6 */
    card->format_rev = (uint8_t)itso_bits(data, 12, 4); /* 1 */
    card->fvc = data[2]; /* 4 */

    /* Implied platform parameters (TS 1000-10 table 42). */
    card->iin = 633597;
    card->oid = 8189; /* Reserved OID used for compact shells. */
    card->ksc = 0;
    card->kvc = 1;
    card->expiry = 0x3FFF; /* EXP: does not expire for the foreseeable future. */
    card->sector_size = 32; /* B: one 32-byte sector for IPE storage. */
    card->sector_count = 1; /* S */
    card->dir_entries = 1; /* E: a single directory entry. */
    card->sct_len = 0; /* No Sector Chain Table. */

    /* No SECRC: the Compact Shell Dataset has none (TS 1000-2 table 4), and the
     * checksum the full shell carries covers a full shell's elements. */
    card->shell_valid = true;
    return true;
}

bool itso_parse_shell(ItsoCard* card, const uint8_t* data, size_t len) {
    card->shell_reject = itso_shell_header_reject(data, len);
    if(card->shell_reject != ItsoShellAccepted) return false;
    if(itso_shell_is_compact(data, len)) return itso_parse_compact_shell(card, data, len);
    itso_shell_card_number(data, len, card->isrn);

    uint8_t bitmap = itso_bits(data, 6, 6);
    card->isrn_check_ok = itso_isrn_check(card->isrn);

    card->format_rev = (uint8_t)itso_bits(data, 12, 4);
    card->shell_len = (uint8_t)itso_bits(data, 0, 6);
    card->fvc = data[11];
    card->ksc = data[12];
    card->kvc = data[13];
    card->expiry = itso_bits(data, 114, 14); /* 2 RFU bits precede the 14-bit DATE. */
    card->sector_size = data[16];
    card->sector_count = data[17];
    card->dir_entries = data[18];
    card->sct_len = data[19];

    card->mcrn_present = (bitmap & 0x02) != 0;
    if(card->mcrn_present && len >= 30) {
        /* BCD, terminated and padded with 0xF to a fixed 10 bytes. */
        char digits[21];
        itso_bcd(data, 160, 20, digits);
        char* out = card->mcrn;
        for(uint8_t i = 0; i < 20 && digits[i] != 'F'; i++) {
            *out++ = digits[i];
        }
        *out = '\0';
    }

    /* The SECRC covers every element of the dataset before it (TS 1000-2 clause
     * 4.1.15), so ShellLength is what locates it: whether the shell carries an
     * MCRN decides whether it sits at byte 22 or byte 30, and the buffer may be
     * longer than the dataset either way - a CMD7 reader gets back a whole file
     * rather than as many bytes as the shell claims to use.
     *
     * Stored low byte first, which is the order Annex A appends a CRC to a
     * transmission in. Confirmed against five cards from four schemes; nothing
     * in the clause itself says which way round a "two byte binary integer"
     * goes, and the other order would fail every card. */
    size_t dataset_len = (size_t)card->shell_len * ITSO_SHELL_BLOCK_LEN;
    if(dataset_len >= 4 && dataset_len <= len) {
        card->secrc_stored =
            (uint16_t)(data[dataset_len - 2] | ((uint16_t)data[dataset_len - 1] << 8));
        card->secrc_computed = itso_crc_b(data, dataset_len - 2);
        card->secrc_valid = card->secrc_stored == card->secrc_computed;
        card->secrc_checked = true;
    }

    /* The operator and issuer numbers are worked out from these digits, so one
     * that is not a decimal digit - BCD a misread or a corrupt card left behind -
     * would turn into a real-looking operator number that is nobody's. The check
     * digit is left to the Luhn check. */
    for(uint8_t i = 0; i < ITSO_ISRN_DIGITS - 1; i++) {
        if(card->isrn[i] < '0' || card->isrn[i] > '9') {
            card->shell_reject = ItsoShellRejectNumber;
            return false;
        }
    }
    card->oid = (uint16_t)((card->isrn[6] - '0') * 1000 + (card->isrn[7] - '0') * 100 +
                           (card->isrn[8] - '0') * 10 + (card->isrn[9] - '0'));
    card->iin = 0;
    for(uint8_t i = 0; i < 6; i++) {
        card->iin = card->iin * 10 + (uint32_t)(card->isrn[i] - '0');
    }

    /* Sanity-check the geometry before anything downstream trusts it. */
    if(card->sector_size == 0 || card->sector_count < 4 || card->dir_entries == 0 ||
       card->dir_entries > ITSO_MAX_PRODUCTS || card->sct_len == 0 || card->sct_len > 64) {
        card->shell_reject = ItsoShellRejectGeometry;
        return false;
    }

    card->shell_valid = true;
    return true;
}

/* ------------------------------------------------------------------ */
/* Directory Data Group (TS 1000-2 clause 5)                          */
/* ------------------------------------------------------------------ */

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
static void itso_parse_dir_entry(ItsoProduct* product, const uint8_t* entry, uint8_t index) {
    product->present = true;
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
    product->expiry = (uint16_t)itso_bits(entry, 26, 14);
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

/* ------------------------------------------------------------------ */
/* Type 2 tag page media (TS 1000-10 section 5, CMD4)                  */
/* ------------------------------------------------------------------ */

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
#define ITSO_T2_SHELL_OFFSET    ITSO_TYPE2_SHELL_OFFSET
#define ITSO_T2_DIR_OFFSET      27
#define ITSO_T2_INSTANCE_OFFSET 32
#define ITSO_T2_SEAL_OFFSET     56

/* The physical page offsets the Space Saving IPE's dataset is spread across
 * (TS 1000-10 clauses 5.6.2-5.6.3). Reassembling them in this order gives the
 * one contiguous dataset TS 1000-5 table 48 defines: the static elements, then
 * the rewritable dynamic elements, then the one-time-programmable ones. */
#define ITSO_T2_STATIC_OFFSET   40 /* Pages 10-13: 16 bytes of static data. */
#define ITSO_T2_STATIC_LEN      16
#define ITSO_T2_DYN_DATA_OFFSET 16 /* Pages 4-5: 8 bytes of rewritable dynamic data. */
#define ITSO_T2_DYN_DATA_LEN    8
#define ITSO_T2_OTP_OFFSET      12 /* Page 3: 4 bytes of OTP dynamic data. */
#define ITSO_T2_OTP_LEN         4
#define ITSO_T2_DATASET_LEN     (ITSO_T2_STATIC_LEN + ITSO_T2_DYN_DATA_LEN + ITSO_T2_OTP_LEN)

static void itso_parse_instance_id(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    size_t dataset_len);

ItsoType2Kind itso_type2_kind(const uint8_t* pages, size_t len) {
    /* Checked first, because a read cut short can still hold page 6: a compact
     * shell with its Seal missing would decode, and be saved, as a ticket whose
     * product is half there. */
    if(len < ITSO_CMD4_MEMORY_LEN) return ItsoType2Incomplete;

    const uint8_t* shell = pages + ITSO_T2_SHELL_OFFSET;
    size_t shell_len = len - ITSO_T2_SHELL_OFFSET;
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

/* ------------------------------------------------------------------ */
/* Full-shell Type 2 media (TS 1000-10 clauses 10 and 11: CMD9, CMD10) */
/* ------------------------------------------------------------------ */

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

/**
 * Decode a Space Saving IPE's area element: TYP 27's 100-bit GeoValidity (table
 * 50) or TYP 28/29's 68-bit AreaValidity (tables 53 and 57). Both start at bit
 * 60 of the dataset and share one coding - a top nibble that, when non-zero, is
 * a LocDefType-200; when zero, the bit below it choosing a reference fare code or
 * an actual fare value, held in the rest of the element.
 */
static void itso_space_area(ItsoSpaceSaving* ss, const uint8_t* ds, uint8_t bits) {
    uint8_t locdef = (uint8_t)itso_bits(ds, 60, 4);
    if(locdef == 0) {
        ss->area_kind = itso_bits(ds, 64, 1) ? ItsoAreaFareValue : ItsoAreaFareCode;
        /* The code or value fills 63 or 95 bits; the low 32 are kept, which any
         * fare and any fare table an operator could print will fit. */
        ss->area_value = itso_bits(ds, 60 + bits - 32, 32);
    } else {
        /* A LOCE of LocDefType 200+n fills the rest of the element. Every ticket
         * seen so far carries a fare code instead, so the location itself is not
         * decoded: its type is kept, and the screen says it is not decoded. */
        ss->area_kind = ItsoAreaLocation;
        ss->area_value = (uint32_t)locdef + 200;
    }
}

/** LastUseDTS, where the type has one. Zero is "never used" at creation (and
 *  a DTS of zero decodes to 2028, so it must not be shown as a time). */
static void itso_space_last_use(ItsoSpaceSaving* ss, const uint8_t* ds) {
    ss->has_last_use = true;
    ss->last_use_dts = itso_bits(ds, 168, 24);
}

/**
 * Where a TYP 29 was last used: a 4-byte LOCE whose LocDefType (200-203) the
 * TYP29UsageRecCode gives (table 58). Held in @c from, rendered through the same
 * location decoder a full ticket's are.
 *
 * SPT's Subway gates record a bus fare stage (202) whose stage number is the
 * station, 1-15, and whose machine number is the gate - as Ryan Murphy found,
 * with Partick at 2 and Hillhead at 4. On an SPT ticket the stage is shown as the
 * station it names; anywhere else it stays a fare stage.
 */
static void itso_space_usage_place(ItsoProduct* product, uint8_t def_type, const uint8_t* loce) {
    const uint8_t loc2[7] = {def_type, loce[0], loce[1], loce[2], loce[3], 0, 0};
    itso_parse_location(loc2, sizeof(loc2), ItsoLocStructLoc2, &product->from);
    if(itso_is_blank(loce, 4)) product->from.valid = false; /* Never used. */

    if(def_type == 202 && product->oid == ITSO_OID_SPT_SUBWAY_TICKET) {
        const char* station = itso_spt_subway_station(loce[3]);
        if(station) snprintf(product->from.text, sizeof(product->from.text), "%s", station);
    }
}

/**
 * Decode a Space Saving IPE (TYP 27, 28 or 29) from a card's page memory into
 * @p product and @c card->space (TS 1000-5 clauses 2.14-2.16).
 *
 * Each dataset is a fixed sequence of fields at known bit offsets, unlike a full
 * IPE where a bitmap says which optional elements are present. It is physically
 * split across three page regions, so it is reassembled into one buffer first
 * and the offsets below are into that. All four layouts - TYP 27, 28, and TYP 29
 * at revisions 1 and 2 - fill 16 static and 12 dynamic bytes exactly, and agree
 * on the first 31 bits and on where the pass flags and area sit.
 *
 * TYP 27 was confirmed field for field against a real SPT Subway day ticket read
 * 2026-09-27, and TYP 29 revision 1 against Ryan Murphy's published dump of 21
 * Subway singles and returns. TYP 28 and TYP 29 revision 2 follow the spec alone.
 * The elements a full ticket also has go into @c product->ticket so they render
 * through the shared path.
 */
static void itso_parse_space_saving(
    ItsoCard* card,
    ItsoProduct* product,
    const uint8_t* pages,
    size_t len) {
    if(len < ITSO_CMD4_MEMORY_LEN) return;
    /* A CMD4 carries only these three (TS 1000-10 clause 5.6). Any other type in
     * the directory entry is left as the directory entry describes it, rather
     * than read through a layout that is not its own. */
    if(product->typ != ItsoTypPeriodCompact && product->typ != ItsoTypCarnet &&
       product->typ != ItsoTypMultiUse) {
        return;
    }

    uint8_t ds[ITSO_T2_DATASET_LEN];
    memcpy(ds, pages + ITSO_T2_STATIC_OFFSET, ITSO_T2_STATIC_LEN);
    memcpy(ds + ITSO_T2_STATIC_LEN, pages + ITSO_T2_DYN_DATA_OFFSET, ITSO_T2_DYN_DATA_LEN);
    memcpy(
        ds + ITSO_T2_STATIC_LEN + ITSO_T2_DYN_DATA_LEN,
        pages + ITSO_T2_OTP_OFFSET,
        ITSO_T2_OTP_LEN);

    /* TYP 27 and 28 define revision 1 only, TYP 29 revisions 1 and 2. A card
     * claiming another is left as a product decoded from its directory entry. */
    uint8_t rev = (uint8_t)itso_bits(ds, 12, 4);
    bool multi_leg = product->typ == ItsoTypMultiUse && rev == 2;
    if(rev != 1 && !multi_leg) return;

    ItsoSpaceSaving* ss = &card->space;
    memset(ss, 0, sizeof(ItsoSpaceSaving));
    product->space_saving = true;
    product->body_parsed = true;
    product->format_rev = rev;
    product->bitmap = (uint8_t)itso_bits(ds, 6, 6);

    ItsoTicketTerms* t = &product->ticket;
    t->valid = true;
    t->issue_date = (uint16_t)itso_bits(ds, 16, 14); /* IssueDate, a DATE. */
    ss->euro = itso_bits(ds, 30, 1);

    /* Bits 32-39 are PassbackTime and the payment method on TYP 27 and 28; TYP 29
     * spends the same eight bits differently at each revision. PassbackTime is
     * kept even at zero, which means the reader's own rule applies. */
    if(multi_leg) {
        product->has_passback = true;
        product->passback = (uint8_t)itso_bits(ds, 32, 4);
        ss->max_daily_journeys = (uint8_t)itso_bits(ds, 36, 4);
        t->max_transfers = (uint8_t)itso_bits(ds, 40, 4);
    } else {
        if(product->typ != ItsoTypMultiUse) {
            product->has_passback = true;
            product->passback = (uint8_t)itso_bits(ds, 32, 4);
        }
        t->paid_mop = (uint8_t)itso_bits(ds, 36, 4);
        t->amount_paid.value = (int32_t)itso_bits(ds, 40, 16); /* AmountPaid, VALI, pence. */
        t->amount_paid.currency = ss->euro ? 1 : 0;
        t->amount_paid.valid = true;
    }

    /* TYP27/28/29PassFlags share one definition (tables 49, 52 and 56). */
    ss->flags = (uint8_t)itso_bits(ds, 56, 4);
    t->travel_class = (ss->flags & ITSO_SS_FIRST_CLASS) ? 1 : 2;

    switch(product->typ) {
    case ItsoTypPeriodCompact: {
        /* Table 48. Bit 31 is the child flag; GeoValidity runs on into dynamic
         * memory, 100 bits in all. */
        if(itso_bits(ds, 31, 1)) {
            t->children = 1;
        } else {
            t->adults = 1;
        }
        itso_space_area(ss, ds, 100);
        /* Event1 and Event2: two EventTypeCodes, which the spec does not order
         * or otherwise explain, so both are kept and shown as they stand. */
        ss->has_events = true;
        ss->event1 = (uint8_t)itso_bits(ds, 160, 4);
        ss->event2 = (uint8_t)itso_bits(ds, 164, 4);
        itso_space_last_use(ss, ds);
        t->photocard = itso_bits(ds, 192, 24); /* PhotocardNumber, 0 if none. */
        /* TYP27ExpiryDate: days to subtract from the directory expiry. Zero leaves
         * the directory's own date standing. */
        uint8_t expiry_offset = (uint8_t)itso_bits(ds, 216, 8);
        if(expiry_offset && product->expiry > expiry_offset) {
            product->expiry = (uint16_t)(product->expiry - expiry_offset);
        }
        break;
    }

    case ItsoTypCarnet: {
        /* Table 51: a carnet of up to eight day passes (clause 2.15.2). Six
         * one-time-programmable ticks record the days used; issue and expiry day
         * each have a flag instead of a tick. */
        itso_space_area(ss, ds, 68);
        itso_space_last_use(ss, ds);
        uint8_t unused = 0;
        for(uint8_t i = 0; i < sizeof(ss->carnet_ticks); i++) {
            ss->carnet_ticks[i] = (uint8_t)itso_bits(ds, 192 + i * 5, 5);
            if(ss->carnet_ticks[i] == 0) unused++;
        }
        ss->carnet_issue_day = itso_bits(ds, 222, 1);
        ss->carnet_expiry_day = itso_bits(ds, 223, 1);
        /* The expiry-day pass spends no tick, so it is one more left until the
         * day it is for - and on that day the product's own expiry takes over.
         * The issue-day pass is not counted: it is for the day the carnet was
         * bought, which has gone by the time anyone is counting what is left. */
        if(ss->carnet_expiry_day) unused++;
        product->count_kind = ItsoCountPasses;
        product->count = unused;
        break;
    }

    case ItsoTypMultiUse:
        itso_space_area(ss, ds, 68);
        if(!multi_leg) {
            /* Table 55: a carnet of single tickets, or coupons. QtyRemaining counts
             * up from 8191 minus the number bought, so what is left is the
             * difference. */
            product->count_kind = itso_bits(ds, 31, 1) ? ItsoCountCoupons : ItsoCountRides;
            product->count = 8191 - itso_bits(ds, 147, 13);
            uint8_t code = (uint8_t)itso_bits(ds, 144, 3); /* TYP29UsageRecCode. */
            ss->usage_alighted = code & 0x01;
            itso_space_usage_place(product, (uint8_t)(200 + ((code >> 1) & 0x03)), ds + 20);
        } else {
            /* Table 55a: multi-leg journeys. QtyRemaining counts up from 255. */
            ss->journey_start_dts = itso_bits(ds, 128, 24);
            product->count_kind = ItsoCountRides;
            product->count = 255 - itso_bits(ds, 152, 8);
            ss->transfers = (uint8_t)itso_bits(ds, 160, 4);
            ss->daily_journeys = (uint8_t)itso_bits(ds, 164, 4);
            itso_space_last_use(ss, ds);
        }
        break;

    default:
        break;
    }
}

bool itso_parse_type2(ItsoCard* card, const uint8_t* pages, size_t len) {
    /* Only a whole CMD4: a full shell (CMD9, CMD10) is laid out nothing like
     * this fixed mapping, and a short read is not a card at all. */
    if(itso_type2_kind(pages, len) != ItsoType2Compact) return false;
    if(!itso_parse_shell(card, pages + ITSO_T2_SHELL_OFFSET, len - ITSO_T2_SHELL_OFFSET)) {
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

    itso_parse_space_saving(card, product, pages, len);
    return true;
}

/* ------------------------------------------------------------------ */
/* IPE Data Groups (TS 1000-2 clause 6, TS 1000-5 clause 2)           */
/* ------------------------------------------------------------------ */

/** Read a signed 16-bit big-endian value. */
static int32_t itso_int16(const uint8_t* data) {
    return (int16_t)((data[0] << 8) | data[1]);
}

/** Copy a fixed-length ASCII name field, trimming trailing spaces. */
static size_t itso_copy_name(const uint8_t* src, size_t n, char* dst, size_t dst_len) {
    size_t pos = 0;
    for(size_t i = 0; i < n && pos + 1 < dst_len; i++) {
        char c = (char)src[i];
        if(c < 0x20 || c > 0x7E) break;
        dst[pos++] = c;
    }
    while(pos > 0 && dst[pos - 1] == ' ') {
        pos--;
    }
    dst[pos] = '\0';
    return pos;
}

/**
 * True when 12-bit TS# @p a is newer than @p b, allowing for wraparound.
 *
 * TS 1000-2 clause 7.2.4.2: TS# counts up by one per record written and rolls
 * over from 0xFFF to zero. Only a handful of records exist at once, so the
 * newer of two is simply the one a short distance ahead modulo the range.
 */
static bool itso_ts_newer(uint16_t a, uint16_t b) {
    uint16_t delta = (uint16_t)(a - b) & 0x0FFF;
    return delta != 0 && delta < 0x800;
}

uint8_t itso_value_records(const uint8_t* group, size_t len, uint8_t sector_size, size_t* offset) {
    /* The value group starts at a sector boundary, so its offset cannot be found
     * without a sector size. A shell that reached the transports has one, but the
     * decoder is also driven directly by the host tests. */
    if(sector_size == 0 || len < 2) return 0;

    size_t dataset_len = (size_t)itso_bits(group, 0, 6) * ITSO_IPE_BLOCK_LEN;
    if(dataset_len < 4 || dataset_len > len) return 0;

    /* TS 1000-2 clause 5.1.5.3 rule 2: the group begins in the next chained
     * sector after the one the IPE data group ends in. */
    size_t ipe_group_len = dataset_len + ITSO_INSTANCE_ID_LEN + ITSO_SEAL_LEN;
    size_t sectors_used = (ipe_group_len + sector_size - 1) / sector_size;
    size_t vg_offset = sectors_used * sector_size;
    if(vg_offset + 2 > len) return 0;

    /* Table 14: the upper five flags of VGBitMap count the records the group
     * supports, and its LSB flags a Value Group Extension we have no use for. */
    uint8_t vg_bitmap = itso_bits(group + vg_offset, 6, 6);
    uint8_t records = 0;
    for(uint8_t bit = 1; bit < 6; bit++) {
        if(vg_bitmap & (1 << bit)) records++;
    }
    /* Keep the count to what is actually there, so a caller may slice records
     * out without re-checking the length of each. */
    while(records && vg_offset + 2 + (size_t)records * ITSO_VALUE_RECORD_LEN > len) {
        records--;
    }
    if(records == 0) return 0;

    if(offset) *offset = vg_offset + 2;
    return records;
}

uint8_t itso_previous_value_records(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    size_t* offset) {
    size_t current = 0;
    uint8_t records = itso_value_records(group, len, sector_size, &current);
    if(records == 0) return 0;

    /* The current copy is its dataset - VGLength blocks, extension included -
     * then an InstanceID and a Seal (TS 1000-2 clause 7), and the previous copy
     * starts in the sector after (annex A.3.2.1). */
    const uint8_t* vg = group + current - 2;
    size_t dataset_len = (size_t)itso_bits(vg, 0, 6) * ITSO_IPE_BLOCK_LEN;
    size_t group_len = dataset_len + ITSO_INSTANCE_ID_LEN + ITSO_SEAL_LEN;
    size_t previous = current - 2 + (group_len + sector_size - 1) / sector_size * sector_size;
    if(previous + 2 + (size_t)records * ITSO_VALUE_RECORD_LEN > len) return 0;

    /* Both copies support the same number of records (annex A.3.2.2), so their
     * headers match; anything else is not the other copy. */
    if(group[previous] != vg[0] || group[previous + 1] != vg[1]) return 0;

    *offset = previous + 2;
    return records;
}

/**
 * The VGXRef of the Value Group Extension a value group carries, or 0.
 *
 * @p records_offset is where the value records start, as itso_value_records()
 * reported it. The extension sits after every record the group supports - not
 * only the ones written - which is what the VGBitMap count is (TS 1000-2
 * clause 7.5).
 */
static uint8_t itso_vgx_ref(const uint8_t* group, size_t len, size_t records_offset) {
    const uint8_t* vg = group + records_offset - 2;
    uint8_t vg_bitmap = itso_bits(vg, 6, 6);
    if(!(vg_bitmap & 0x01)) return 0;

    uint8_t supported = 0;
    for(uint8_t bit = 1; bit < 6; bit++) {
        if(vg_bitmap & (1 << bit)) supported++;
    }
    size_t vgx = records_offset + (size_t)supported * ITSO_VALUE_RECORD_LEN;
    if(vgx + 2 > len) return 0;
    /* Table 15b: the two top bits of VGXRef say how to read the rest. Only
     * "reference number 0-255" names an extension TS 1000-5 defines. */
    if(itso_bits(group + vgx, 6, 2) != 0) return 0xFF;
    return group[vgx + 1];
}

/**
 * Decode one value record: the common header, and whichever of a balance or a
 * counter the IPE type keeps in the tail.
 *
 * Those two are what a history is made of, and they are the only part of a
 * record whose meaning is worth working out for a transaction that is over.
 * Offsets in the TS 1000-5 tables are absolute from the start of the data
 * group, so an element the table puts at offset N sits at record byte N-2: the
 * two-byte value group header precedes the first record.
 */
static void itso_decode_value_record(ItsoValueRecord* out, const uint8_t* record, uint8_t typ) {
    memset(out, 0, sizeof(*out));

    /* TS 1000-2 table 15 defines the first ten bytes of every value record
     * identically, whatever the IPE type. */
    out->txn = (uint8_t)itso_bits(record, 0, 4);
    out->ts = (uint16_t)itso_bits(record, 4, 12);
    out->dts = itso_bits(record, 16, 24);

    switch(typ) {
    case ItsoTypStoredTravelRights: /* TS 1000-5 table 4. */
    case ItsoTypChargeToAccount1: /* Table 12: the same bytes, counting up. */
        itso_decode_money(itso_int16(record + 10), (record[12] >> 4) & 0x0F, &out->amount);
        break;

    case ItsoTypLoyalty1:
        /* Table 9: points rather than money, and three bytes of them. */
        out->count = itso_bits(record, 80, 24);
        out->has_count = true;
        break;

    case ItsoTypPeriodTicket:
        /* Tables 29, 29a and 3.29: a stock of unactivated passes, six bits. */
        out->count = itso_bits(record, 80, 6);
        out->has_count = true;
        break;

    case ItsoTypChargeToAccount2: /* Table 17: transactions this charge period. */
    case ItsoTypJourneyTicket: /* Tables 33, 33a, 33b: rides left. */
    case ItsoTypReservationTicket: /* Table 139. */
    case ItsoTypVoucher: /* Table 38. */
    case ItsoTypTolling: /* Table 42, identical to 38. */
        out->count = record[10];
        out->has_count = true;
        break;

    default:
        /* A type whose tail we do not decode. The header still read cleanly, so
         * the record belongs in the history with a date and no amount. */
        break;
    }
}

/** Keep one record in the product's history, newest first, deduplicated. */
static void itso_add_value_record(ItsoProduct* product, const ItsoValueRecord* record) {
    /* The same record twice: a card read again offers the records the last read
     * already saw. TS# identifies a write within a product, and the timestamp
     * separates the two records that could share one across a wrap of the
     * 12-bit counter - which takes 4096 transactions to reach. */
    for(uint8_t i = 0; i < product->value_history_count; i++) {
        if(product->value_history[i].ts == record->ts &&
           product->value_history[i].dts == record->dts) {
            return;
        }
    }

    /* Newest first, by TS# rather than by time: see itso_parse_value_records. */
    uint8_t pos = product->value_history_count;
    for(uint8_t i = 0; i < product->value_history_count; i++) {
        if(itso_ts_newer(record->ts, product->value_history[i].ts)) {
            pos = i;
            break;
        }
    }
    if(pos >= ITSO_MAX_VALUE_RECORDS) return; /* Older than everything we keep. */

    for(uint8_t i = product->value_history_count; i > pos; i--) {
        if(i < ITSO_MAX_VALUE_RECORDS) product->value_history[i] = product->value_history[i - 1];
    }
    product->value_history[pos] = *record;
    if(product->value_history_count < ITSO_MAX_VALUE_RECORDS) product->value_history_count++;
}

/**
 * Add a run of value records to the product's history, and return the newest.
 *
 * Records the card has not written yet are all zeros, and are skipped: a blank
 * TS# of zero would beat a live record that has since wrapped past it, and a
 * blank DTS decodes to 2028, which is later than any real timestamp.
 *
 * @param live    when not NULL, the live record from another run: a record here
 *                newer than its TS# @p live_ts is skipped rather than kept.
 */
static const uint8_t* itso_add_value_run(
    ItsoProduct* product,
    const uint8_t* run,
    uint8_t records,
    const uint8_t* live,
    uint16_t live_ts) {
    const uint8_t* newest = NULL;
    uint16_t newest_ts = 0;
    for(uint8_t i = 0; i < records; i++) {
        const uint8_t* record = run + (size_t)i * ITSO_VALUE_RECORD_LEN;
        if(itso_is_blank(record, ITSO_VALUE_RECORD_LEN)) continue;

        ItsoValueRecord decoded;
        itso_decode_value_record(&decoded, record, product->typ);
        if(live && itso_ts_newer(decoded.ts, live_ts)) continue;
        decoded.on_card = true;
        itso_add_value_record(product, &decoded);

        if(newest == NULL || itso_ts_newer(decoded.ts, newest_ts)) {
            newest = record;
            newest_ts = decoded.ts;
        }
    }
    return newest;
}

/**
 * Decode the Value Record Data Group bound to an IPE (TS 1000-2 clause 7).
 *
 * It is not a purse feature: any IPE may carry one, and table 15 gives every
 * value record the same 10-byte common header. The five bytes after that header
 * are defined per IPE type in TS 1000-5, which is what the switch at the end is
 * for.
 *
 * Every record is decoded, not only the live one. The others are the
 * transactions before it, which is the only statement a card keeps.
 */
static void itso_parse_value_records(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    uint8_t sector_size) {
    size_t offset = 0;
    uint8_t records = itso_value_records(group, len, sector_size, &offset);
    if(records == 0) return;
    product->vgx_ref = itso_vgx_ref(group, len, offset);

    /* Records are written cyclically, so the live one is the newest, and TS# is
     * what orders them. The DTS cannot do that job: it has a resolution of one
     * minute, and a tap that spends a ride writes a record in the same minute as
     * the tap in - two records, one timestamp, and picking either at random. */
    const uint8_t* newest = itso_add_value_run(product, group + offset, records, NULL, 0);

    /* A software anti-tear card keeps a second copy behind the first, holding
     * the other half of the history (TS 1000-10 annex A.3.2). The copy the
     * chain names first is the current one, and anything in the other that is
     * newer is a transaction torn before the directory was relinked to it -
     * which a POST discards and so does this (clause A.3.2.4.1). Should the
     * current copy hold nothing at all, the previous copy is what a POST falls
     * back on, and so is this (clause A.3.2.4.3). */
    size_t previous = 0;
    uint8_t previous_records = itso_previous_value_records(group, len, sector_size, &previous);
    if(previous_records) {
        uint16_t ceiling = newest ? (uint16_t)itso_bits(newest, 4, 12) : 0;
        const uint8_t* fallback =
            itso_add_value_run(product, group + previous, previous_records, newest, ceiling);
        if(!newest) newest = fallback;
    }
    if(newest == NULL) return;

    product->value_parsed = true;
    product->value_txn = (uint8_t)itso_bits(newest, 0, 4);
    product->value_ts = (uint16_t)itso_bits(newest, 4, 12);
    product->value_dts = itso_bits(newest, 16, 24);
    product->value_isam = itso_bits(newest, 40, 32);
    product->value_action_seq = newest[9];

    /* What the tail of the newest record says about the product as it stands.
     * The balance and the counter come from the record itself, which
     * itso_decode_value_record() has already read: they are the same field
     * whether they are being shown as the current value or as a line of the
     * history, and deciding what those bytes mean in two places is how the two
     * views come to disagree. What is left here is the part of the tail that
     * describes the product rather than the transaction. */
    const ItsoValueRecord* live = &product->value_history[0];

    switch(product->typ) {
    case ItsoTypStoredTravelRights:
        /* TS 1000-5 table 4. */
        product->balance = live->amount;
        product->journey_legs = newest[12] & 0x0F;
        itso_decode_money(
            (int32_t)itso_bits(newest, 104, 13),
            (newest[12] >> 4) & 0x0F,
            &product->cumulative_fare);
        product->has_journey = true;
        {
            /* TYP2ValueFlags is three bits wide; flag n is bit n of it. */
            uint8_t flags = (uint8_t)itso_bits(newest, 117, 3);
            product->auto_top_up = (flags & 0x01) != 0;
            product->priority_override = (flags & 0x02) != 0;
            product->auto_top_up_internal = (flags & 0x04) != 0;
        }
        break;

    case ItsoTypLoyalty1:
        /* TS 1000-5 table 9: points rather than money, and three bytes of them. */
        product->count_kind = ItsoCountPoints;
        product->count = live->count;
        break;

    case ItsoTypChargeToAccount1:
        /* TS 1000-5 table 12. The layout matches TYP 2 exactly; what differs is
         * the meaning, so the same bytes are read and flagged as spend. */
        product->balance = live->amount;
        product->balance_is_spend = true;
        product->journey_legs = newest[12] & 0x0F;
        itso_decode_money(
            (int32_t)itso_bits(newest, 104, 12),
            (newest[12] >> 4) & 0x0F,
            &product->cumulative_fare);
        product->has_journey = true;
        product->priority_override = (itso_bits(newest, 116, 4) & 0x02) != 0;
        break;

    case ItsoTypChargeToAccount2:
        /* TS 1000-5 table 17: a count of transactions in the charge period, and
         * the date that count was last cleared. */
        product->count_kind = ItsoCountTransactions;
        product->count = live->count;
        product->last_reset = (uint16_t)itso_bits(newest, 90, 14);
        product->has_last_reset = true;
        product->journey_legs = newest[14] & 0x0F;
        product->has_journey = true;
        break;

    case ItsoTypPeriodTicket:
        /* TS 1000-5 tables 29, 29a and 3.29, which agree across all three
         * revisions. A period ticket keeps a stock of unactivated passes, and
         * two expiry dates: one for the stock, one for the pass in use. */
        product->count_kind = ItsoCountPasses;
        product->count = live->count;
        {
            uint8_t flags = (uint8_t)itso_bits(newest, 86, 6);
            product->auto_renew = (flags & 0x01) != 0;
            /* Clear, the ticket is one continuous period and AutoRenewQuantity1
             * counts days rather than passes (rules 5 and 6 of 2.9.1.4). */
            product->stored_passes = (flags & 0x02) != 0;
        }
        product->stored_expiry = (uint16_t)itso_bits(newest, 92, 14);
        product->has_stored_expiry = true;
        product->current_expiry = (uint16_t)itso_bits(newest, 106, 14);
        product->has_current_expiry = true;
        break;

    case ItsoTypJourneyTicket:
        /* TS 1000-5 tables 33, 33a and 33b agree on the first three bytes: a
         * journey ticket counts rides rather than money. Reading these bytes as a
         * purse would render the ride count and the transfer count as one
         * 16-bit amount, and take a currency from a flags byte. */
        product->count_kind = ItsoCountRides;
        product->count = live->count;
        product->transfers = newest[11];
        product->has_transfers = true;
        product->auto_renew = (newest[12] & 0x01) != 0;
        product->ticket_used = (newest[12] & 0x02) != 0; /* TYP23ValueFlags bit 1. */
        if(product->format_rev >= 3) {
            /* Table 33b added an expiry for the unactivated rides; in revisions 1
             * and 2 these bytes are RFU and reading them would invent a date. */
            product->stored_expiry = (uint16_t)itso_bits(newest, 106, 14);
            product->has_stored_expiry = true;
        }
        break;

    case ItsoTypReservationTicket:
        /* TS 1000-5 table 139. */
        product->count_kind = ItsoCountRides;
        product->count = live->count;
        break;

    case ItsoTypVoucher:
    case ItsoTypTolling:
        /* TS 1000-5 tables 38 and 42, which are identical. */
        product->count_kind = ItsoCountRides;
        product->count = live->count;
        product->auto_renew = (newest[11] & 0x01) != 0;
        break;

    default:
        /* A type whose value record tail we do not decode. The common header
         * still read cleanly, so this is not a failure to report. */
        break;
    }
}

/**
 * Elements every IPE dataset puts in the same place (TS 1000-5 clause 2).
 *
 * RemoveDate sits at byte 2 for every type. ProductRetailer follows it at byte
 * 3 for every type except the two identity ones, which put an accounting
 * reference there instead - so reading it unconditionally would report a
 * scheme's internal account code as the shop that sold the card.
 *
 * The optional IIN is the odd one out: TS 1000-2 clause 6.2.6 appends it after
 * the dataset padding rather than inline, so it is the last three bytes of the
 * dataset whenever IPEBitMap bit 0 is set.
 */
static void itso_parse_ipe_common(
    ItsoProduct* product,
    const uint8_t* data,
    size_t dataset_len,
    uint8_t bitmap) {
    if(dataset_len < 3) return;

    product->remove_date = data[2];
    product->has_remove_date = true;

    bool has_retailer = product->typ != ItsoTypId && product->typ != ItsoTypEntitlement;
    if(has_retailer && dataset_len >= 5) {
        product->retailer = (uint16_t)((data[3] << 8) | data[4]);
        /* Zero is "not recorded" rather than operator zero, which is RFU. */
        product->has_retailer = product->retailer != 0;
    }

    if((bitmap & 0x01) && dataset_len >= 3) {
        product->iin = itso_bits(data, (uint32_t)(dataset_len - 3) * 8, 24);
        product->has_iin = true;
    }
}

/**
 * The IPE InstanceID that follows the dataset (TS 1000-2 table 11).
 *
 * This is the only unique identity a product carries: the shell has an ISRN and
 * the directory entry has an owner and a type, but nothing names one particular
 * season ticket except the ISAM that created it and that ISAM's sequence
 * number.
 */
static void itso_parse_instance_id(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    size_t dataset_len) {
    if(dataset_len + ITSO_INSTANCE_ID_LEN > len) return;

    const uint8_t* id = group + dataset_len;
    product->key_id = (id[0] >> 4) & 0x0F;
    product->iteration = id[0] & 0x0F;
    product->isam_id = itso_bits(id, 8, 32);
    product->isam_seq = itso_bits(id, 40, 24);
    product->instance_valid = true;
}

/**
 * TYP 2, 4 and 5: the purse and charge-to-account datasets.
 *
 * The three share a shape - flags, then limits, then a deposit and a validity
 * start - but not a set of offsets, so each is laid out separately rather than
 * parameterised into something that reads as though the spec were tidier than
 * it is. TS 1000-5 tables 2, 10 and 15.
 */
static void itso_parse_purse_ipe(ItsoProduct* product, const uint8_t* data, size_t len) {
    /* The currency for every amount in the dataset is the value record's
     * ValueCurrencyCode, which the value record parser has already decoded. */
    uint8_t currency = product->balance.valid ? product->balance.currency : 0;

    switch(product->typ) {
    case ItsoTypStoredTravelRights:
        if(len < 22) return;
        itso_decode_money(itso_int16(data + 6), currency, &product->top_up_threshold);
        itso_decode_money(itso_int16(data + 8), currency, &product->top_up_amount);
        product->has_top_up = product->top_up_amount.value != 0;
        itso_decode_money(itso_int16(data + 10), currency, &product->max_value);
        itso_decode_money(itso_int16(data + 12), currency, &product->max_negative);
        product->has_limits = true;
        itso_decode_money(itso_int16(data + 14), (data[20] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[19] & 0x0F;
        product->deposit_vat = (uint16_t)itso_bits(data, 164, 12);
        product->has_deposit = product->deposit.value != 0;
        /* StartDateAutoTopUp: a DATE at byte 16, two bits into the byte. */
        product->start = (uint16_t)itso_bits(data, 128, 14);
        product->has_start = product->start != 0;
        break;

    case ItsoTypChargeToAccount1:
        if(len < 16) return;
        itso_decode_money(itso_int16(data + 6), currency, &product->max_value);
        product->has_limits = true;
        itso_decode_money(itso_int16(data + 8), (data[14] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[13] & 0x0F;
        product->has_deposit = product->deposit.value != 0;
        product->start = (uint16_t)itso_bits(data, 80, 14); /* StartDateCTA at byte 10. */
        product->has_start = product->start != 0;
        product->end_date = (uint16_t)itso_bits(data, 94, 14); /* EndDate at byte 11.75. */
        product->has_end_date = true;
        break;

    case ItsoTypChargeToAccount2:
        if(len < 18) return;
        product->weeks_per_period = data[6];
        product->max_transactions = data[7];
        product->has_charge_period = true;
        itso_decode_money(itso_int16(data + 8), currency, &product->max_value);
        product->has_limits = true;
        itso_decode_money(itso_int16(data + 10), (data[16] >> 4) & 0x0F, &product->deposit);
        product->deposit_mop = data[15] & 0x0F;
        product->has_deposit = product->deposit.value != 0;
        product->start = (uint16_t)itso_bits(data, 96, 14); /* StartDateCTA at byte 12. */
        product->has_start = product->start != 0;
        product->end_date = (uint16_t)itso_bits(data, 110, 14); /* EndDate at byte 13.75. */
        product->has_end_date = true;
        break;

    default:
        break;
    }
}

/**
 * Shared tail of the TYP 14 and TYP 16 datasets: optional identity and location
 * elements, laid out back to back in bitmap order.
 *
 * @param pos     offset of the first optional element.
 * @param names   true for TYP 16, which carries the holder's name.
 */
static void itso_parse_id_optionals(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    size_t pos,
    uint8_t bitmap,
    bool names) {
    uint8_t bit = 1;

    if(bitmap & (1 << bit)) {
        if(pos + 4 > len) return;
        product->secondary_holder_id = itso_bits(data + pos, 0, 32);
        product->has_secondary_holder = true;
        pos += 4;
    }
    bit++;

    if(names) {
        if(bitmap & (1 << bit)) {
            /* Forename and surname are each a length byte followed by that many
             * ASCII bytes: TS 1000-5 says the elements are compressed to size. */
            if(pos >= len) return;
            uint8_t forename_len = data[pos++];
            if(pos + forename_len > len) return;
            size_t written =
                itso_copy_name(data + pos, forename_len, product->name, ITSO_NAME_LEN);
            pos += forename_len;

            if(pos >= len) return;
            uint8_t surname_len = data[pos++];
            if(pos + surname_len > len) return;
            if(written + 1 < ITSO_NAME_LEN) {
                if(written > 0) product->name[written++] = ' ';
                itso_copy_name(
                    data + pos, surname_len, product->name + written, ITSO_NAME_LEN - written);
            }
            pos += surname_len;

            product->has_name = product->name[0] != '\0';
        }
        bit++;
    }

    if(bitmap & (1 << bit)) {
        if(pos + 2 > len) return;
        product->half_days = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        product->has_half_days = true;
        pos += 2;
        if(pos >= len) return;
        pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    }
    bit++;

    if(bitmap & (1 << bit)) {
        if(pos >= len) return;
        itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
    }
}

/** TYP 16 (ITSO ID) and TYP 14 (Entitlement): holder identity and entitlement. */
static void itso_parse_id_ipe(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    size_t optionals;
    uint32_t start_bit = 0;
    uint32_t expiry_bit;
    size_t entitlement_offset;

    if(product->typ == ItsoTypId) {
        if(format_rev >= 2) {
            /* TS 1000-5 table 22a. */
            start_bit = 130; /* EntitlementStartDate at byte 16.25. */
            expiry_bit = 144; /* EntitlementExpiryDate at byte 18. */
            entitlement_offset = 29;
            optionals = 31;
        } else {
            /* TS 1000-5 table 22. */
            expiry_bit = 130; /* EntitlementExpiryDate at byte 16.25. */
            entitlement_offset = 27;
            optionals = 29;
        }
    } else {
        if(format_rev >= 2) {
            /* TS 1000-5 table 20a. */
            start_bit = 90; /* EntitlementStartDate at byte 11.25. */
            expiry_bit = 104; /* EntitlementExpiryDate at byte 13. */
            entitlement_offset = 20;
            optionals = 22;
        } else {
            /* TS 1000-5 table 20. */
            expiry_bit = 90;
            entitlement_offset = 18;
            optionals = 20;
        }
    }

    /* Fixed elements both revisions of both types share (tables 20 and 22).
     * IDFlags carries the things a concessionary pass is actually judged on at
     * the gate: whether the card is photo-personalised, and whether a companion
     * travels free with the holder. */
    if(len >= 7) {
        product->id_flags = data[5];
        product->has_id_flags = true;
        /* PassbackTime is six bits, two bits into byte 6. */
        product->passback = (uint8_t)itso_bits(data, 50, 6);
        product->has_passback = true;
    }

    /* DateOfBirth is only on the ID type; TYP 14 puts HolderID at byte 7. It is
     * a Datef (BCD yyyymmdd), not a DATE, so it can predate the DATE epoch. */
    if(product->typ == ItsoTypId && len >= 11) {
        char digits[9];
        itso_bcd(data, 56, 8, digits);
        bool numeric = true;
        for(uint8_t i = 0; i < 8; i++) {
            if(digits[i] == 'F') numeric = false;
        }
        /* An all-zero Datef is the documented "explicitly no date". */
        if(numeric && !itso_is_blank(data + 7, 4)) {
            product->dob_year = (uint16_t)((digits[0] - '0') * 1000 + (digits[1] - '0') * 100 +
                                           (digits[2] - '0') * 10 + (digits[3] - '0'));
            product->dob_month = (uint8_t)((digits[4] - '0') * 10 + (digits[5] - '0'));
            product->dob_day = (uint8_t)((digits[6] - '0') * 10 + (digits[7] - '0'));
            product->has_dob = product->dob_month >= 1 && product->dob_month <= 12 &&
                               product->dob_day >= 1 && product->dob_day <= 31;
        }
    }

    if(entitlement_offset + 2 > len) return;

    if(start_bit) {
        product->start = (uint16_t)itso_bits(data, start_bit, 14);
        product->has_start = true;
    }
    product->sub_expiry = (uint16_t)itso_bits(data, expiry_bit, 14);
    product->has_sub_expiry = true;

    product->entitlement_code = data[entitlement_offset];
    product->concession_class = data[entitlement_offset + 1];
    product->has_entitlement = true;

    if(product->typ == ItsoTypId) {
        /* Tables 22 and 22a agree up to HolderID; revision 2 then inserts the
         * two-byte EntitlementStartDate, pushing both deposits along by two. */
        product->cpicc = (uint16_t)((data[3] << 8) | data[4]);
        product->has_cpicc = product->cpicc != 0;
        product->language = data[11];
        product->holder_id = itso_bits(data, 96, 32);
        product->has_holder_id = product->holder_id != 0;
        if(data[6] & 0x80) product->rounding |= ITSO_ROUNDING_ENABLED;
        if(data[16] & 0x80) product->rounding |= ITSO_ROUNDING_FLAG;
        if(data[16] & 0x40) product->rounding |= ITSO_ROUNDING_VALUE;

        const size_t d = format_rev >= 2 ? 20 : 18; /* DepositMethodOfPayment. */
        const uint8_t valcs = data[d + 4];
        product->deposit_mop = data[d] >> 4;
        product->deposit_vat = (uint16_t)itso_bits(data, (uint32_t)d * 8 + 4, 12);
        product->shell_deposit_mop = data[d + 2] >> 4;
        product->shell_deposit_vat = (uint16_t)itso_bits(data, (uint32_t)(d + 2) * 8 + 4, 12);
        itso_decode_money(itso_int16(data + d + 5), valcs >> 4, &product->deposit);
        itso_decode_money(itso_int16(data + d + 7), valcs & 0x0F, &product->shell_deposit);
        product->has_deposit = product->deposit.value != 0;
        product->has_shell_deposit = product->shell_deposit.value != 0;
    }

    itso_parse_id_optionals(product, data, len, optionals, bitmap, product->typ == ItsoTypId);
}

/**
 * TYP 22: pre-defined area-based ticket, all three format revisions.
 *
 * Tables 27, 27a and 3.27 agree on everything up to byte 13: flags, passback,
 * issue date, expiry time, class. After that each revision moves things - a
 * DTS start in revisions 1 and 2 against a date and a time in 3, a two-byte
 * AmountPaid in revision 1 against four bytes later - and the optional elements
 * come in a different order in each.
 */
static void itso_parse_period_ipe(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    ItsoTicketTerms* t = &product->ticket;
    /* Where the mandatory part ends, which is where the optional elements start. */
    const size_t fixed = format_rev >= 3 ? 29 : format_rev == 2 ? 28 : 26;
    if(format_rev == 0 || len < fixed) return;

    t->flags = (uint16_t)itso_bits(data, 40, 16);
    product->passback = (uint8_t)itso_bits(data, 58, 6);
    product->has_passback = true;
    t->issue_date = (uint16_t)itso_bits(data, 64, 14);
    t->expiry_time = (uint16_t)itso_bits(data, 78, 11);
    t->renew_quantity = (uint8_t)itso_bits(data, 90, 6);
    t->travel_class = (uint8_t)itso_bits(data, 96, 3);
    t->validity_code = (uint8_t)itso_bits(data, 99, 5);

    /* Everything from ValidityStartDTS on sits one byte later in revision 3,
     * whose start date and time take four bytes where the DTS took three. */
    size_t b; /* Byte of PromotionCode. */
    if(format_rev >= 3) {
        product->start = (uint16_t)itso_bits(data, 106, 14); /* ValidityStartDate. */
        product->has_start = true;
        t->start_time = (uint16_t)itso_bits(data, 125, 11);
        t->has_start_time = true;
        b = 17;
    } else {
        t->valid_from_dts = itso_bits(data, 104, 24);
        b = 16;
    }
    t->promotion_code = data[b];
    t->valid_days = data[b + 1];
    t->adults = data[b + 2];
    t->children = data[b + 3];
    t->concessions = data[b + 4];

    /* Zero in both the amount and its currency is the documented "not used",
     * and a real zero fare is not worth a line either. */
    const uint8_t valc = data[b + 5] & 0x0F;
    int32_t paid;
    size_t mop_byte;
    if(format_rev == 1) {
        paid = itso_int16(data + 22);
        mop_byte = 24;
    } else {
        paid = (int32_t)itso_bits(data, (uint32_t)(b + 6) * 8, 32);
        mop_byte = b + 10;
    }
    if(paid) itso_decode_money(paid, valc, &t->amount_paid);
    t->paid_mop = data[mop_byte] >> 4;
    t->vat = (uint16_t)itso_bits(data, (uint32_t)mop_byte * 8 + 4, 12);
    t->valid = true;

    size_t pos = fixed;
    if(bitmap & (1 << 4)) {
        if(pos + 2 > len) return;
        product->cpicc = (uint16_t)((data[pos] << 8) | data[pos + 1]);
        product->has_cpicc = true;
        pos += 2;
    }

    if(format_rev == 1) {
        /* Revision 1 flags the two locations independently and puts PassDuration
         * after them, so it cannot be found without walking both. */
        if(bitmap & (1 << 1)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
        }
        if(bitmap & (1 << 2)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
        }
        if((bitmap & (1 << 3)) && pos < len) {
            t->pass_duration = data[pos];
            t->has_pass_duration = true;
        }
        return;
    }

    if(bitmap & (1 << 3)) {
        if(format_rev == 2) {
            if(pos + 1 > len) return;
            t->pass_duration = data[pos];
            pos += 1;
        } else {
            /* Table 3.27: a unit code, a 12-bit count of it, then the days the
             * stock of passes is extended by on auto-renew. */
            if(pos + 4 > len) return;
            t->duration_unit = data[pos] >> 4;
            t->pass_duration = (uint16_t)itso_bits(data, (uint32_t)pos * 8 + 4, 12);
            t->stock_duration = (uint16_t)((data[pos + 2] << 8) | data[pos + 3]);
            t->has_stock_duration = true;
            pos += 4;
        }
        t->has_pass_duration = true;
    }

    if(!(bitmap & (1 << 1))) return;
    pos += 5; /* RouteCode. */

    if(pos >= len) return;
    pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    if(pos >= len) return;
    itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
}

/**
 * The terms of a TYP 23 journey ticket: tables 31, 31a and 31b.
 *
 * The same elements as a period ticket's, less the day filters, plus a
 * photocard number and the optional mode group. Revision 2 widened AmountPaid to
 * four bytes; revision 3 added a ValidityStartDTS after IssueDate, which pushes
 * everything from ValidityCode on three bytes later, and widened the ride value.
 */
static void itso_parse_journey_terms(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    ItsoTicketTerms* t = &product->ticket;
    const size_t fixed = format_rev >= 3 ? 33 : format_rev == 2 ? 29 : 27;
    if(format_rev == 0 || len < fixed) return;

    if(data[5] & 0x02) product->ticket_used = true; /* TYP23Flags UsedChecked. */
    product->passback = (uint8_t)itso_bits(data, 50, 6);
    product->has_passback = true;
    t->issue_date = (uint16_t)itso_bits(data, 58, 14);

    /* Everything from ValidityCode on moves three bytes in revision 3. */
    const uint32_t shift = format_rev >= 3 ? 24 : 0;
    if(format_rev >= 3) t->valid_from_dts = itso_bits(data, 72, 24);
    t->validity_code = (uint8_t)itso_bits(data, 72 + shift, 5);
    t->expiry_time = (uint16_t)itso_bits(data, 77 + shift, 11);
    t->travel_class = (uint8_t)itso_bits(data, 93 + shift, 3);

    const size_t b = 12 + shift / 8; /* PartySizeAdult. */
    t->adults = data[b];
    t->children = data[b + 1];
    t->concessions = data[b + 2];
    const uint8_t valc = data[b + 3] & 0x0F;

    int32_t paid;
    size_t mop_byte;
    if(format_rev == 1) {
        paid = itso_int16(data + 16);
        mop_byte = 18;
    } else {
        paid = (int32_t)itso_bits(data, (uint32_t)(b + 4) * 8, 32);
        mop_byte = b + 8;
    }
    if(paid) itso_decode_money(paid, valc, &t->amount_paid);
    t->paid_mop = data[mop_byte] >> 4;
    t->vat = (uint16_t)itso_bits(data, (uint32_t)mop_byte * 8 + 4, 12);
    t->photocard = itso_bits(data, (uint32_t)(mop_byte + 2) * 8, 32);
    t->promotion_code = data[mop_byte + 6];
    product->cpicc = (uint16_t)((data[mop_byte + 7] << 8) | data[mop_byte + 8]);
    product->has_cpicc = product->cpicc != 0;
    if(format_rev >= 3) t->renew_quantity = data[32];
    t->valid = true;

    /* Bit 3 of every revision: mode, transfer limit, time limit, ride value. */
    if((bitmap & (1 << 3)) && fixed + (format_rev >= 3 ? 7 : 5) <= len) {
        t->mode = data[fixed] & 0x0F;
        t->max_transfers = data[fixed + 1];
        t->time_limit = data[fixed + 2];
        int32_t ride = format_rev >= 3 ? (int32_t)itso_bits(data, (uint32_t)(fixed + 3) * 8, 32) :
                                         itso_int16(data + fixed + 3);
        if(ride) itso_decode_money(ride, valc, &t->ride_value);
        t->has_mode_group = true;
    }
}

/**
 * TYP 23: pre-defined specific journey ticket, all three format revisions.
 *
 * The three revisions carry the same elements in the same order; what moves is
 * where they start. Revision 2 added RouteCode and six bytes of mandatory
 * fields over revision 1, and revision 3 added AutoRenewQuantity and widened
 * ValueOfRideJourney from two bytes to four.
 */
static void itso_parse_journey_ipe(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    size_t pos;

    itso_parse_journey_terms(product, data, len, bitmap, format_rev);

    if(format_rev >= 3) {
        /* TS 1000-5 table 31b: AutoRenewQuantity ends the mandatory part. */
        pos = 33;
        if(bitmap & (1 << 3)) pos += 8; /* Mode, transfers, time limit, ride value. */
    } else if(format_rev == 2) {
        /* TS 1000-5 table 31a. */
        pos = 29;
        if(bitmap & (1 << 3)) pos += 6;
    } else if(format_rev == 1) {
        /* TS 1000-5 table 31. Revision 1 has no RouteCode and flags the two
         * locations independently - bit 2 the origin, bit 1 the destination -
         * where later revisions gate both on bit 1 together with RouteCode. */
        pos = 27;
        if(bitmap & (1 << 3)) pos += 6;

        if(bitmap & (1 << 2)) {
            if(pos >= len) return;
            pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
        }
        if(bitmap & (1 << 1)) {
            if(pos >= len) return;
            itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
        }
        return;
    } else {
        return; /* Revision 0 is not defined; guessing an offset would invent a station. */
    }

    if(!(bitmap & (1 << 1))) return;
    pos += 5; /* RouteCode. */

    if(pos >= len) return;
    pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    if(pos >= len) return;
    itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
}

void itso_parse_ipe(ItsoProduct* product, const uint8_t* group, size_t len, uint8_t sector_size) {
    if(len < 2) return;

    uint8_t ipe_length = itso_bits(group, 0, 6);
    uint8_t bitmap = itso_bits(group, 6, 6);
    product->bitmap = bitmap;
    product->format_rev = itso_bits(group, 12, 4);

    size_t dataset_len = (size_t)ipe_length * ITSO_IPE_BLOCK_LEN;
    if(dataset_len < 4 || dataset_len > len) return;

    product->body_parsed = true;

    itso_parse_ipe_common(product, group, dataset_len, bitmap);
    itso_parse_instance_id(product, group, len, dataset_len);

    /* The Value Record Data Group belongs to the IPE rather than to its type, so
     * decode it wherever VGP says one is there. It is decoded before the dataset
     * because the dataset's amounts take their currency from the value record. */
    if(product->value_group) {
        itso_parse_value_records(product, group, len, sector_size);
    }

    switch(product->typ) {
    case ItsoTypStoredTravelRights:
    case ItsoTypChargeToAccount1:
    case ItsoTypChargeToAccount2:
        itso_parse_purse_ipe(product, group, dataset_len);
        break;

    case ItsoTypId:
    case ItsoTypEntitlement:
        itso_parse_id_ipe(product, group, dataset_len, bitmap, product->format_rev);
        break;

    case ItsoTypPeriodTicket:
        itso_parse_period_ipe(product, group, dataset_len, bitmap, product->format_rev);
        break;

    case ItsoTypJourneyTicket:
        itso_parse_journey_ipe(product, group, dataset_len, bitmap, product->format_rev);
        break;

    default:
        /* Other product types are reported from their directory entry and,
         * where they carry one, their value record. */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Cyclic log / Transient Ticket Records (TS 1000-5 clause 3)         */
/* ------------------------------------------------------------------ */

/**
 * Size in bytes of each optional Transient Ticket data group, by bitmap bit.
 *
 * Bits 8 to 10 are RFU in format revisions 1 and 2, and a record of those
 * revisions must not have a size attributed to them: doing so would shift every
 * group that follows. TS 1000-5 tables 59a, 60a, 63 and 66.
 */
static uint8_t itso_tt_group_len(uint8_t bit, uint8_t format_rev) {
    switch(bit) {
    case 0:
        return 5; /* AMT: amount paid. */
    case 1:
        return 7; /* DEST: destination, LOC2. */
    case 2:
        return 1; /* IPEID: pointer to a directory entry. */
    case 3:
        return 7; /* ORGN: origin, LOC2. */
    case 5:
        return 7; /* RC: routing code, LOC2. */
    case 7:
        return 3; /* IIN. */
    case 8:
        return format_rev >= 3 ? 3 : 0; /* CIPE: candidate IPEs. */
    case 9:
        return format_rev >= 4 ? 10 : 0; /* ENTRY: where the journey checked in. */
    case 10:
        return format_rev >= 4 ? 3 : 0; /* ENTRY OID. */
    default:
        return 0; /* RFU, or the variable-length user defined group. */
    }
}

/** Decode one Transient Ticket Record. Returns false if the record is empty. */
static bool itso_parse_tap(ItsoTap* tap, const uint8_t* data, size_t len) {
    if(len < 7 || itso_is_blank(data, 7)) return false;

    uint8_t tt_length = itso_bits(data, 0, 6);
    tap->format_rev = itso_bits(data, 12, 4);
    if(tap->format_rev == 0) return false;

    /* Keep the record inside both its own declared length and the buffer. */
    size_t record_len = (size_t)tt_length * ITSO_IPE_BLOCK_LEN;
    if(record_len < 7 || record_len > len) record_len = len;

    uint16_t bitmap = (uint16_t)itso_bits(data, 16, 12);
    tap->transaction_type = (uint8_t)itso_bits(data, 28, 4);
    tap->dts = itso_bits(data, 32, 24);

    size_t pos = 7;
    for(uint8_t bit = 0; bit <= 10; bit++) {
        if(!(bitmap & (1 << bit))) continue;
        uint8_t group_len = itso_tt_group_len(bit, tap->format_rev);
        if(group_len == 0) continue;
        if(pos + group_len > record_len) break;

        const uint8_t* group = data + pos;
        switch(bit) {
        case 0:
            /* MOP nibble, then currency nibble, then a 2-byte amount. */
            itso_decode_money(itso_int16(group + 1), group[0] & 0x0F, &tap->amount);
            tap->mop = (uint8_t)((group[0] >> 4) & 0x0F);
            tap->has_mop = true;
            /* NoFareCharged is bit 27 of the group - three RFU bits into byte 3,
             * not its LSB: the operator carried the holder and left the fare
             * outstanding against the IPE. */
            tap->no_fare_charged = itso_bits(group, 27, 1) != 0;
            /* Table 60 put the two flags ahead of it into revision 2's RFU. */
            if(tap->format_rev >= 2) {
                tap->companion = itso_bits(group, 24, 1) != 0;
                tap->return_ticket = itso_bits(group, 25, 1) != 0;
            }
            tap->vat = (uint16_t)itso_bits(group, 28, 12);
            tap->has_vat = tap->vat != 0;
            break;
        case 1:
            itso_parse_location(group, group_len, ItsoLocStructLoc2, &tap->destination);
            break;
        case 2:
            tap->ipe_pointer = (uint8_t)(group[0] & 0x1F);
            tap->has_ipe_pointer = tap->ipe_pointer != 0;
            break;
        case 3:
            itso_parse_location(group, group_len, ItsoLocStructLoc2, &tap->origin);
            break;
        case 5:
            /* The routing code is a location too - typically the NLC of the
             * point a fare is charged "via". */
            itso_parse_location(group, group_len, ItsoLocStructLoc2, &tap->route);
            break;
        case 7:
            tap->iin = itso_bits(group, 0, 24);
            tap->has_iin = true;
            break;
        case 8:
            /* Four 5-bit directory entry pointers, then four flag bits. */
            for(uint8_t i = 0; i < 4; i++) {
                tap->cipe[i] = (uint8_t)itso_bits(group, (uint32_t)i * 5, 5);
            }
            tap->invalid_travel = (itso_bits(group, 20, 4) & 0x01) != 0;
            tap->inspected = (itso_bits(group, 20, 4) & 0x02) != 0;
            tap->has_cipe = true;
            break;
        case 9:
            /* The check-in record copied forward, so a tap out says where the
             * journey began even though the gate that wrote it did not see it. */
            tap->entry_isam = itso_bits(group, 0, 32);
            tap->entry_isam_seq = itso_bits(group, 32, 24);
            tap->entry_dts = itso_bits(group, 56, 24);
            tap->has_entry = true;
            break;
        case 10:
            tap->entry_oid = (uint16_t)((group[0] << 8) | group[1]);
            tap->entry_iin_index = group[2];
            tap->has_entry_oid = true;
            break;
        default:
            break;
        }
        pos += group_len;
    }

    /* A record is stored as an Orphan IPE Data Group (TS 1000-5 clause 3.2), so
     * its InstanceID follows the dataset: key and iteration, then the ISAM of
     * the reader that wrote it. That names whose gate or bus took the tap. */
    size_t declared = (size_t)tt_length * ITSO_IPE_BLOCK_LEN;
    if(declared >= 7 && declared + 5 <= len) {
        tap->writer_isam = itso_bits(data + declared + 1, 0, 32);
        tap->has_writer = tap->writer_isam != 0;
    }

    tap->present = true;
    return true;
}

/**
 * Keep one decoded tap, if it is not one we already have.
 *
 * A tap carries no sequence number of its own, so a record is identified by
 * when it happened and what it was. That is what a saved card needs: reading
 * the same card again offers the records the last read of it already saw, and a
 * journey listed twice reads as two journeys.
 */
static void itso_add_tap(ItsoCard* card, const ItsoTap* tap) {
    for(uint8_t i = 0; i < card->tap_count; i++) {
        if(card->taps[i].dts == tap->dts &&
           card->taps[i].transaction_type == tap->transaction_type) {
            return;
        }
    }
    /* Full: the live log is parsed first, and a saved card offers its records
     * newest first, so what is dropped here is the oldest of what was offered. */
    if(card->tap_count >= ITSO_MAX_TAPS) return;
    /* Grown a slot at a time: a card has only as many as it has journeys, and
     * the few records one read offers are not worth counting ahead of time. */
    if(card->tap_count >= card->tap_capacity) {
        ItsoTap* grown = realloc(card->taps, (size_t)(card->tap_count + 1) * sizeof(ItsoTap));
        if(!grown) return;
        card->taps = grown;
        card->tap_capacity = (uint8_t)(card->tap_count + 1);
    }
    card->taps[card->tap_count++] = *tap;
}

/** Newest first, so the most recent journey is the first thing on screen. */
static void itso_sort_taps(ItsoCard* card) {
    /* At most ITSO_MAX_TAPS records, so an insertion sort is plenty. */
    for(uint8_t i = 1; i < card->tap_count; i++) {
        ItsoTap held = card->taps[i];
        uint32_t held_time = itso_dts_to_unix(held.dts);
        int8_t j = (int8_t)i - 1;
        while(j >= 0 && itso_dts_to_unix(card->taps[j].dts) < held_time) {
            card->taps[j + 1] = card->taps[j];
            j--;
        }
        card->taps[j + 1] = held;
    }
}

void itso_parse_log(ItsoCard* card, const uint8_t* data, size_t len) {
    /* TS 1000-10 clause 8.7.5: the DESFire cyclic log holds fixed-length records.
     * Record Offset in the Log Directory Entry names the *next* slot to be used,
     * so the newest record is the one before it - counted over the slots the log
     * has rather than the ones we have room for. */
    uint8_t slots = (uint8_t)(len / ITSO_TAP_RECORD_LEN);
    if(slots == 0) return;
    uint8_t newest_slot = (uint8_t)((card->log_record_offset + slots - 1) % slots);

    for(uint8_t i = 0; i < slots; i++) {
        ItsoTap tap;
        memset(&tap, 0, sizeof(tap));
        if(!itso_parse_tap(&tap, data + (size_t)i * ITSO_TAP_RECORD_LEN, ITSO_TAP_RECORD_LEN)) {
            continue;
        }

        tap.latest = card->log_entry_valid && card->log_normal_mode && (i == newest_slot);
        tap.on_card = true;
        itso_add_tap(card, &tap);
    }

    itso_sort_taps(card);
}

void itso_parse_log_history(ItsoCard* card, const uint8_t* data, size_t len) {
    /* No latest flag on any of these: the record the card itself calls its
     * newest is in the live log, which has already been parsed. And on_card
     * stays false, because only the file has them - a record still on the card
     * was added from the live log first, and the duplicate is dropped. */
    for(size_t offset = 0; offset + ITSO_TAP_RECORD_LEN <= len; offset += ITSO_TAP_RECORD_LEN) {
        ItsoTap tap;
        memset(&tap, 0, sizeof(tap));
        if(!itso_parse_tap(&tap, data + offset, ITSO_TAP_RECORD_LEN)) continue;
        itso_add_tap(card, &tap);
    }

    itso_sort_taps(card);
}

ItsoProduct* itso_card_add_product(ItsoCard* card, const uint8_t* entry, uint8_t index) {
    ItsoProduct* product = itso_card_next_product(card);
    if(!product) return NULL;
    itso_parse_dir_entry(product, entry, index);
    return product;
}

void itso_product_off_card(ItsoProduct* product, uint32_t last_seen) {
    product->on_card = false;
    product->last_seen = last_seen;
    for(uint8_t i = 0; i < product->value_history_count; i++) {
        product->value_history[i].on_card = false;
    }
}

void itso_parse_value_history(ItsoProduct* product, const uint8_t* data, size_t len) {
    for(size_t offset = 0; offset + ITSO_VALUE_RECORD_LEN <= len;
        offset += ITSO_VALUE_RECORD_LEN) {
        const uint8_t* record = data + offset;
        if(itso_is_blank(record, ITSO_VALUE_RECORD_LEN)) continue;

        /* on_card stays false: these are the records that have rolled off the
         * group since, and only the file has them. */
        ItsoValueRecord decoded;
        itso_decode_value_record(&decoded, record, product->typ);
        itso_add_value_record(product, &decoded);
    }
}

/* ------------------------------------------------------------------ */
/* Raw records, for code that stores them rather than decoding them    */
/* ------------------------------------------------------------------ */

uint8_t itso_shell_sector_size(const uint8_t* data, size_t len) {
    if(!itso_looks_like_shell(data, len)) return 0;
    return data[16]; /* B, TS 1000-2 clause 4.1.9. */
}

const uint8_t* itso_dir_entry(const uint8_t* dir, size_t len, uint8_t index) {
    if(index == 0) return NULL;
    size_t offset = 2 + (size_t)(index - 1) * ITSO_DIR_ENTRY_LEN;
    if(offset + ITSO_DIR_ENTRY_LEN > len) return NULL;
    return dir + offset;
}

bool itso_tap_record_present(const uint8_t* record, size_t len) {
    if(len < 7 || itso_is_blank(record, 7)) return false;
    /* TTFormatRevision zero is not a revision any record is written at, which
     * is how a slot holding something other than a record is told apart from
     * one holding a record of a revision we do not decode. */
    return itso_bits(record, 12, 4) != 0;
}

bool itso_tap_record_newer(const uint8_t* a, const uint8_t* b) {
    /* Through the DTS conversion rather than on the raw field: a DTS is a
     * two's complement count of minutes, so the larger number is the earlier
     * time for every record written before the 2028 epoch. */
    return itso_dts_to_unix(itso_bits(a, 32, 24)) > itso_dts_to_unix(itso_bits(b, 32, 24));
}

bool itso_value_record_newer(const uint8_t* a, const uint8_t* b) {
    return itso_ts_newer((uint16_t)itso_bits(a, 4, 12), (uint16_t)itso_bits(b, 4, 12));
}

bool itso_parse_capping(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    uint8_t valc,
    ItsoCapping* out) {
    memset(out, 0, sizeof(*out));

    size_t offset;
    if(itso_value_records(group, len, sector_size, &offset) == 0) return false;
    uint8_t ref = itso_vgx_ref(group, len, offset);
    if(ref != 1 && ref != 2) return false;

    /* itso_vgx_ref() has already found the extension and bounds-checked its
     * header; find it again rather than widen that function's contract. */
    uint8_t vg_bitmap = itso_bits(group + offset - 2, 6, 6);
    uint8_t supported = 0;
    for(uint8_t bit = 1; bit < 6; bit++) {
        if(vg_bitmap & (1 << bit)) supported++;
    }
    const uint8_t* v = group + offset + (size_t)supported * ITSO_VALUE_RECORD_LEN;
    size_t vgx_len = (size_t)itso_bits(v, 0, 6) * ITSO_IPE_BLOCK_LEN;
    size_t avail = len - (size_t)(v - group);
    if(vgx_len > avail) vgx_len = avail;

    /* Tables AD1 and AD2: the four accumulator sets are 9 bytes apart in the
     * reduced form and 11 in the full one, which adds LastFarePaid. Locations
     * follow them - one for all four in AD1, one per set with a DTS in AD2. */
    const size_t stride = ref == 1 ? 9 : 11;
    const size_t locations = 4 + ITSO_CAP_ACCUMULATORS * stride;
    if(vgx_len < locations) return false;

    out->ref = ref;
    out->strategy = (uint16_t)((v[2] << 8) | v[3]);
    for(uint8_t a = 0; a < ITSO_CAP_ACCUMULATORS; a++) {
        ItsoCapAccumulator* acc = &out->acc[a];
        const size_t base = 4 + a * stride;
        acc->rule = v[base] >> 4;
        size_t amounts;
        if(ref == 1) {
            acc->last_txn = v[base] & 0x0F;
            amounts = base + 1;
        } else {
            itso_decode_money(
                (int16_t)itso_bits(v, (uint32_t)base * 8 + 4, 16), valc, &acc->last_fare);
            acc->last_txn = v[base + 2] & 0x0F;
            amounts = base + 3;
        }
        itso_decode_money(itso_int16(v + amounts), valc, &acc->uncapped);
        itso_decode_money(itso_int16(v + amounts + 2), valc, &acc->day);
        itso_decode_money(itso_int16(v + amounts + 4), valc, &acc->multiday);
        acc->day_count = (uint16_t)((v[amounts + 6] << 8) | v[amounts + 7]);
    }

    size_t pos = locations;
    for(uint8_t a = 0; a < (ref == 1 ? 1 : ITSO_CAP_ACCUMULATORS) && pos < vgx_len; a++) {
        size_t used =
            itso_parse_location(v + pos, vgx_len - pos, ItsoLocStructLoc1, &out->acc[a].location);
        if(used == 0) break;
        pos += used;
        if(ref == 2) {
            if(pos + 3 > vgx_len) break;
            out->acc[a].cap_dts = itso_bits(v + pos, 0, 24);
            pos += 3;
        }
    }

    out->valid = true;
    return true;
}
