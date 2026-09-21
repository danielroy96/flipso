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

#define ITSO_IPE_BLOCK_LEN   4 /* BL for every IPE type we decode. */
#define ITSO_INSTANCE_ID_LEN 8
#define ITSO_SEAL_LEN        8
#define ITSO_VALUE_RECORD_LEN 15
#define ITSO_DIR_ENTRY_LEN   5

void itso_card_reset(ItsoCard* card) {
    memset(card, 0, sizeof(ItsoCard));
}

/* ------------------------------------------------------------------ */
/* ITSO Shell Environment Data Group (TS 1000-2 clause 4)             */
/* ------------------------------------------------------------------ */

bool itso_looks_like_shell(const uint8_t* data, size_t len) {
    /* The IIN is the only fixed marker: ITSO's registered issuer number, 633597,
     * held as six BCD digits at byte 2. */
    if(len < 24) return false;
    return data[2] == 0x63 && data[3] == 0x35 && data[4] == 0x97;
}

/** Luhn "double-add-double" check over the 18 ISRN digits (ISO/IEC 7812-1). */
static bool itso_isrn_check(const char* isrn) {
    uint32_t sum = 0;
    bool doubled = true; /* Start doubling from the digit left of the check digit. */
    for(int8_t i = ITSO_ISRN_DIGITS - 2; i >= 0; i--) {
        if(isrn[i] < '0' || isrn[i] > '9') return false;
        uint8_t digit = isrn[i] - '0';
        if(doubled) {
            digit *= 2;
            if(digit > 9) digit -= 9;
        }
        sum += digit;
        doubled = !doubled;
    }
    uint8_t expected = (10 - (sum % 10)) % 10;
    return isrn[ITSO_ISRN_DIGITS - 1] == ('0' + expected);
}

bool itso_shell_card_number(const uint8_t* data, size_t len, char* out) {
    if(!itso_looks_like_shell(data, len)) return false;

    /* A compact shell (bitmap zero) carries only a format version code: there is
     * no directory to walk, so there is nothing for us to show. */
    if((itso_bits(data, 6, 6) & 0x01) == 0) return false;

    /* ISRN = IIN(6) + OID(4) + ISSN(7) + check digit, all BCD. TS 1000-2 4.1.4. */
    itso_bcd(data, 16, 6, out);
    itso_bcd(data, 40, 4, out + 6);
    itso_bcd(data, 56, 7, out + 10);
    itso_bcd(data, 84, 1, out + 17);
    return true;
}

bool itso_parse_shell(ItsoCard* card, const uint8_t* data, size_t len) {
    if(!itso_shell_card_number(data, len, card->isrn)) return false;

    uint8_t bitmap = itso_bits(data, 6, 6);
    card->isrn_check_ok = itso_isrn_check(card->isrn);

    card->oid = (uint16_t)((card->isrn[6] - '0') * 1000 + (card->isrn[7] - '0') * 100 +
                           (card->isrn[8] - '0') * 10 + (card->isrn[9] - '0'));

    card->iin = 0;
    for(uint8_t i = 0; i < 6; i++) {
        card->iin = card->iin * 10 + (uint32_t)(card->isrn[i] - '0');
    }

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

    /* Sanity-check the geometry before anything downstream trusts it. */
    if(card->sector_size == 0 || card->sector_count < 4 || card->dir_entries == 0 ||
       card->dir_entries > ITSO_MAX_PRODUCTS || card->sct_len == 0 || card->sct_len > 64) {
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

/** Decode one 5-byte IPE Directory Entry (TS 1000-2 clause 6.1). */
static void itso_parse_dir_entry(ItsoProduct* product, const uint8_t* entry, uint8_t index) {
    product->present = true;
    product->dir_index = index;

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

    card->product_count = 0;
    for(uint8_t i = 1; i <= card->dir_entries; i++) {
        const uint8_t* entry = data + 2 + (i - 1) * ITSO_DIR_ENTRY_LEN;

        if(i == card->log_dir_index) {
            if(!itso_is_blank(entry, ITSO_DIR_ENTRY_LEN)) itso_parse_log_entry(card, entry);
            continue;
        }

        /* Unused directory entries are all zeros. */
        if(itso_is_blank(entry, ITSO_DIR_ENTRY_LEN)) continue;

        if(card->product_count >= ITSO_MAX_PRODUCTS) break;

        ItsoProduct* product = &card->products[card->product_count];
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

        card->product_count++;
    }

    card->dir_valid = true;
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

/**
 * Decode the Value Record Data Group bound to an IPE (TS 1000-2 clause 7).
 *
 * The group starts at the beginning of the next chained sector after the IPE
 * data group ends (TS 1000-2 clause 5.1.5.3 rule 2). It is not a purse feature:
 * any IPE may carry one, and table 15 gives every value record the same 10-byte
 * common header. The five bytes after that header are defined per IPE type in
 * TS 1000-5, which is what the switch at the end is for.
 */
static void itso_parse_value_records(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    size_t dataset_len,
    uint8_t sector_size) {
    /* The value group starts at a sector boundary, so its offset cannot be found
     * without a sector size. A shell that reached the transports has one, but the
     * decoder is also driven directly by the host tests. */
    if(sector_size == 0) return;

    size_t ipe_group_len = dataset_len + ITSO_INSTANCE_ID_LEN + ITSO_SEAL_LEN;
    size_t sectors_used = (ipe_group_len + sector_size - 1) / sector_size;
    size_t offset = sectors_used * sector_size;
    if(offset + 2 > len) return;

    const uint8_t* vg = group + offset;
    size_t vg_len = len - offset;

    uint8_t vg_bitmap = itso_bits(vg, 6, 6);
    /* Table 14: the upper five flags count the supported records, the LSB flags a
     * Value Group Extension we do not need here. */
    uint8_t records = 0;
    for(uint8_t bit = 1; bit < 6; bit++) {
        if(vg_bitmap & (1 << bit)) records++;
    }
    if(records == 0) return;

    /* Records are written cyclically, so the live one is the newest, and TS# is
     * what orders them. The DTS cannot do that job: it has a resolution of one
     * minute, and a tap that spends a ride writes a record in the same minute as
     * the tap in - two records, one timestamp, and picking either at random.
     *
     * Records the card has not written yet are all zeros. Skip them: a blank TS#
     * of zero would beat a live record that has since wrapped past it. */
    const uint8_t* newest = NULL;
    uint16_t newest_ts = 0;
    for(uint8_t i = 0; i < records; i++) {
        size_t record_offset = 2 + (size_t)i * ITSO_VALUE_RECORD_LEN;
        if(record_offset + ITSO_VALUE_RECORD_LEN > vg_len) break;
        const uint8_t* record = vg + record_offset;
        if(itso_is_blank(record, ITSO_VALUE_RECORD_LEN)) continue;

        uint16_t ts = (uint16_t)itso_bits(record, 4, 12);
        if(newest == NULL || itso_ts_newer(ts, newest_ts)) {
            newest = record;
            newest_ts = ts;
        }
    }
    if(newest == NULL) return;

    /* TS 1000-2 table 15 defines the first ten bytes of every value record
     * identically, whatever the IPE type: what the transaction was, how many
     * times the record has been written, when, and by which POST. */
    product->value_parsed = true;
    product->value_txn = (uint8_t)itso_bits(newest, 0, 4);
    product->value_ts = newest_ts;
    product->value_dts = itso_bits(newest, 16, 24);
    product->value_isam = itso_bits(newest, 40, 32);
    product->value_action_seq = newest[9];

    /* The remaining five bytes are defined per IPE type in TS 1000-5. Offsets in
     * those tables are absolute from the start of the data group, so an element
     * the table puts at offset N sits at record byte N-2: the two-byte value
     * group header precedes the first record. */
    switch(product->typ) {
    case ItsoTypStoredTravelRights:
        /* TS 1000-5 table 4. */
        itso_decode_money(itso_int16(newest + 10), (newest[12] >> 4) & 0x0F, &product->balance);
        product->journey_legs = newest[12] & 0x0F;
        itso_decode_money(
            (int32_t)itso_bits(newest, 104, 13), (newest[12] >> 4) & 0x0F,
            &product->cumulative_fare);
        product->has_journey = true;
        {
            /* TYP2ValueFlags is three bits wide; flag n is bit n of it. */
            uint8_t flags = (uint8_t)itso_bits(newest, 117, 3);
            product->auto_top_up = (flags & 0x01) != 0;
            product->priority_override = (flags & 0x02) != 0;
        }
        break;

    case ItsoTypLoyalty1:
        /* TS 1000-5 table 9: points rather than money, and three bytes of them. */
        product->count_kind = ItsoCountPoints;
        product->count = itso_bits(newest, 80, 24);
        break;

    case ItsoTypChargeToAccount1:
        /* TS 1000-5 table 12. The layout matches TYP 2 exactly; what differs is
         * the meaning, so the same bytes are read and flagged as spend. */
        itso_decode_money(itso_int16(newest + 10), (newest[12] >> 4) & 0x0F, &product->balance);
        product->balance_is_spend = true;
        product->journey_legs = newest[12] & 0x0F;
        itso_decode_money(
            (int32_t)itso_bits(newest, 104, 12), (newest[12] >> 4) & 0x0F,
            &product->cumulative_fare);
        product->has_journey = true;
        product->priority_override = (itso_bits(newest, 116, 4) & 0x02) != 0;
        break;

    case ItsoTypChargeToAccount2:
        /* TS 1000-5 table 17: a count of transactions in the charge period, and
         * the date that count was last cleared. */
        product->count_kind = ItsoCountTransactions;
        product->count = newest[10];
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
        product->count = itso_bits(newest, 80, 6);
        {
            uint8_t flags = (uint8_t)itso_bits(newest, 86, 6);
            product->auto_renew = (flags & 0x01) != 0;
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
        product->count = newest[10];
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
        product->count = newest[10];
        break;

    case ItsoTypVoucher:
    case ItsoTypTolling:
        /* TS 1000-5 tables 38 and 42, which are identical. */
        product->count_kind = ItsoCountRides;
        product->count = newest[10];
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
static void itso_parse_instance_id(ItsoProduct* product, const uint8_t* group, size_t len, size_t dataset_len) {
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

    if(bitmap & (1 << bit)) pos += 4; /* SecondaryHolderID. */
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
        pos += 2; /* HalfDayOfWeek. */
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

    itso_parse_id_optionals(
        product, data, len, optionals, bitmap, product->typ == ItsoTypId);
}

/** TYP 22: pre-defined area-based ticket, all three format revisions. */
static void itso_parse_period_ipe(
    ItsoProduct* product,
    const uint8_t* data,
    size_t len,
    uint8_t bitmap,
    uint8_t format_rev) {
    size_t pos;

    if(format_rev >= 3) {
        /* TS 1000-5 clause 2.9.3: ValidityStartDate is a DATE at byte 13.25, so
         * the mandatory part has to reach byte 15 before it can be read. */
        if(len < 15) return;
        product->start = (uint16_t)itso_bits(data, 106, 14);
        product->has_start = true;
        pos = 31; /* CPICC is mandatory here and ends at byte 31. */

        if(bitmap & (1 << 3)) pos += 4; /* PassDurationCode, PassDuration, ExpiryDateSPDuration. */
        if(!(bitmap & (1 << 1))) return;
        pos += 5; /* RouteCode. */
    } else {
        /* Revisions 1 and 2 store a DTS, not a DATE, at byte 13. */
        if(len < 16) return;
        product->start = 0;
        product->has_start = false;

        if(format_rev == 2) {
            pos = 28;
            if(bitmap & (1 << 4)) pos += 2; /* CPICC. */
            if(bitmap & (1 << 3)) pos += 1; /* PassDuration. */
            if(!(bitmap & (1 << 1))) return;
            pos += 5; /* RouteCode. */
        } else {
            pos = 26;
            if(bitmap & (1 << 4)) pos += 2; /* CPICC. */
            /* Revision 1 flags the two locations independently. */
            if(bitmap & (1 << 1)) {
                if(pos >= len) return;
                pos += itso_parse_location(
                    data + pos, len - pos, ItsoLocStructLoc1, &product->from);
            }
            if(bitmap & (1 << 2)) {
                if(pos >= len) return;
                itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
            }
            return;
        }
    }

    if(pos >= len) return;
    pos += itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->from);
    if(pos >= len) return;
    itso_parse_location(data + pos, len - pos, ItsoLocStructLoc1, &product->to);
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
            pos += itso_parse_location(
                data + pos, len - pos, ItsoLocStructLoc1, &product->from);
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
        itso_parse_value_records(product, group, len, dataset_len, sector_size);
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
            tap->entry_dts = itso_bits(group, 56, 24);
            tap->has_entry = true;
            break;
        case 10:
            tap->entry_oid = (uint16_t)((group[0] << 8) | group[1]);
            tap->has_entry_oid = true;
            break;
        default:
            break;
        }
        pos += group_len;
    }

    tap->present = true;
    return true;
}

void itso_parse_log(ItsoCard* card, const uint8_t* data, size_t len) {
    /* TS 1000-10 clause 8.7.5: the DESFire cyclic log holds fixed 48-byte records.
     * Record Offset in the Log Directory Entry names the *next* slot to be used,
     * so the newest record is the one before it. */
    const size_t record_len = 48;
    uint8_t slots = (uint8_t)(len / record_len);
    if(slots == 0) return;
    if(slots > ITSO_MAX_TAPS) slots = ITSO_MAX_TAPS;

    for(uint8_t i = 0; i < slots; i++) {
        ItsoTap* tap = &card->taps[card->tap_count];
        memset(tap, 0, sizeof(ItsoTap));
        if(!itso_parse_tap(tap, data + (size_t)i * record_len, record_len)) continue;

        /* Mark the slot the Log Directory Entry points back at. */
        uint8_t newest_slot = (uint8_t)((card->log_record_offset + slots - 1) % slots);
        tap->latest = card->log_entry_valid && card->log_normal_mode && (i == newest_slot);
        card->tap_count++;
    }

    /* Newest first, so the most recent journey is the first thing on screen.
     * At most ITSO_MAX_TAPS records, so an insertion sort is plenty. */
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
