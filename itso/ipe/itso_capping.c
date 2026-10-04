/**
 * @file itso_capping.c
 * @brief The Complex Capping Value Group Extension, VGXRef 1 and 2 (TS 1000-5 clause 4.1).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_ipe_i.h"

#include <string.h>

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

    const uint8_t* v = group + itso_vgx_offset(group, len, offset);
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
        acc->rule = (ItsoCapRule)(v[base] >> 4);
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
