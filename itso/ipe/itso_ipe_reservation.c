/**
 * @file itso_ipe_reservation.c
 * @brief TYP 24: the specific journey with reservations (TS 1000-5 clause 2.11), and the VGXRef 3 extension that holds them (clause 4.1.3).
 *
 * Offsets cite ITSO TS 1000 clause numbers so they can be checked against the
 * published specification. Every accessor is bounds checked: card data is
 * attacker-controlled as far as this app is concerned, and a malformed card
 * must produce an empty result rather than a crash.
 */
#include "itso_ipe_i.h"

#include <string.h>
#include <stdlib.h>

/* Bytes before Origin: everything up to the first LOC1 is at a fixed offset. */
#define ITSO_T24_FIXED_LEN 30
/* Route through AmountPaid: the fixed-size run between the four leading
 * locations and VendorLoc. */
#define ITSO_T24_TERMS_LEN 20

/**
 * Where a TYP 24 dataset's elements must end: its declared length, cut to what
 * was read, less the IIN bitmap bit 0 appends after the padding (TS 1000-2
 * clause 6.2.6). Table 136 caps the dataset at 256 bytes, which a six-bit
 * IPELength cannot exceed.
 */
static size_t itso_t24_end(const uint8_t* data, size_t len) {
    size_t end = (size_t)itso_bits(data, 0, 6) * ITSO_IPE_BLOCK_LEN;
    if(itso_bits(data, 6, 6) & 0x01) end = end >= 3 ? end - 3 : 0;
    return end < len ? end : len;
}

/**
 * The LOC1 at @p pos, into @p out or nowhere: the offset after it, or 0 when it
 * does not fit or @p pos is already 0.
 *
 * Table 136's offsets assume six-byte LOC1s (its note 1, for UK rail), but a
 * LOC1 is as long as its own length byte says, so everything after the first
 * one is found by walking - a bus operator's AtcoCode is longer.
 */
static size_t itso_t24_location(const uint8_t* data, size_t end, size_t pos, ItsoLocation* out) {
    ItsoLocation skipped;
    if(pos == 0 || pos >= end) return 0;
    size_t used =
        itso_parse_location(data + pos, end - pos, ItsoLocStructLoc1, out ? out : &skipped);
    return used ? pos + used : 0;
}

/**
 * The fixed part of a TYP 24 dataset, Origin to VendorLoc (table 136).
 *
 * One walk for both readers: itso_parse_ipe() passes the product, for what its
 * summary and list row need, and itso_parse_reservation() an ItsoReservation,
 * for the rest. Either may be NULL; nothing is read twice into the same place.
 *
 * @param end where the dataset's elements end, from itso_t24_end().
 * @return the offset after VendorLoc, or 0 when the dataset ends first.
 */
static size_t
    itso_t24_fixed(const uint8_t* data, size_t end, ItsoProduct* product, ItsoReservation* res) {
    if(end < ITSO_T24_FIXED_LEN) return 0;

    if(product) {
        ItsoTicketTerms* t = &product->terms.ticket;
        t->flags = (uint16_t)itso_bits(data, 40, 12);
        product->auto_renew = (t->flags & ITSO_T24_AUTO_RENEW) != 0;
        t->sold_as = (uint8_t)itso_bits(data, 52, 4);
        t->travel_class = (uint8_t)itso_bits(data, 108, 3);
        t->journeys_sold = (uint16_t)itso_bits(data, 117, 9);
        t->outward_days = (uint16_t)itso_bits(data, 126, 9);
        t->return_days = (uint16_t)itso_bits(data, 135, 9);
        t->adults = data[23];
        t->children = data[24];
        t->concessions = data[25];
        t->valid = true;
    }
    if(res) {
        memcpy(res->ticket_number, data + 7, sizeof(res->ticket_number));
        res->renew_days = (uint8_t)itso_bits(data, 111, 6);
        memcpy(res->operator_code, data + 18, sizeof(res->operator_code));
        memcpy(res->ftot, data + 20, sizeof(res->ftot));
        memcpy(res->id_doc, data + 26, sizeof(res->id_doc));
    }

    size_t pos = itso_t24_location(data, end, ITSO_T24_FIXED_LEN, product ? &product->from : NULL);
    pos = itso_t24_location(data, end, pos, product ? &product->to : NULL);
    pos = itso_t24_location(data, end, pos, res ? &res->alt_from : NULL);
    pos = itso_t24_location(data, end, pos, res ? &res->alt_to : NULL);
    if(pos == 0 || pos + ITSO_T24_TERMS_LEN > end) return 0;

    /* From Route on, at offsets from one another: Route 0, OutPortionValidFrom
     * 5, RtnPortionValidFrom 8, RestrictionCode 11, the two day masks 13 and
     * 14, the currency and payment method 15, AmountPaid 16. */
    const uint8_t* terms = data + pos;
    /* AmountPaidCurrencyCode is the high nibble and the payment method the low:
     * the reverse of the order TYP 22 and TYP 23 keep them in. */
    const uint8_t valc = terms[15] >> 4;
    if(product) {
        ItsoTicketTerms* t = &product->terms.ticket;
        memcpy(t->route_code, terms, sizeof(t->route_code));
        t->has_route_code = true;
        t->valid_from_dts = itso_bits(terms, 40, 24);
        t->return_from_dts = itso_bits(terms, 64, 24);
        t->paid_mop = terms[15] & 0x0F;
        uint32_t paid = itso_bits(terms, 128, 32);
        if(paid) itso_decode_money((int32_t)paid, valc, &t->amount_paid);
    }
    if(res) {
        memcpy(res->restriction_code, terms + 11, sizeof(res->restriction_code));
        res->valid_days = terms[13];
        res->restricted_days = terms[14];
        res->valc = valc;
    }

    return itso_t24_location(data, end, pos + ITSO_T24_TERMS_LEN, res ? &res->vendor : NULL);
}

/**
 * TYP 24 revision 2, the only one table 136 defines: the parts of the fixed
 * dataset a product keeps.
 */
void itso_ipe_reservation_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    if(product->format_rev != 2) return;
    const size_t end = itso_t24_end(data, len);
    size_t pos = itso_t24_fixed(data, end, product, NULL);

    /* The first discount: the railcard, on rail, which the summary names. It
     * is the second optional group, after the one-byte associated IPEs. */
    if(pos == 0 || !(product->bitmap & (1 << 2)) || itso_bits(data, 90, 2) == 0) return;
    pos += itso_bits(data, 88, 2);
    if(pos + sizeof(product->terms.ticket.discount) > end) return;
    memcpy(product->terms.ticket.discount, data + pos, sizeof(product->terms.ticket.discount));
    product->terms.ticket.has_discount = true;
}

/** Room for @p need bytes at @p pos; when there is not, the groups from here are lost. */
static bool itso_t24_fits(ItsoReservation* res, size_t pos, size_t need, size_t end) {
    if(pos != 0 && pos + need <= end) return true;
    res->overrun = true;
    return false;
}

/**
 * The eight optional groups of IPEBitMap bit 2, in table 136's order.
 *
 * The counts come Associated, Discounts, Supplements, Transfers, Interchanges,
 * but the table lists the Interchange group before Transfers. The table is the
 * layout - it is what gives each group its byte count - so that is the order
 * they are read in.
 *
 * @return the offset after the last group, or 0 when a count ran past the end.
 */
static size_t itso_t24_options(const uint8_t* data, size_t end, size_t pos, ItsoReservation* res) {
    const uint8_t associated = (uint8_t)itso_bits(data, 88, 2);
    const uint8_t discounts = (uint8_t)itso_bits(data, 90, 2);
    const uint8_t supplements = (uint8_t)itso_bits(data, 92, 2);
    const uint8_t transfers = (uint8_t)itso_bits(data, 94, 2);
    const uint8_t interchanges = (uint8_t)itso_bits(data, 96, 3);
    const uint8_t bands = (uint8_t)itso_bits(data, 99, 3);
    const uint8_t services = (uint8_t)itso_bits(data, 102, 3);
    const uint8_t routes = (uint8_t)itso_bits(data, 105, 3);

    for(uint8_t i = 0; i < associated; i++) {
        if(!itso_t24_fits(res, pos, 1, end)) return 0;
        res->associated[res->associated_count++] = data[pos++];
    }

    for(uint8_t i = 0; i < discounts; i++) {
        if(!itso_t24_fits(res, pos, 11, end)) return 0;
        const uint8_t* g = data + pos;
        ItsoDiscount* d = &res->discounts[res->discount_count++];
        memcpy(d->code, g, sizeof(d->code));
        itso_decode_money((int32_t)itso_bits(g, 40, 32), res->valc, &d->amount);
        d->percentage = (uint16_t)itso_bits(g, 72, 10);
        d->type = (uint8_t)itso_bits(g, 82, 5);
        pos += 11;
    }

    for(uint8_t i = 0; i < supplements; i++) {
        if(!itso_t24_fits(res, pos, 3, end)) return 0;
        itso_copy_name(data + pos, 3, res->supplements[res->supplement_count++], 4);
        pos += 3;
    }

    /* The groups with locations in them are allocated to fit. One that cannot
     * be allocated is still walked, so the groups after it are found. */
    if(interchanges) res->interchanges = calloc(interchanges, sizeof(ItsoInterchange));
    for(uint8_t i = 0; i < interchanges; i++) {
        ItsoInterchange x = {0};
        pos = itso_t24_location(data, end, pos, &x.exit);
        pos = itso_t24_location(data, end, pos, &x.entry);
        if(!itso_t24_fits(res, pos, 1, end)) return 0;
        x.minutes = (uint8_t)itso_bits(data + pos, 0, 6);
        pos += 1;
        if(res->interchanges) res->interchanges[res->interchange_count++] = x;
    }

    for(uint8_t i = 0; i < transfers; i++) {
        if(!itso_t24_fits(res, pos, 3, end)) return 0;
        ItsoTransfer* x = &res->transfers[res->transfer_count++];
        x->type = data[pos];
        x->count = (uint16_t)itso_bits(data + pos, 8, 9);
        x->hours = (uint8_t)itso_bits(data + pos, 18, 6);
        pos += 3;
    }

    if(bands) res->time_bands = calloc(bands, sizeof(ItsoTimeBand));
    for(uint8_t i = 0; i < bands; i++) {
        ItsoTimeBand x = {0};
        if(!itso_t24_fits(res, pos, 2, end)) return 0;
        memcpy(x.operator_code, data + pos, sizeof(x.operator_code));
        pos = itso_t24_location(data, end, pos + 2, &x.location);
        if(!itso_t24_fits(res, pos, 4, end)) return 0;
        const uint8_t* g = data + pos;
        x.portion = (uint8_t)itso_bits(g, 0, 2);
        x.start = (uint16_t)itso_bits(g, 2, 11);
        x.end = (uint16_t)itso_bits(g, 13, 11);
        x.arrival = itso_bits(g, 24, 1) != 0;
        x.include = itso_bits(g, 25, 1) != 0;
        pos += 4;
        if(res->time_bands) res->time_bands[res->time_band_count++] = x;
    }

    /* Restriction2's offsets come to 13.5 bytes with a six-byte LOC1 and then
     * put an RFU at 15.5, but its count says 14 bytes: the eight after the
     * location are read, and the last four bits taken as RFU. */
    if(services) res->services = calloc(services, sizeof(ItsoServiceRule));
    for(uint8_t i = 0; i < services; i++) {
        ItsoServiceRule x = {0};
        pos = itso_t24_location(data, end, pos, &x.departs);
        if(!itso_t24_fits(res, pos, 8, end)) return 0;
        memcpy(x.service, data + pos, sizeof(x.service));
        x.time = (uint16_t)itso_bits(data + pos, 48, 11);
        x.restriction = itso_bits(data + pos, 59, 1) != 0;
        pos += 8;
        if(res->services) res->services[res->service_count++] = x;
    }

    if(routes) res->routes = calloc(routes, sizeof(ItsoRoutePoint));
    for(uint8_t i = 0; i < routes; i++) {
        ItsoRoutePoint x = {0};
        pos = itso_t24_location(data, end, pos, &x.location);
        if(!itso_t24_fits(res, pos, 1, end)) return 0;
        x.via = (uint8_t)itso_bits(data + pos, 0, 2);
        pos += 1;
        if(res->routes) res->routes[res->route_count++] = x;
    }
    return pos;
}

bool itso_parse_reservation_dataset(const uint8_t* data, size_t len, ItsoReservation* out) {
    if(len < 2 || itso_bits(data, 12, 4) != 2) return false;
    const uint8_t bitmap = (uint8_t)itso_bits(data, 6, 6);
    const size_t end = itso_t24_end(data, len);

    size_t pos = itso_t24_fixed(data, end, NULL, out);
    if(pos == 0) return false;
    out->valid = true;

    if(bitmap & (1 << 2)) pos = itso_t24_options(data, end, pos, out);
    /* PaxDetail comes after every optional group (table 136), so a group that
     * overran leaves it nowhere to be found. */
    if((bitmap & (1 << 1)) && itso_t24_fits(out, pos, 21, end)) {
        itso_copy_name(data + pos, 20, out->passenger, sizeof(out->passenger));
        out->gender = (uint8_t)itso_bits(data + pos, 160, 2);
        out->has_passenger = true;
    }
    return true;
}

bool itso_parse_reservation_vgx(
    const uint8_t* vgx,
    size_t len,
    uint8_t count,
    ItsoReservation* out) {
    if(len < 2 || itso_bits(vgx, 6, 2) != 0 || vgx[1] != 3) return false;
    /* Table AD3 counts 36 bytes for the part before the reservations, but its
     * elements come to 19 with a six-byte LOC1. VGXLength is the one figure
     * the card itself states, so it is the bound. */
    size_t end = (size_t)itso_bits(vgx, 0, 6) * ITSO_IPE_BLOCK_LEN;
    if(end > len) end = len;
    if(end < 5) return false;

    out->last_validation = itso_bits(vgx, 16, 24);
    size_t pos = itso_t24_location(vgx, end, 5, &out->last_validation_at);
    if(pos == 0 || pos + 8 > end) return false;
    itso_copy_name(vgx + pos, 8, out->booking, sizeof(out->booking));
    pos += 8;
    out->has_extension = true;

    if(count > ITSO_T24_LEGS_MAX) count = ITSO_T24_LEGS_MAX;
    if(count) out->legs = calloc(count, sizeof(ItsoReservedLeg));
    for(uint8_t i = 0; i < count; i++) {
        /* 32 bytes each with six-byte LOC1s: a DTS, the service, two
         * locations, then coach, seat, attribute and two bytes of flags. */
        if(pos + 9 > end) break;
        ItsoReservedLeg leg = {0};
        leg.departs = itso_bits(vgx + pos, 0, 24);
        itso_copy_trimmed(vgx + pos + 3, 6, leg.service, sizeof(leg.service));
        pos = itso_t24_location(vgx, end, pos + 9, &leg.from);
        pos = itso_t24_location(vgx, end, pos, &leg.to);
        if(pos == 0 || pos + 11 > end) break;
        const uint8_t* g = vgx + pos;
        itso_copy_trimmed(g, 2, leg.coach, sizeof(leg.coach));
        itso_copy_trimmed(g + 2, 3, leg.seat, sizeof(leg.seat));
        itso_copy_trimmed(g + 5, 4, leg.attribute, sizeof(leg.attribute));
        leg.direction = (uint8_t)itso_bits(g, 72, 2);
        leg.berth = (uint8_t)itso_bits(g, 74, 2);
        leg.type = (uint8_t)itso_bits(g, 76, 4);
        leg.together = itso_bits(g, 80, 1) != 0;
        pos += 11;
        if(out->legs) out->legs[out->leg_count++] = leg;
    }
    return true;
}

bool itso_parse_reservation(
    const uint8_t* group,
    size_t len,
    uint8_t sector_size,
    uint8_t reservations,
    ItsoReservation* out) {
    memset(out, 0, sizeof(*out));
    if(len < 2) return false;
    size_t dataset_len = (size_t)itso_bits(group, 0, 6) * ITSO_IPE_BLOCK_LEN;
    if(dataset_len < 4 || dataset_len > len) return false;

    bool ok = itso_parse_reservation_dataset(group, dataset_len, out);

    /* The reservations are in the extension after the value record (clause
     * 4.1.3); a group with none, or with another kind, simply has no legs. */
    size_t offset;
    if(itso_value_records(group, len, sector_size, &offset) &&
       itso_vgx_ref(group, len, offset) == 3) {
        size_t vgx = itso_vgx_offset(group, len, offset);
        itso_parse_reservation_vgx(group + vgx, len - vgx, reservations, out);
    }
    return ok;
}

void itso_reservation_free(ItsoReservation* res) {
    free(res->interchanges);
    free(res->time_bands);
    free(res->services);
    free(res->routes);
    free(res->legs);
    memset(res, 0, sizeof(*res));
}

/** TYP 24: journeys and transfers left, and how many legs are reserved. */
void itso_ipe_reservation_value(ItsoProduct* product, const uint8_t* newest) {
    const ItsoValueRecord* live = &product->value_history[0];
    /* TS 1000-5 table 139. Table offset N is record byte N-2. Its comment
     * allows "up to 3 transfer types each with up to 511 transfers", which
     * would take 27 bits, but the element is 11: one count, the total. */
    product->count_kind = ItsoCountJourneys;
    product->count = live->count;
    product->terms.ticket.transfers_left = (uint16_t)itso_bits(newest, 88, 11);
    product->terms.ticket.part_used = itso_bits(newest, 99, 1) != 0;
    /* Only IPEBitMap bit 3 says the count means anything (table 137). */
    if(product->bitmap & (1 << 3)) {
        product->terms.ticket.reservations = (uint8_t)itso_bits(newest, 100, 4);
    }
}
