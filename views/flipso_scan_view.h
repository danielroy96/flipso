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

/** Invoked when the user presses OK on the idle prompt. */
typedef void (*FlipsoScanViewCallback)(void* context);

FlipsoScanView* flipso_scan_view_alloc(void);
void flipso_scan_view_free(FlipsoScanView* instance);
View* flipso_scan_view_get_view(FlipsoScanView* instance);

/** Set the handler for the OK press that starts a scan. */
void flipso_scan_view_set_callback(
    FlipsoScanView* instance,
    FlipsoScanViewCallback callback,
    void* context);

/** Switch between the idle prompt (false) and the active reader (true). */
void flipso_scan_view_set_scanning(FlipsoScanView* instance, bool scanning);

/** Advance the animation by one frame. Call from the scene tick handler. */
void flipso_scan_view_tick(FlipsoScanView* instance);

#ifdef __cplusplus
}
#endif
