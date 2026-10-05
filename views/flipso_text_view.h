/**
 * @file flipso_text_view.h
 * @brief A paged, scrolling text panel that wraps on word boundaries.
 *
 * The firmware's text scroll element breaks a line wherever it runs out of
 * pixels, which on a 128px screen splits the long names this app shows all the
 * time - "South Western Rail/way", "Greater London (Fr/eedom Pass)". This is
 * otherwise the same thing: same \e# bold-header markup, same scrollbar, and
 * Up and Down scroll by a line.
 *
 * The text can be cut into pages with FLIPSO_TEXT_PAGE, and Left and Right
 * move between them. A page that opens with a heading keeps it as a fixed
 * title row while the rest scrolls beneath, and that row carries an arrow at
 * each side there is another page to go to - so the first page has no left
 * arrow and the last no right one, which is how the holder knows they have
 * read everything. There is no wrapping round from the last page to the first.
 *
 * Leading spaces indent a line, and the indent holds for every row the line
 * wraps onto, so a detail written under its heading stays visibly under it. A
 * "Label: value" line that wraps hangs its continuation in by a step, so the
 * value's second row does not read as a label of its own.
 *
 * The text is copied in, so the caller may free its own buffer afterwards.
 */
#pragma once

#include <furi.h>
#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoTextView FlipsoTextView;

/**
 * A heading's icon is written as one byte straight after the "\e#": icon n of
 * the table is the byte FLIPSO_TEXT_ICON_BASE + n. The bytes that gives start
 * above '\n', so an icon number can never end the line it is on, and stop
 * short of '0': past the control characters they run into the space and
 * punctuation, which no heading starts with, but a heading may start with a
 * digit.
 */
#define FLIPSO_TEXT_ICON_BASE 0x10
#define FLIPSO_TEXT_MAX_ICONS ('0' - FLIPSO_TEXT_ICON_BASE - 1)

/**
 * Starts a new page. Written straight before the page's heading, so every
 * page but the first opens "\f\e#Title".
 */
#define FLIPSO_TEXT_PAGE '\f'

FlipsoTextView* flipso_text_view_alloc(void);
void flipso_text_view_free(FlipsoTextView* instance);
View* flipso_text_view_get_view(FlipsoTextView* instance);

/**
 * Replace the panel's contents, and go back to the top of the first page.
 *
 * @param text lines separated by '\n', and pages by FLIPSO_TEXT_PAGE. A line
 *             beginning "\e#" is drawn in the bold font, as the firmware's
 *             text scroll element does; if the next byte is between 1 and the
 *             icon table's size, the icon it numbers is drawn in front of the
 *             heading.
 */
void flipso_text_view_set_text(FlipsoTextView* instance, const char* text);

/**
 * flipso_text_view_set_text(), taking @p text rather than copying it: the view
 * owns it from here, and frees it along with whatever it held before. A
 * screen's text is the largest thing on the heap while it is open, so this is
 * how the app hands it over - it is never held twice, and taking an empty
 * string gives back the last screen's whole buffer.
 */
void flipso_text_view_take_text(FlipsoTextView* instance, FuriString* text);

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
