/**
 * @file flipso_capture_decode.c
 * @brief A capture back into a card: its blocks through the same decoder a tap uses.
 */
#include "flipso_capture_i.h"

bool flipso_capture_decode(const FlipsoCapture* capture, ItsoCard* card) {
    if(!capture || !card) return false;

    itso_card_reset(card);

    /* A Type 2 tag is one flat block of pages with its data groups at fixed
     * offsets, so it decodes on its own rather than through the shell/directory/
     * product sequence the other media use. */
    const FlipsoCaptureBlock* type2 = flipso_capture_find(capture, FlipsoBlockType2, 0);
    if(type2) return itso_parse_type2(card, capture->bytes + type2->offset, type2->len);

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(!shell) return false;
    if(!itso_parse_shell(card, capture->bytes + shell->offset, shell->len)) return false;

    /* A CMD9 or CMD10 card's chip pages, which say which chip it is and, on a
     * CMD9, how far its Abacus has counted. After the shell, which names the
     * chip; before the rest, as a live read takes them. */
    const FlipsoCaptureBlock* tag = flipso_capture_find(capture, FlipsoBlockTag, 0);
    if(tag) itso_parse_type2_tag(card, capture->bytes + tag->offset, tag->len);

    /* From here the sequence mirrors flipso_desfire_read(), including what it does
     * when a group is missing: a directory that will not parse still leaves the
     * card number and expiry on screen, and a product with no block keeps
     * whatever its directory entry said about it. */
    const FlipsoCaptureBlock* dir = flipso_capture_find(capture, FlipsoBlockDirectory, 0);
    if(dir) itso_parse_directory(card, capture->bytes + dir->offset, dir->len);

    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const FlipsoCaptureBlock* group =
            flipso_capture_find(capture, FlipsoBlockProduct, product->dir_index);
        if(group) {
            itso_parse_ipe(product, capture->bytes + group->offset, group->len, card->sector_size);
        }
    }

    const FlipsoCaptureBlock* log = flipso_capture_find(capture, FlipsoBlockLog, 0);
    if(log) itso_parse_log(card, capture->bytes + log->offset, log->len);

    /* Then whatever earlier reads of this card saw, which only a saved card
     * carries. It goes in after the live blocks on purpose: the card is the
     * authority on what it holds now, and these only fill in what has since
     * rolled off the end of its own rolling windows. */
    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const FlipsoCaptureBlock* history =
            flipso_capture_find(capture, FlipsoBlockValueHistory, product->dir_index);
        if(history) {
            itso_parse_value_history(product, capture->bytes + history->offset, history->len);
        }
    }

    /* And the products the card has dropped altogether, appended after the ones
     * it still lists so that a screen walking the array shows the card before
     * it shows the card's past. Each carries the directory entry that described
     * it, because the directory in this file no longer does. */
    for(uint8_t slot = 0; slot < ITSO_MAX_HISTORIC_PRODUCTS; slot++) {
        uint8_t index = (uint8_t)(FLIPSO_CAPTURE_HISTORY_BASE + slot);
        const FlipsoCaptureBlock* gone =
            flipso_capture_find(capture, FlipsoBlockProductHistory, index);
        if(!gone || gone->len < FLIPSO_PRODUCT_HISTORY_HEADER) continue;

        const uint8_t* data = capture->bytes + gone->offset;
        uint32_t last_seen = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                             ((uint32_t)data[2] << 8) | data[3];

        ItsoProduct* product = itso_card_add_product(card, data + 5, data[4]);
        if(!product) break;

        itso_parse_ipe(
            product,
            data + FLIPSO_PRODUCT_HISTORY_HEADER,
            gone->len - FLIPSO_PRODUCT_HISTORY_HEADER,
            card->sector_size);

        /* Its own value history is keyed by the same slot, not by the entry it
         * used to hold: that entry may belong to a live product by now. */
        const FlipsoCaptureBlock* history =
            flipso_capture_find(capture, FlipsoBlockValueHistory, index);
        if(history) {
            itso_parse_value_history(product, capture->bytes + history->offset, history->len);
        }

        /* Last, so that the records the group itself held are marked too: they
         * were live when it was captured, and are not now. */
        itso_product_off_card(product, last_seen);
    }

    const FlipsoCaptureBlock* log_history = flipso_capture_find(capture, FlipsoBlockLogHistory, 0);
    if(log_history) {
        itso_parse_log_history(card, capture->bytes + log_history->offset, log_history->len);
    }

    return true;
}
