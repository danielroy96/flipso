/**
 * @file itso_shell.c
 * @brief The ITSO Shell Environment Data Group (TS 1000-2 clause 4), full and compact.
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "../itso_i.h"

#include <string.h>

/* Format Version Code of the Compact-Shell page media (TS 1000-10 clause 5.3):
 * a MIFARE Ultralight / Infineon my-d, the family SPT's paper tickets use. The
 * other Type 2 tag CMDs (9 NTAG, 10 Ultralight EV1) carry a full shell, so a
 * compact shell is specifically this one. */
#define ITSO_FVC_ULTRALIGHT 4

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
bool itso_shell_is_compact(const uint8_t* data, size_t len) {
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

uint8_t itso_shell_sector_size(const uint8_t* data, size_t len) {
    if(!itso_looks_like_shell(data, len)) return 0;
    return data[16]; /* B, TS 1000-2 clause 4.1.9. */
}
