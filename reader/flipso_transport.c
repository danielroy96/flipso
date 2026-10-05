/**
 * @file flipso_transport.c
 * @brief What the transports share once a card has answered: see flipso_transport.h.
 */
#include "flipso_transport.h"
#include "../itso/itso_operators.h"

#include <furi.h>

#define TAG "Flipso"

void flipso_log_shell_owner(const ItsoCard* card) {
    if(!card->shell_valid) return;

    const char* name = itso_operator_name(card->oid);
    FURI_LOG_I(TAG, "Shell owner: OID %u (%s)", card->oid, name ? name : "unknown");
}

/* ------------------------------------------------------------------ */
/* Products and the log, for the transports that walk sector chains    */
/* ------------------------------------------------------------------ */

FlipsoReaderStatus flipso_transport_read_groups(
    ItsoCard* card,
    FlipsoCapture* capture,
    const FlipsoGroupSource* source) {
    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const uint8_t* group = NULL;
        size_t len = source->read_group(source->context, product->dir_index, &group);
        bool lost = source->lost(source->context);
        if(len && !lost) {
            flipso_capture_add(capture, FlipsoBlockProduct, product->dir_index, group, len);
            itso_parse_ipe(product, group, len, card->sector_size);
        }

        /* A product that could not be read is not a product the card does not
         * have. Carrying on would finish the scan with entries the directory
         * names and nothing behind them, chirp success and show the user a card
         * that looks read and is empty - so stop, and let the scan retry the
         * way it does for a card that drops out anywhere else. Stopping at the
         * first one also saves the timeout on each remaining read. */
        if(lost) {
            FURI_LOG_W(TAG, "Card left the field at entry %u of %u", i + 1, card->product_count);
            return FlipsoReaderStatusCardLost;
        }
    }

    if(card->log_dir_index) {
        const uint8_t* log = NULL;
        size_t len = source->read_log(source->context, &log);
        /* The journey list is the last thing read and the same argument
         * applies to it: a card showing three of its taps because the fourth
         * did not arrive is worse than being asked to tap again. */
        if(source->lost(source->context)) {
            FURI_LOG_W(TAG, "Card left the field reading the journey log");
            return FlipsoReaderStatusCardLost;
        }
        if(len) {
            flipso_capture_add(capture, FlipsoBlockLog, 0, log, len);
            itso_parse_log(card, log, len);
        }
    }

    return FlipsoReaderStatusSuccess;
}
