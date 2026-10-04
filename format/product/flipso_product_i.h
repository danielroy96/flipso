/**
 * @file flipso_product_i.h
 * @brief What the files that build a product's screen share with each other.
 *
 * A product's screen is a set of pages, and which pages depends on what kind of
 * product it is: a season ticket's first question is not a purse's. The lines
 * are written by blocks that each know one part of a product - its ticket
 * terms, its reservations, its holder - and write into slots rather than pages,
 * so that one block serves every kind; flipso_product_pages.c maps each kind's
 * slots onto its pages and shows them in its order.
 */
#pragma once

#include "../flipso_format_i.h"

#ifdef __cplusplus
extern "C" {
#endif

/** True for an ITSO ID (TYP 16) or entitlement (TYP 14). */
static inline bool flipso_product_is_identity(const ItsoProduct* product) {
    return itso_product_family(product->typ) == ItsoFamilyId;
}

/**
 * What a product is to the holder, which decides the pages its screen has and
 * what goes on each: a season ticket's first question is not a purse's.
 */
typedef enum {
    FlipsoKindPeriod, /**< TYP 22: a season, rail point to point or zonal. */
    FlipsoKindJourney, /**< TYP 23: rides, a carnet, a single or return. */
    FlipsoKindReserved, /**< TYP 24: a rail ticket with its trains and seats. */
    FlipsoKindPaper, /**< TYP 27, 28 and 29: the Space Saving paper tickets. */
    FlipsoKindPurse, /**< TYP 2: pay as you go. */
    FlipsoKindAccount, /**< TYP 4 and 5: charge to account. */
    FlipsoKindId, /**< TYP 16: the ITSO ID. */
    FlipsoKindEntitlement, /**< TYP 14. */
    FlipsoKindVoucher, /**< TYP 25: a car park or a meal bought with a ticket. */
    FlipsoKindOther, /**< Loyalty, tolls and anything else. */
} FlipsoKind;

/**
 * Where a line belongs, as the blocks that write them see it. Each kind maps
 * these onto the pages it has (flipso_pages_alloc()), so a block need not know
 * which product it is writing for: a reserved journey's travellers go on its
 * Details page and a season ticket's on its Conditions page, and a purse with
 * no page for terms puts any it has on Top-up.
 */
typedef enum {
    FlipsoSlotMain, /**< The first page: is it good, where, until when, whose. */
    FlipsoSlotLeft, /**< What is left of it, and how it renews. */
    FlipsoSlotRules, /**< When it can be used: days, times, trains. */
    FlipsoSlotWho, /**< Who it covers: travellers, class, photocard. */
    FlipsoSlotRoute, /**< A reserved journey's permitted route. */
    FlipsoSlotDetails, /**< A reserved journey's terms beyond those. */
    FlipsoSlotPurchase, /**< Who sold it, when, and for how much. */
    FlipsoSlotHistory, /**< What has happened to it on the card. */
    FlipsoSlotOffCard, /**< What only the saved file remembers. */
    FlipsoSlotCount,
} FlipsoSlot;

/* The pages a product's screen can have, in the order a kind lists them in
 * flipso_pages_emit(); the slots above all land on one of these. */
typedef enum {
    FlipsoPageMain,
    FlipsoPageLeft,
    FlipsoPageRules,
    FlipsoPageRoute,
    FlipsoPageDetails,
    FlipsoPagePurchase,
    FlipsoPageHistory,
    FlipsoPageOffCard,
    FlipsoPageCount,
} FlipsoPage;

typedef struct {
    FlipsoKind kind;
    FuriString* page[FlipsoPageCount];
    FlipsoPage slot[FlipsoSlotCount];
} FlipsoPages;

/* --- flipso_product_pages.c --- */

/** The pages for @p product's kind, empty, with its slots mapped onto them. */
FlipsoPages* flipso_pages_alloc(const ItsoProduct* product);

void flipso_pages_free(FlipsoPages* p);

/** The page a slot's lines go on. */
FuriString* flipso_pages_at(FlipsoPages* p, FlipsoSlot slot);

/**
 * The pages flipso_cat_product_details() filled, in the order the holder reads
 * them for the kind of product it is. Technical is the caller's to add.
 */
void flipso_pages_emit(
    FuriString* out,
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res);

/* --- flipso_product_details.c --- */

/**
 * Everything a screen says about one product but its codes, each line on the
 * page it belongs to.
 *
 * @param res a reserved journey's dataset and reservations, from
 *            flipso_decode_reservation(); NULL for any other product.
 */
void flipso_cat_product_details(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res);

/* --- flipso_product_technical.c --- */

/** A product's Technical page, less its title. @p res as for the details. */
void flipso_cat_product_technical(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res);

/* --- flipso_product_ticket.c: TYP 22 and 23, and what TYP 27-29 share --- */

/** The days, times, renewal and travellers a ticket was sold on. */
void flipso_cat_ticket_terms(FlipsoPages* p, const ItsoProduct* product);

/** When a ticket was issued and what was paid for it. */
void flipso_cat_ticket_price(FuriString* out, const ItsoProduct* product);

/* --- flipso_product_reservation.c: TYP 24 --- */

/**
 * Decode a reserved journey's dataset and reservations into @p res, from the
 * capture. @return false when the product is not one, or it did not decode.
 */
bool flipso_decode_reservation(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoReservation* res);

/** One portion of a reserved journey: "Outward: 01/10/2026 to 31/10/2026". */
void flipso_cat_portion(FuriString* out, const char* label, ItsoDts from_dts, uint16_t days);

/** What NumberOfJourneysSold buys, given ProductTypeEncoding. */
void flipso_cat_sold_as(FuriString* out, const ItsoTicketTerms* t);

/** The number of the railcard or ID a reserved journey is held to. */
void flipso_cat_reservation_id(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res);

/** A reserved journey's restrictions, route and details, each on its page. */
void flipso_cat_reservation(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res);

/** Where a reserved journey was sold, unless the retailer already named it. */
void flipso_cat_sold_at(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res);

/** The codes and unset flags behind a reserved journey, for Technical. */
void flipso_cat_reservation_codes(
    FuriString* out,
    const ItsoProduct* product,
    const ItsoReservation* res);

/* --- flipso_product_legs.c: TYP 24's reservations --- */

/** What a reserved journey's reservations extension says beyond its legs. */
void flipso_cat_reservation_record(
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res);

/** A reserved journey's booking reference, which leads its Purchase page. */
void flipso_cat_booking(FuriString* out, const ItsoProduct* product, const ItsoReservation* res);

/** A page for each reserved leg: when, from and to, coach and seat. */
void flipso_cat_leg_pages(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoProduct* product,
    const ItsoReservation* res);

/* --- flipso_product_space_saving.c: TYP 27, 28 and 29 --- */

/** Where a Space Saving ticket is good. */
void flipso_cat_space_area(FuriString* out, const FlipsoFormat* f, const ItsoCard* card);

/** A Space Saving ticket's own conditions, and the passes or journeys it counts. */
void flipso_cat_space_saving(FlipsoPages* p, const ItsoCard* card, const ItsoProduct* product);

/** The codes behind a Space Saving ticket's area, for Technical. */
void flipso_cat_space_codes(FuriString* out, const ItsoCard* card);

/** What TYP 29's backup count says is left, and whether it agrees, for Technical. */
void flipso_cat_space_backup(FuriString* out, const ItsoCard* card, const ItsoProduct* product);

/* --- flipso_product_id.c: TYP 14 and 16 --- */

/** An ID's holder details and terms beyond its name and entitlement. */
void flipso_cat_id_details(FlipsoPages* p, const ItsoProduct* product);

/** An ID's deposits, and whether the card says they come back. */
void flipso_cat_id_deposits(FuriString* out, const ItsoProduct* product);

/* --- flipso_product_purse.c: TYP 2, 4 and 5 --- */

/** A purse's or account's limits, auto-top-up and deposit. */
void flipso_cat_purse_terms(FuriString* out, const ItsoProduct* product);

/**
 * Decode a product's capping extension into @p cap, from the capture.
 * @return false when the product has none, or it did not decode.
 */
bool flipso_decode_capping(
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    ItsoCapping* cap);

/** A Fare capping page, when the product has a capping extension. */
void flipso_cat_capping(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product);

/* --- flipso_product_history.c --- */

/** What the product's newest value record says the last transaction was. */
void flipso_cat_last_transaction(FuriString* out, const ItsoProduct* product);

/** The transactions before the live one, newest first, on and off the card. */
void flipso_cat_value_history(FlipsoPages* p, const ItsoProduct* product);

#ifdef __cplusplus
}
#endif
