/**
 * @file flipso_format_product.c
 * @brief The product, Pay as you go and ID screens.
 *
 * Each product's screen is assembled here from the parts beside it: the pages
 * its kind has (flipso_product_pages.c), the lines that fill them
 * (flipso_product_details.c and one file per kind of product), and its
 * Technical page (flipso_product_technical.c).
 */
#include "flipso_product_i.h"

/**
 * Every page of one product, Technical last unless @p technical is false, for
 * a screen that puts several products' codes on one page of its own.
 */
static void flipso_cat_product_screen(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product,
    bool technical) {
    const ItsoTicketTerms* ticket = itso_product_ticket(product);
    /* A reserved journey's dataset and reservations, decoded once for every
     * page and released before the text is shown. */
    ItsoReservation* res = NULL;
    if(product->typ == ItsoTypReservationTicket && ticket->valid) {
        res = malloc(sizeof(ItsoReservation));
        flipso_decode_reservation(f, card, product, res);
    }
    FlipsoPages* pages = flipso_pages_alloc(product);
    flipso_cat_product_details(pages, f, card, product, res);
    flipso_pages_emit(out, pages, f, card, product, res);
    flipso_pages_free(pages);
    if(technical) {
        flipso_cat_page(out, FlipsoIconCode, "Technical");
        flipso_cat_product_technical(out, f, card, product, res);
    }
    if(res) {
        itso_reservation_free(res);
        free(res);
    }
}

/**
 * A screen of every product the card holds that @p wanted picks: each one's
 * pages in turn, then one Technical page for all of them, each product's codes
 * under its name when there is more than one - so Technical is the screen's
 * last page however many there are. Nothing at all when there are none.
 *
 * @return how many products it showed.
 */
static uint8_t flipso_cat_products_screen(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    bool (*wanted)(const ItsoProduct* product)) {
    uint8_t found = 0;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        /* What the card holds now; a product it has dropped is the product
         * list's to show. */
        if(!product->on_card || !wanted(product)) continue;
        found++;
        flipso_cat_product_screen(out, f, card, product, false);
    }
    if(!found) return 0;

    flipso_cat_page(out, FlipsoIconCode, "Technical");
    bool first = true;
    for(uint8_t i = 0; i < card->product_count; i++) {
        const ItsoProduct* product = &card->products[i];
        if(!product->on_card || !wanted(product)) continue;
        if(found > 1) {
            if(!first) furi_string_push_back(out, '\n');
            flipso_cat_heading(out, FlipsoIconNone, flipso_product_title(product));
        }
        first = false;
        flipso_cat_product_technical(out, f, card, product, NULL);
    }
    return found;
}

static bool flipso_product_is_purse(const ItsoProduct* product) {
    return product->typ == ItsoTypStoredTravelRights;
}

void flipso_format_payg(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    /* The product list leaves these out, so this is the only screen with room
     * for the rest of what the card says about them. */
    if(!flipso_cat_products_screen(out, f, card, flipso_product_is_purse)) {
        flipso_cat_page(out, FlipsoIconPurse, "Pay as you go");
        furi_string_cat(out, "No purse on this card.\n");
    }
}

void flipso_format_id(FuriString* out, const FlipsoFormat* f, const ItsoCard* card) {
    if(!flipso_cat_products_screen(out, f, card, flipso_product_is_identity)) {
        flipso_cat_page(out, FlipsoIconId, "ID");
        furi_string_cat(out, "No identity product on this card.\n");
    }
}

void flipso_format_product(
    FuriString* out,
    const FlipsoFormat* f,
    const ItsoCard* card,
    const ItsoProduct* product) {
    flipso_cat_product_screen(out, f, card, product, true);
}
