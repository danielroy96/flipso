/**
 * @file flipso_product_legs.c
 * @brief A TYP 24 reserved journey's reservations: its booking, its legs, and when
 * it was last validated (the VGXRef 3 extension, TS 1000-5 table AD3).
 */
#include "flipso_product_i.h"

/** True when a reserved journey has reservations to show, or ought to. */
static bool flipso_has_reservations(const ItsoProduct* product, const ItsoReservation* res) {
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    if(!res || product->typ != ItsoTypReservationTicket || !ticket->valid) return false;
    return res->has_extension || ticket->reservations;
}

/**
 * What a reserved journey's VGXRef 3 extension (TS 1000-5 table AD3) says
 * beyond its legs, each on the page it belongs to: the booking reference with
 * the purchase, the last validation with the history, and whether the legs
 * could all be read with the ticket's details.
 */
void flipso_cat_reservation_record(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    if(!flipso_has_reservations(product, res)) return;
    FuriString* details = flipso_pages_at(p, FlipsoSlotDetails);
    if(!res->has_extension) {
        furi_string_cat(details, "Reservations: Could not be read\n");
        return;
    }
    const uint8_t expected = ticket->reservations;
    if(!res->leg_count && !expected) furi_string_cat(details, "Reserved legs: None\n");
    if(expected > res->leg_count) {
        furi_string_cat_printf(
            details, "Not read: %u of %u\n", expected - res->leg_count, expected);
    }

    FuriString* history = flipso_pages_at(p, FlipsoSlotHistory);
    if(res->last_validation) {
        flipso_cat_datetime_line(history, "", "Last validated", res->last_validation);
        flipso_cat_location(history, f, "  ", "At", &res->last_validation_at);
    } else {
        furi_string_cat(history, "Last validated: Never\n");
    }
}

/** A reserved journey's booking reference, which leads its Purchase page. */
void flipso_cat_booking(FuriString* out, const ItsoProduct* product, const ItsoReservation* res) {
    if(!flipso_has_reservations(product, res) || !res->has_extension) return;
    flipso_cat_ud_line(
        out, "", "Booking reference", (const uint8_t*)res->booking, strlen(res->booking));
}

/**
 * The seats, berths and spaces a reserved journey holds, a page to each leg,
 * led by what the holder looks for on the platform: when the train goes, and
 * the coach and seat. The kinds of place and seat direction are as RSPS3002
 * 3.8.6 defines them for rail.
 */
void flipso_cat_leg_pages(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    if(!flipso_has_reservations(product, res) || !res->has_extension) return;

    static const char* const places[] = {
        "Seat", "Sleeper berth", "Bicycle space", "No specific place", "Wheelchair space"};
    static const char* const directions[] = {NULL, "Forwards", "Backwards", "Airline style"};
    for(uint8_t i = 0; i < res->leg_count; i++) {
        const ItsoReservedLeg* leg = &res->legs[i];
        char title[12];
        snprintf(title, sizeof(title), "Leg %u", i + 1);
        flipso_cat_page(out, FlipsoIconSeat, title);
        flipso_cat_datetime_line(out, "", "Departs", leg->departs);
        flipso_cat_location(out, f, "", "From", &leg->from);
        flipso_cat_location(out, f, "", "To", &leg->to);
        if(leg->coach[0]) furi_string_cat_printf(out, "Coach: %s\n", leg->coach);
        if(leg->seat[0]) {
            furi_string_cat_printf(
                out, "%s: %s\n", leg->type == ItsoPlaceBerth ? "Berth" : "Seat", leg->seat);
        }
        if(leg->type < COUNT_OF(places)) {
            furi_string_cat_printf(out, "Reserved: %s\n", places[leg->type]);
        } else {
            furi_string_cat_printf(out, "Reserved: Other (%u)\n", leg->type);
        }
        if(directions[leg->direction & 0x03]) {
            furi_string_cat_printf(out, "Facing: %s\n", directions[leg->direction & 0x03]);
        }
        if(leg->attribute[0]) {
            const char* feature = itso_seat_attribute_name(leg->attribute);
            furi_string_cat_printf(out, "Feature: %s\n", feature ? feature : leg->attribute);
        }
        if(leg->berth == 1 || leg->berth == 2) {
            furi_string_cat_printf(out, "Bunk: %s\n", leg->berth == 1 ? "Lower" : "Upper");
            flipso_cat_flag(out, "  ", "Cabin shared", leg->together);
        }
        if(leg->service[0]) furi_string_cat_printf(out, "Train: %s\n", leg->service);
    }
}
