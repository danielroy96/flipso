/**
 * @file flipso_product_pages.c
 * @brief Which pages a product's screen has, and the order they are shown in.
 */
#include "flipso_product_i.h"

static FlipsoKind flipso_kind(const ItsoProduct* product) {
    switch(product->typ) {
    case ItsoTypPeriodTicket:
        return FlipsoKindPeriod;
    case ItsoTypJourneyTicket:
        return FlipsoKindJourney;
    case ItsoTypReservationTicket:
        return FlipsoKindReserved;
    case ItsoTypStoredTravelRights:
        return FlipsoKindPurse;
    case ItsoTypChargeToAccount1:
    case ItsoTypChargeToAccount2:
        return FlipsoKindAccount;
    case ItsoTypId:
        return FlipsoKindId;
    case ItsoTypEntitlement:
        return FlipsoKindEntitlement;
    case ItsoTypVoucher:
        return FlipsoKindVoucher;
    case ItsoTypTolling:
        return FlipsoKindToll;
    default:
        return product->space_saving ? FlipsoKindPaper : FlipsoKindOther;
    }
}

FlipsoPages* flipso_pages_alloc(const ItsoProduct* product) {
    FlipsoPages* p = malloc(sizeof(FlipsoPages));
    p->kind = flipso_kind(product);
    for(size_t i = 0; i < FlipsoPageCount; i++) {
        p->page[i] = furi_string_alloc();
    }

    /* What most tickets have: a page for what is left, one for the terms -
     * where who it covers sits with when it can be used - and one for the
     * purchase. A route and the details beyond it are a reserved journey's
     * alone, so anything else's would be terms. */
    FlipsoPage* s = p->slot;
    s[FlipsoSlotMain] = FlipsoPageMain;
    s[FlipsoSlotLeft] = FlipsoPageLeft;
    s[FlipsoSlotRules] = FlipsoPageRules;
    s[FlipsoSlotWho] = FlipsoPageRules;
    s[FlipsoSlotRoute] = FlipsoPageRules;
    s[FlipsoSlotDetails] = FlipsoPageRules;
    s[FlipsoSlotPurchase] = FlipsoPagePurchase;
    s[FlipsoSlotHistory] = FlipsoPageHistory;
    s[FlipsoSlotOffCard] = FlipsoPageOffCard;

    switch(p->kind) {
    case FlipsoKindReserved:
        /* Its trains and times are the restrictions; how many journeys are
         * left, and who it covers, are details of the ticket. */
        s[FlipsoSlotLeft] = FlipsoPageDetails;
        s[FlipsoSlotWho] = FlipsoPageDetails;
        s[FlipsoSlotRoute] = FlipsoPageRoute;
        s[FlipsoSlotDetails] = FlipsoPageDetails;
        break;
    case FlipsoKindPurse:
        /* Who sold a purse is part of whose money it is, so it follows the
         * operator on the first page; its terms are its top-up and limits. */
        s[FlipsoSlotRules] = s[FlipsoSlotWho] = s[FlipsoSlotRoute] = s[FlipsoSlotDetails] =
            FlipsoPageLeft;
        s[FlipsoSlotPurchase] = FlipsoPageMain;
        break;
    case FlipsoKindAccount:
        s[FlipsoSlotRules] = s[FlipsoSlotWho] = s[FlipsoSlotRoute] = s[FlipsoSlotDetails] =
            s[FlipsoSlotPurchase] = FlipsoPageLeft;
        break;
    case FlipsoKindId:
        /* What the ID says about the holder is a page of its own; the terms
         * the card was issued on, and who issued it, are the other. */
        s[FlipsoSlotPurchase] = FlipsoPageRules;
        break;
    case FlipsoKindEntitlement:
        s[FlipsoSlotLeft] = s[FlipsoSlotPurchase] = FlipsoPageRules;
        break;
    case FlipsoKindVoucher:
    case FlipsoKindToll:
        /* Uses or crossings left and how they renew are what a voucher or a
         * toll pass is, so they are its first page; neither covers travellers,
         * so anything about who - a toll pass's vehicle class - is terms. */
        s[FlipsoSlotLeft] = FlipsoPageMain;
        break;
    case FlipsoKindOther:
        s[FlipsoSlotPurchase] = FlipsoPageMain;
        break;
    default:
        break;
    }
    return p;
}

void flipso_pages_free(FlipsoPages* p) {
    for(size_t i = 0; i < FlipsoPageCount; i++) {
        furi_string_free(p->page[i]);
    }
    free(p);
}

/** The page a slot's lines go on. */
FuriString* flipso_pages_at(FlipsoPages* p, FlipsoSlot slot) {
    return p->page[p->slot[slot]];
}

/** A page of @p p, under its title, where it has anything on it. */
static void flipso_pages_put(
    FuriString* out,
    FlipsoPages* p,
    FlipsoPage page,
    FlipsoIcon icon,
    const char* title) {
    flipso_cat_page_from(out, icon, title, p->page[page]);
}

/**
 * The pages flipso_cat_product_details() filled, in the order the holder
 * reads them for the kind of product it is, with a reserved journey's legs
 * and any fare capping in their places. Technical is the caller's to add.
 */
void flipso_pages_emit(
    FuriString* out,
    FlipsoPages* p,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    const ItsoReservation* res) {
    /* The first page is the product, by name, even with nothing decoded on it. */
    flipso_cat_page(
        out,
        product->on_card ? flipso_product_icon(product) : FlipsoIconPast,
        flipso_product_title(product));
    furi_string_cat(out, p->page[FlipsoPageMain]);
    furi_string_reset(p->page[FlipsoPageMain]);

    switch(p->kind) {
    case FlipsoKindPeriod:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconPass, "Passes");
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Conditions");
        break;
    case FlipsoKindJourney:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconTicket, "Rides");
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Conditions");
        break;
    case FlipsoKindReserved:
        /* The seat first: on the platform it is what the holder is looking for. */
        flipso_cat_leg_pages(out, f, product, res);
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Restrictions");
        flipso_pages_put(out, p, FlipsoPageRoute, FlipsoIconRoute, "Route");
        flipso_pages_put(out, p, FlipsoPageDetails, FlipsoIconInfo, "Details");
        break;
    case FlipsoKindPaper:
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Conditions");
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconTaps, "Use");
        break;
    case FlipsoKindPurse:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconTopUp, "Top-up");
        break;
    case FlipsoKindAccount:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconAccount, "Account");
        break;
    case FlipsoKindId:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconId, "Holder");
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "ID terms");
        break;
    case FlipsoKindEntitlement:
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Entitlement terms");
        break;
    case FlipsoKindVoucher:
    case FlipsoKindToll:
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Conditions");
        break;
    case FlipsoKindOther:
        flipso_pages_put(out, p, FlipsoPageLeft, FlipsoIconInfo, "Details");
        flipso_pages_put(out, p, FlipsoPageRules, FlipsoIconTerms, "Conditions");
        break;
    }
    flipso_pages_put(out, p, FlipsoPagePurchase, FlipsoIconPurchase, "Purchase");
    flipso_cat_capping(out, f, card, product);
    flipso_pages_put(out, p, FlipsoPageHistory, FlipsoIconPast, "History");
    flipso_pages_put(out, p, FlipsoPageOffCard, FlipsoIconPast, "Off card");

    /* Every slot lands on a page its kind shows, so nothing should be left;
     * if a kind ever gains a line with nowhere to go, it is shown, not lost. */
    for(size_t i = 0; i < FlipsoPageCount; i++) {
        flipso_pages_put(out, p, (FlipsoPage)i, FlipsoIconInfo, "More");
    }
}
