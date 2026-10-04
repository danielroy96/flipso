/**
 * @file itso_card.c
 * @brief The ItsoCard: what it owns, and the slots its products go in.
 */
#include "itso_i.h"

#include <string.h>
#include <stdlib.h>

void itso_card_init(ItsoCard* card) {
    memset(card, 0, sizeof(ItsoCard));
}

void itso_card_reset(ItsoCard* card) {
    /* Every slot allocated, not only the ones counted: a directory decoded
     * again starts its count over, and the slots past it still hold what the
     * last decode put there until they are reused. */
    for(uint8_t i = 0; i < card->product_capacity; i++) {
        itso_product_free(&card->products[i]);
    }
    free(card->products);
    free(card->taps);
    free(card->space);
    memset(card, 0, sizeof(ItsoCard));
}

void itso_card_free(ItsoCard* card) {
    itso_card_reset(card);
}

bool itso_card_equal(const ItsoCard* a, const ItsoCard* b) {
    if(a->product_count != b->product_count || a->tap_count != b->tap_count) return false;
    for(uint8_t i = 0; i < a->product_count; i++) {
        if(!itso_product_equal(&a->products[i], &b->products[i])) return false;
    }
    if(a->tap_count && memcmp(a->taps, b->taps, (size_t)a->tap_count * sizeof(ItsoTap)) != 0) {
        return false;
    }
    if(!a->space != !b->space) return false;
    if(a->space && memcmp(a->space, b->space, sizeof(ItsoSpaceSaving)) != 0) return false;
    /* Everything else, less where the arrays live and how much room is spare
     * behind them, which depend on how the card was built rather than what it
     * says. */
    ItsoCard x = *a, y = *b;
    x.products = y.products = NULL;
    x.product_capacity = y.product_capacity = 0;
    x.taps = y.taps = NULL;
    x.tap_capacity = y.tap_capacity = 0;
    x.space = y.space = NULL;
    return memcmp(&x, &y, sizeof(ItsoCard)) == 0;
}

/**
 * Make room for @p count products in all, keeping the ones already there.
 * The slots beyond them are zeroed. Capped at ITSO_MAX_CARD_PRODUCTS.
 *
 * @return false when the room could not be had; the card is unchanged then.
 */
bool itso_card_reserve(ItsoCard* card, uint8_t count) {
    if(count > ITSO_MAX_CARD_PRODUCTS) count = ITSO_MAX_CARD_PRODUCTS;
    if(count <= card->product_capacity) return true;
    ItsoProduct* grown = realloc(card->products, (size_t)count * sizeof(ItsoProduct));
    if(!grown) return false;
    memset(
        grown + card->product_capacity,
        0,
        (size_t)(count - card->product_capacity) * sizeof(ItsoProduct));
    card->products = grown;
    card->product_capacity = count;
    return true;
}

/** The next product slot, zeroed and counted, or NULL when there is no room. */
ItsoProduct* itso_card_next_product(ItsoCard* card) {
    if(card->product_count >= ITSO_MAX_CARD_PRODUCTS) return NULL;
    if(!itso_card_reserve(card, (uint8_t)(card->product_count + 1))) return NULL;
    ItsoProduct* product = &card->products[card->product_count++];
    itso_product_free(product);
    memset(product, 0, sizeof(*product));
    return product;
}

ItsoProduct* itso_card_add_product(ItsoCard* card, const uint8_t* entry, uint8_t index) {
    ItsoProduct* product = itso_card_next_product(card);
    if(!product) return NULL;
    itso_parse_dir_entry(product, entry, index);
    return product;
}

uint16_t itso_card_issuer_oid(const ItsoCard* card) {
    /* A compact shell's OID is the generic 8189 every compact shell carries
     * (TS 1000-10 table 42), which names no operator at all. The card holds one
     * product, and that product's owner is the operator whose ticket it is. */
    if(card->shell_compact && card->product_count && card->products[0].on_card) {
        return card->products[0].oid;
    }
    return card->oid;
}

bool itso_card_retired(const ItsoCard* card) {
    return card->chip_abacus_valid && card->chip_abacus >= 16;
}
