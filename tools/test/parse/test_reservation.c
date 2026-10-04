/**
 * @file test_reservation.c
 * @brief TYP 24 reserved journeys: the dataset, its optional groups and the legs.
 */
#include "test_parse.h"

/** itso_parse_reservation() over an exact-length heap copy of @p len bytes. */
static bool parse_reservation_exact(
    const uint8_t* src,
    size_t len,
    uint8_t reservations,
    ItsoReservation* res) {
    uint8_t* exact = malloc(len ? len : 1);
    memcpy(exact, src, len);
    bool ok = itso_parse_reservation(exact, len, 64, reservations, res);
    free(exact);
    return ok;
}

/** itso_parse_reservation_dataset() over an exact-length heap copy. */
static bool parse_t24_dataset_exact(const uint8_t* src, size_t len, ItsoReservation* res) {
    memset(res, 0, sizeof(*res));
    uint8_t* exact = malloc(len ? len : 1);
    memcpy(exact, src, len);
    bool ok = itso_parse_reservation_dataset(exact, len, res);
    free(exact);
    return ok;
}

/* Where the synthetic reserved journeys keep their value group and its
 * extension: three 64-byte sectors of IPE, then a two-byte header and one
 * fifteen-byte record (build_card.py). */
#define RES_DATASET_LEN 168
#define RES_VG_OFFSET   192
#define RES_VGX_OFFSET  (RES_VG_OFFSET + 2 + ITSO_VALUE_RECORD_LEN)

/*
 * TYP 24, the reserved journey (TS 1000-5 clause 2.11 and table AD3): its
 * dataset, which ItsoProduct holds the summary of, and everything else, which
 * itso_parse_reservation() decodes on demand.
 */
void reservation_ticket(void) {
    printf("\nTYP 24 reserved journey\n");
    static ItsoProduct p;
    ItsoReservation res;

    parse_group(&p, ItsoTypReservationTicket, true, reservation_group, sizeof(reservation_group));
    const ItsoTicketTerms* t = itso_product_ticket(&p);
    check("TYP 24 dataset decoded", p.body_parsed && t->valid && p.format_rev == 2);
    check(
        "TYP24Flags, twelve bits from byte 5",
        t->flags == (ITSO_T24_DUPLICATE | ITSO_T24_TEST | ITSO_T24_PASSENGER |
                     ITSO_T24_SEAT_REQUIRED | ITSO_T24_AUTO_RENEW) &&
            p.auto_renew);
    check(
        "a return of two journeys, first class",
        t->sold_as == ItsoSoldReturns && t->journeys_sold == 2 && t->travel_class == 1);
    check("the two portions' periods", t->outward_days == 1 && t->return_days == 30);
    check("travellers", t->adults == 1 && t->children == 1 && t->concessions == 0);
    check(
        "origin and destination walked to",
        strcmp(loc_text(&p.from), "Station 1072") == 0 &&
            strcmp(loc_text(&p.to), "Station 1444") == 0);
    check("Route after four LOC1s", t->has_route_code && memcmp(t->route_code, "00700", 5) == 0);
    check(
        "the outward portion starts",
        strcmp(fmt_unix(itso_dts_to_unix(t->valid_from_dts)), "2026-10-01 00:00") == 0);
    check(
        "the return portion starts",
        strcmp(fmt_unix(itso_dts_to_unix(t->return_from_dts)), "2026-10-03 00:00") == 0);
    check(
        "AmountPaid, currency from the high nibble and method from the low",
        t->amount_paid.valid && t->amount_paid.value == 8950 && t->amount_paid.currency == 0 &&
            t->paid_mop == 3);
    check("the IIN after the padding", p.has_iin && p.iin == 633597);
    check("ProductRetailer, as rail's retailing NLC", p.has_retailer && p.retailer == 0x96AD);
    check(
        "the first discount kept for the summary",
        t->has_discount && memcmp(t->discount, "YNG  ", 5) == 0);
    check(
        "JourneysRemaining is journeys",
        p.value_parsed && p.count_kind == ItsoCountJourneys && p.count == 1 && p.value_txn == 2);
    check(
        "TransfersRemaining, JourneyPartUsedFlag and NumberOfReservations",
        t->transfers_left == 511 && t->part_used && t->reservations == 2);
    check("the extension is VGXRef 3", p.vgx_ref == 3);

    bool ok = parse_reservation_exact(
        reservation_group, sizeof(reservation_group), t->reservations, &res);
    check("the rest of the dataset decodes on demand", ok && res.valid);
    check(
        "TicketNumber, OperatorSpecificity, FTOT",
        memcmp(res.ticket_number, "\x00\x01\xE2\x40", 4) == 0 &&
            memcmp(res.operator_code, "GR", 2) == 0 && memcmp(res.ftot, "SOR", 3) == 0);
    check(
        "IdDocumentReference and AutoRenewTimeAfterExpiry",
        memcmp(res.id_doc, "\x00\x00\x38\xE7", 4) == 0 && res.renew_days == 14);
    check(
        "RestrictionCode and the two day masks",
        memcmp(res.restriction_code, "OP", 2) == 0 && res.valid_days == 0xF8 &&
            res.restricted_days == 0x06);
    check(
        "alternative ends, one of them null",
        strcmp(loc_text(&res.alt_from), "Station 0035") == 0 && !res.alt_to.valid);
    check("VendorLoc", strcmp(loc_text(&res.vendor), "Station 5685") == 0);
    check("bitmap bit 2: the optional groups, none overrunning", !res.overrun);
    check("an associated IPE", res.associated_count == 1 && res.associated[0] == 2);
    check(
        "a discount by percentage",
        res.discount_count == 1 && memcmp(res.discounts[0].code, "YNG  ", 5) == 0 &&
            res.discounts[0].percentage == 33 && res.discounts[0].type == ITSO_DISCOUNT_STATUS &&
            res.discounts[0].amount.value == 0);
    check(
        "rail's whole percent is 330 tenths",
        itso_discount_is_rail(&res.discounts[0]) &&
            itso_discount_tenths(&res.discounts[0]) == 330);
    check("a supplement", res.supplement_count == 1 && strcmp(res.supplements[0], "SLP") == 0);
    check(
        "an interchange, read before the transfers",
        res.interchange_count == 1 &&
            strcmp(loc_text(&res.interchanges[0].exit), "Station 5148") == 0 &&
            strcmp(loc_text(&res.interchanges[0].entry), "Station 5143") == 0 &&
            res.interchanges[0].minutes == 45);
    check(
        "break of journey, rail's transfer type 2, unlimited",
        res.transfer_count == 1 && res.transfers[0].type == ITSO_TRANSFER_BREAK_OF_JOURNEY &&
            res.transfers[0].count == ITSO_TRANSFERS_UNLIMITED && res.transfers[0].hours == 0);
    check(
        "a time band",
        res.time_band_count == 1 && res.time_bands[0].portion == ITSO_T24_BAND_OUTWARD &&
            res.time_bands[0].start == 420 && res.time_bands[0].end == 570 &&
            !res.time_bands[0].arrival && !res.time_bands[0].include &&
            !res.time_bands[0].location.valid);
    check(
        "a specific train, fourteen bytes with a six-byte LOC1",
        res.service_count == 1 &&
            strcmp(loc_text(&res.services[0].departs), "Station 1444") == 0 &&
            memcmp(res.services[0].service, "GR1234", 6) == 0 && res.services[0].time == 1110 &&
            res.services[0].restriction);
    check(
        "a routing point",
        res.route_count == 1 && strcmp(loc_text(&res.routes[0].location), "Station 1555") == 0 &&
            res.routes[0].via == 1);
    check(
        "bitmap bit 1: the passenger, after every group",
        res.has_passenger && strcmp(res.passenger, "A N OTHER") == 0 && res.gender == 2);
    check(
        "the extension's fixed part",
        res.has_extension && strcmp(res.booking, "ABC12345") == 0 &&
            strcmp(fmt_unix(itso_dts_to_unix(res.last_validation)), "2026-10-01 08:02") == 0 &&
            strcmp(loc_text(&res.last_validation_at), "Station 1072") == 0);
    check("two reserved legs", res.leg_count == 2);
    if(res.leg_count == 2) {
        const ItsoReservedLeg* a = &res.legs[0];
        const ItsoReservedLeg* b = &res.legs[1];
        check(
            "the outward leg, coach and seat without rail's left padding",
            strcmp(fmt_unix(itso_dts_to_unix(a->departs)), "2026-10-01 08:30") == 0 &&
                strcmp(a->service, "GR1234") == 0 &&
                strcmp(loc_text(&a->from), "Station 1072") == 0 &&
                strcmp(loc_text(&a->to), "Station 1444") == 0 && strcmp(a->coach, "C") == 0 &&
                strcmp(a->seat, "42") == 0 && strcmp(a->attribute, "WNDW") == 0 &&
                a->direction == ITSO_SEAT_FACING && a->berth == 0 && a->type == 0 && !a->together);
        check(
            "the return leg, a shared upper berth",
            strcmp(b->seat, "17A") == 0 && b->direction == ITSO_SEAT_AIRLINE && b->berth == 2 &&
                b->type == 1 && b->together);
    }
    itso_reservation_free(&res);
    check("freeing leaves it empty", res.legs == NULL && res.leg_count == 0 && !res.valid);

    /* NumberOfReservations says how many legs there are; fewer asked for, fewer
     * read, and more than the extension holds stops at its end. */
    parse_reservation_exact(reservation_group, sizeof(reservation_group), 1, &res);
    check("one leg asked for, one read", res.leg_count == 1);
    itso_reservation_free(&res);
    parse_reservation_exact(reservation_group, sizeof(reservation_group), 15, &res);
    check("fifteen asked for, two there", res.leg_count == 2);
    itso_reservation_free(&res);

    /* AtcoCode LOC1s are eleven bytes here, not six: every offset table 136
     * gives after Origin is wrong, and only a walk finds the elements. */
    parse_group(
        &p, ItsoTypReservationTicket, true, reservation_atco_group, sizeof(reservation_atco_group));
    check(
        "a bus ticket's ends by AtcoCode",
        strcmp(loc_text(&p.from), "Stop 450016879") == 0 &&
            strcmp(loc_text(&p.to), "Stop 450030236") == 0);
    check(
        "and the terms after them, walked to",
        itso_product_ticket(&p)->amount_paid.value == 420 &&
            itso_product_ticket(&p)->paid_mop == 1 &&
            strcmp(
                fmt_unix(itso_dts_to_unix(itso_product_ticket(&p)->valid_from_dts)),
                "2026-10-01 00:00") == 0);
    check("its one reservation counted", itso_product_ticket(&p)->reservations == 1);
    {
        /* Without IPEBitMap bit 3 NumberOfReservations means nothing (table 137). */
        uint8_t* plain = malloc(sizeof(reservation_atco_group));
        memcpy(plain, reservation_atco_group, sizeof(reservation_atco_group));
        plain[1] &= (uint8_t) ~(0x08 << 4);
        ItsoProduct q;
        memset(&q, 0, sizeof(q));
        q.typ = ItsoTypReservationTicket;
        q.value_group = true;
        itso_parse_ipe(&q, plain, sizeof(reservation_atco_group), 64);
        free(plain);
        check(
            "without bitmap bit 3 the count is not kept",
            itso_product_ticket(&q)->reservations == 0);
        itso_product_free(&q);
    }
    ok = parse_reservation_exact(
        reservation_atco_group,
        sizeof(reservation_atco_group),
        itso_product_ticket(&p)->reservations,
        &res);
    check("its VendorLoc", ok && strcmp(loc_text(&res.vendor), "Stop 450030236") == 0);
    check(
        "an interchange of two AtcoCodes",
        res.interchange_count == 1 &&
            strcmp(loc_text(&res.interchanges[0].exit), "Stop 450030236") == 0 &&
            res.interchanges[0].minutes == 0);
    check(
        "and a not-via routing point after it",
        res.route_count == 1 && strcmp(loc_text(&res.routes[0].location), "Stop 450016879") == 0 &&
            res.routes[0].via == 0);
    check("no passenger without bitmap bit 1", !res.has_passenger);
    check(
        "a leg between AtcoCodes",
        res.leg_count == 1 && strcmp(loc_text(&res.legs[0].from), "Stop 450016879") == 0 &&
            strcmp(loc_text(&res.legs[0].to), "Stop 450030236") == 0 &&
            strcmp(res.legs[0].seat, "12") == 0 && res.legs[0].coach[0] == '\0' &&
            res.legs[0].type == 3);
    check("a null place of last validation", !res.last_validation_at.valid);
    itso_reservation_free(&res);

    /* Revision 1 is not defined (clause 2.11 has only 2): nothing is read. */
    {
        uint8_t rev1[RES_DATASET_LEN];
        memcpy(rev1, reservation_group, sizeof(rev1));
        rev1[1] = (rev1[1] & 0xF0) | 1;
        parse_group(&p, ItsoTypReservationTicket, false, rev1, sizeof(rev1));
        check(
            "a revision 1 dataset is not guessed at",
            !itso_product_ticket(&p)->valid && !p.from.valid);
        check("nor on demand", !parse_t24_dataset_exact(rev1, sizeof(rev1), &res));
        itso_reservation_free(&res);
    }

    /* Every truncation, each an exact allocation: the product's parse, the
     * whole on-demand parse, then the dataset and the extension on their own. */
    const struct {
        const uint8_t* group;
        size_t len;
    } groups[] = {
        {reservation_group, sizeof(reservation_group)},
        {reservation_atco_group, sizeof(reservation_atco_group)},
    };
    for(size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        const uint8_t* src = groups[g].group;
        for(size_t cut = 1; cut <= groups[g].len; cut++) {
            parse_group(&p, ItsoTypReservationTicket, true, src, cut);
            parse_reservation_exact(src, cut, 15, &res);
            itso_reservation_free(&res);
        }
        size_t dataset = (size_t)(src[0] >> 2) * 4;
        for(size_t cut = 0; cut <= dataset; cut++) {
            parse_t24_dataset_exact(src, cut, &res);
            itso_reservation_free(&res);
        }
        size_t vgx_len = (size_t)(src[RES_VGX_OFFSET] >> 2) * 4;
        for(size_t cut = 0; cut <= vgx_len; cut++) {
            memset(&res, 0, sizeof(res));
            uint8_t* exact = malloc(cut ? cut : 1);
            memcpy(exact, src + RES_VGX_OFFSET, cut);
            itso_parse_reservation_vgx(exact, cut, 15, &res);
            free(exact);
            itso_reservation_free(&res);
        }
    }
    check("the dataset and the extension survive every truncation", 1);

    /* Cut inside the passenger: the groups before it are whole, and the
     * passenger is not there to be shown. */
    parse_t24_dataset_exact(reservation_group, RES_DATASET_LEN - 4, &res);
    check(
        "a dataset cut inside PaxDetail keeps the groups and says it ran out",
        res.route_count == 1 && !res.has_passenger && res.overrun);
    itso_reservation_free(&res);

    /* Every count at every value, then all eight at their most (3, 3, 3, 3, 7,
     * 7, 7, 7): 376 bytes of groups, which no 256-byte dataset can hold. */
    static const struct {
        uint8_t bit, width;
    } counts[] = {{88, 2}, {90, 2}, {92, 2}, {94, 2}, {96, 3}, {99, 3}, {102, 3}, {105, 3}};
    uint8_t swept[RES_DATASET_LEN];
    bool bounded = true;
    for(size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); c++) {
        for(uint8_t v = 0; v < (1u << counts[c].width); v++) {
            memcpy(swept, reservation_group, sizeof(swept));
            for(uint8_t b = 0; b < counts[c].width; b++) {
                uint32_t bit = counts[c].bit + b;
                uint8_t mask = (uint8_t)(0x80 >> (bit % 8));
                if(v & (1u << (counts[c].width - 1 - b))) {
                    swept[bit / 8] |= mask;
                } else {
                    swept[bit / 8] &= (uint8_t)~mask;
                }
            }
            parse_t24_dataset_exact(swept, sizeof(swept), &res);
            bounded = bounded && res.associated_count <= 3 && res.discount_count <= 3 &&
                      res.supplement_count <= 3 && res.transfer_count <= 3 &&
                      res.interchange_count <= 7 && res.time_band_count <= 7 &&
                      res.service_count <= 7 && res.route_count <= 7;
            itso_reservation_free(&res);
        }
    }
    check("every count at every value stays within its group", bounded);

    /* All eight at their most, in a 252-byte dataset - the most IPELength can
     * say - inside a buffer that runs on with more of the same: the groups stop
     * at the dataset's end, not the buffer's. */
    static uint8_t big[512];
    static const uint8_t route_group[] = {203, 4, '1', '5', '5', '5', 0x40};
    for(size_t i = 0; i < sizeof(big); i++) {
        big[i] = route_group[i % sizeof(route_group)];
    }
    memcpy(big, reservation_group, RES_DATASET_LEN - 3);
    big[0] = (uint8_t)((63 << 2) | (big[0] & 0x03));
    for(uint32_t bit = 88; bit < 108; bit++) {
        big[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
    }
    ItsoReservation capped;
    parse_t24_dataset_exact(big, 252, &capped);
    parse_t24_dataset_exact(big, sizeof(big), &res);
    check("every count at its most runs past the dataset", res.valid && res.overrun);
    check(
        "and the dataset's end holds, however much follows it",
        res.route_count == capped.route_count && res.service_count == capped.service_count &&
            res.interchange_count == capped.interchange_count &&
            res.time_band_count == capped.time_band_count && !res.has_passenger);
    itso_reservation_free(&capped);
    itso_reservation_free(&res);
    itso_product_free(&p);
}
