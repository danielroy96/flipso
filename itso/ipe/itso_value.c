/**
 * @file itso_value.c
 * @brief The Value Record Data Group an IPE may carry (TS 1000-2 clause 7).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_ipe_i.h"

#include <string.h>

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
 * Where the Value Group Extension a value group carries starts within @p group,
 * or 0 when it has none or its header is cut off.
 *
 * @p records_offset is where the value records start, as itso_value_records()
 * reported it. The extension sits after every record the group supports - not
 * only the ones written - which is what the VGBitMap count is (TS 1000-2
 * clause 7.5).
 */
size_t itso_vgx_offset(const uint8_t* group, size_t len, size_t records_offset) {
    uint8_t vg_bitmap = itso_bits(group + records_offset - 2, 6, 6);
    if(!(vg_bitmap & 0x01)) return 0;

    uint8_t supported = 0;
    for(uint8_t bit = 1; bit < 6; bit++) {
        if(vg_bitmap & (1 << bit)) supported++;
    }
    size_t vgx = records_offset + (size_t)supported * ITSO_VALUE_RECORD_LEN;
    return vgx + 2 > len ? 0 : vgx;
}

/** The VGXRef of the Value Group Extension a value group carries, or 0. */
uint8_t itso_vgx_ref(const uint8_t* group, size_t len, size_t records_offset) {
    size_t vgx = itso_vgx_offset(group, len, records_offset);
    if(vgx == 0) return 0;
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
    case ItsoTypStoredTravelRights: /* TS 1000-5 table 4: a balance, a VALS. */
        itso_decode_money(itso_int16(record + 10), (record[12] >> 4) & 0x0F, &out->amount);
        break;
    case ItsoTypChargeToAccount1: /* Table 12: the same bytes, counting up - a VALI. */
        itso_decode_money(itso_uint16(record + 10), (record[12] >> 4) & 0x0F, &out->amount);
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
 * are defined per IPE type in TS 1000-5, which is what the type table is for.
 *
 * Every record is decoded, not only the live one. The others are the
 * transactions before it, which is the only statement a card keeps.
 */
void itso_parse_value_records(
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

    /* What the tail of the newest record says about the product as it stands,
     * which TS 1000-5 defines per IPE type and so each type's own file decodes.
     * The balance and the counter come from the record itself, which
     * itso_decode_value_record() has already read: they are the same field
     * whether they are being shown as the current value or as a line of the
     * history, and deciding what those bytes mean in two places is how the two
     * views come to disagree. What is left to the type is the part of the tail
     * that describes the product rather than the transaction. A type with no
     * decoder for its tail still read a clean common header, so that is not a
     * failure to report. */
    const ItsoIpeType* type = itso_ipe_type(product->typ);
    if(type && type->value) type->value(product, newest);
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

bool itso_value_record_newer(const uint8_t* a, const uint8_t* b) {
    return itso_ts_newer((uint16_t)itso_bits(a, 4, 12), (uint16_t)itso_bits(b, 4, 12));
}
