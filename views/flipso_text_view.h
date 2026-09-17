/**
 * @file flipso_text_view.h
 * @brief A scrolling text panel that wraps on word boundaries.
 *
 * The firmware's text scroll element breaks a line wherever it runs out of
 * pixels, which on a 128px screen splits the long names this app shows all the
 * time - "South Western Rail/way", "Greater London (Fr/eedom Pass)". This is
 * otherwise the same thing: same \e# bold-header markup, same scrollbar, Up
 * and Down scroll by a line.
 *
 * The text is copied in, so the caller may free its own buffer afterwards.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoTextView FlipsoTextView;

FlipsoTextView* flipso_text_view_alloc(void);
void flipso_text_view_free(FlipsoTextView* instance);
View* flipso_text_view_get_view(FlipsoTextView* instance);

/**
 * Replace the panel's contents and scroll back to the top.
 *
 * @param text lines separated by '\n'. A line beginning "\e#" is drawn in the
 *             bold font, as the firmware's text scroll element does.
 */
void flipso_text_view_set_text(FlipsoTextView* instance, const char* text);

#ifdef __cplusplus
}
#endif
