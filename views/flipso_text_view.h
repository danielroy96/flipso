/**
 * @file flipso_text_view.h
 * @brief A scrolling text panel that wraps on word boundaries.
 *
 * The firmware's text scroll element breaks a line wherever it runs out of
 * pixels, which on a 128px screen splits the long names this app shows all the
 * time - "South Western Rail/way", "Greater London (Fr/eedom Pass)". This is
 * otherwise the same thing: same \e# bold-header markup, same scrollbar, Up
 * and Down scroll by a line, and Left and Right by a screen.
 *
 * Leading spaces indent a line, and the indent holds for every row the line
 * wraps onto, so a detail written under its heading stays visibly under it. A
 * "Label: value" line that wraps hangs its continuation in by a step, so the
 * value's second row does not read as a label of its own.
 *
 * The text is copied in, so the caller may free its own buffer afterwards.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoTextView FlipsoTextView;

/**
 * A heading's icon is written as one byte straight after the "\e#": icon n of
 * the table is the byte FLIPSO_TEXT_ICON_BASE + n. The bytes that gives are
 * control characters no text uses, and they start above '\n' so an icon number
 * can never end the line it is on.
 */
#define FLIPSO_TEXT_ICON_BASE 0x10
#define FLIPSO_TEXT_MAX_ICONS 15

FlipsoTextView* flipso_text_view_alloc(void);
void flipso_text_view_free(FlipsoTextView* instance);
View* flipso_text_view_get_view(FlipsoTextView* instance);

/**
 * Replace the panel's contents and scroll back to the top.
 *
 * @param text lines separated by '\n'. A line beginning "\e#" is drawn in the
 *             bold font, as the firmware's text scroll element does; if the
 *             next byte is between 1 and the icon table's size, the icon it
 *             numbers is drawn in front of the heading.
 */
void flipso_text_view_set_text(FlipsoTextView* instance, const char* text);

/**
 * Icons a heading can name, numbered from 1 in the order given.
 *
 * Not copied: the table and the icons must outlive the view, which static
 * const app data does.
 */
void flipso_text_view_set_icons(FlipsoTextView* instance, const Icon* const* icons, uint8_t count);

#ifdef __cplusplus
}
#endif
