/**
 * @file itso_log.c
 * @brief The cyclic log: Transient Ticket Records, one per tap (TS 1000-5 clause 3).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_i.h"

#include <string.h>
#include <stdlib.h>

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
            /* BCD, as every IIN is: ITSO's own reads 633597, not 0x633597. */
            tap->iin = itso_bcd_number(group, 0, 6);
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
        ItsoUnixTime held_time = itso_dts_to_unix(held.dts);
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
     * Which slot is newest does not matter here: the taps are put in time order
     * once they are all in. */
    uint8_t slots = (uint8_t)(len / ITSO_TAP_RECORD_LEN);

    for(uint8_t i = 0; i < slots; i++) {
        ItsoTap tap;
        memset(&tap, 0, sizeof(tap));
        if(!itso_parse_tap(&tap, data + (size_t)i * ITSO_TAP_RECORD_LEN, ITSO_TAP_RECORD_LEN)) {
            continue;
        }

        tap.on_card = true;
        itso_add_tap(card, &tap);
    }

    itso_sort_taps(card);
}

void itso_parse_log_history(ItsoCard* card, const uint8_t* data, size_t len) {
    /* on_card stays false, because only the file has these - a record still on
     * the card was added from the live log first, and the duplicate is dropped. */
    for(size_t offset = 0; offset + ITSO_TAP_RECORD_LEN <= len; offset += ITSO_TAP_RECORD_LEN) {
        ItsoTap tap;
        memset(&tap, 0, sizeof(tap));
        if(!itso_parse_tap(&tap, data + offset, ITSO_TAP_RECORD_LEN)) continue;
        itso_add_tap(card, &tap);
    }

    itso_sort_taps(card);
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
