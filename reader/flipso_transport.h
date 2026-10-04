/**
 * @file flipso_transport.h
 * @brief What the transports share once a card has answered: the shell owner,
 * logged, and the walk of the products and the log for the transports that
 * read sector chains.
 */
#pragma once

#include "flipso_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log the shell owner: the operator that issued the card, and so the one whose
 * branding titles it.
 *
 * Called from inside each transport's read, so that CMD2 logs it as well as
 * DESFire, and because the result callback is documented above to return
 * promptly.
 *
 * Logged by number: the number is what a user needs in order to add their card
 * to the operator table, and unlike the card number it identifies a scheme
 * rather than a holder.
 *
 * Note for anyone debugging with this line: a USB log line can arrive cut off
 * mid-word. That is the serial stream, not the app - the pre-existing per-IPE
 * lines truncate the same way, and the device is running normally afterwards.
 * Re-read rather than concluding the app stopped where the text does.
 */
void flipso_log_shell_owner(const ItsoCard* card);

/**
 * Where a transport gets a card's data groups from.
 *
 * The two transports address sectors differently - a DESFire file number, a
 * CMD2 directory path - but once the shell and directory are in hand they walk
 * the products and the log the same way, and that walk lives in one place:
 * flipso_transport_read_groups().
 */
typedef struct {
    /** Read the sector chain starting at @p sector; the bytes are left in @p data. */
    size_t (*read_group)(void* context, uint8_t sector, const uint8_t** data);
    /** Read the cyclic log, or return 0 when the card keeps none. */
    size_t (*read_log)(void* context, const uint8_t** data);
    /** True once the card has stopped answering. */
    bool (*lost)(void* context);
    void* context;
} FlipsoGroupSource;

/**
 * Read and decode every product the directory lists, then the journey log.
 *
 * @param card    with the shell and directory already decoded into it.
 * @param capture keeps each group's bytes, so the card can be saved.
 * @return Success, or CardLost when the card left the field part way: a
 *         product that could not be read is not a product the card does not
 *         have, and finishing would show a card that looks read and is not.
 */
FlipsoReaderStatus flipso_transport_read_groups(
    ItsoCard* card,
    FlipsoCapture* capture,
    const FlipsoGroupSource* source);

#ifdef __cplusplus
}
#endif
