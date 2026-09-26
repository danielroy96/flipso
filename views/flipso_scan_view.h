/**
 * @file flipso_scan_view.h
 * @brief The card screen: an idle prompt, then an animated contactless indicator.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoScanView FlipsoScanView;

/** Invoked when the user presses one of the idle prompt's buttons. */
typedef void (*FlipsoScanViewCallback)(void* context);

FlipsoScanView* flipso_scan_view_alloc(void);
void flipso_scan_view_free(FlipsoScanView* instance);
View* flipso_scan_view_get_view(FlipsoScanView* instance);

/**
 * Set the handlers for the idle prompt's two buttons.
 *
 * @param scan  OK: start reading a card.
 * @param saved Left: open the cards already saved. Whether that button is on
 *              the screen at all is flipso_scan_view_set_has_saved()'s job.
 * @param about Right: what the app is, and what it has to work with.
 */
void flipso_scan_view_set_callback(
    FlipsoScanView* instance,
    FlipsoScanViewCallback scan,
    FlipsoScanViewCallback saved,
    FlipsoScanViewCallback about,
    void* context);

/**
 * Offer the Left button, or not.
 *
 * The scene asks whether there is anything saved before each visit, so the
 * button appears the moment the first card is saved and goes again when the
 * last one is deleted - rather than sitting there leading to an empty list.
 */
void flipso_scan_view_set_has_saved(FlipsoScanView* instance, bool has_saved);

/**
 * Switch between the idle prompt (false) and the active reader (true). The
 * view animates itself while scanning, and is still while it is not.
 */
void flipso_scan_view_set_scanning(FlipsoScanView* instance, bool scanning);

#ifdef __cplusplus
}
#endif
