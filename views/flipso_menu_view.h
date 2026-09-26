/**
 * @file flipso_menu_view.h
 * @brief A scrolling list whose rows carry an icon.
 *
 * The firmware's Submenu module has no icon slot, and on a 128x64 screen the
 * icon is what lets a row be recognised without reading it. This is otherwise
 * a plain submenu: same header, same wrap-around navigation, same scrollbar.
 *
 * Items are copied in, so the caller may build labels on the stack. The list
 * holds at most FLIPSO_MENU_MAX_ITEMS of them; anything beyond that is dropped
 * rather than overflowing, which is why the products scene asserts that the
 * shell cannot carry more products than the list can show.
 */
#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Enough for every directory entry an ITSO shell can hold, plus the products a
 * saved card remembers from before the card dropped them. */
#define FLIPSO_MENU_MAX_ITEMS 20
#define FLIPSO_MENU_LABEL_LEN 32
/** Longest tag, terminator included: "99 + 99 off card". */
#define FLIPSO_MENU_TAG_LEN   18

typedef struct FlipsoMenuView FlipsoMenuView;

/** Invoked on the UI thread when the user presses OK on a row. */
typedef void (*FlipsoMenuViewCallback)(void* context, uint32_t id);

FlipsoMenuView* flipso_menu_view_alloc(void);
void flipso_menu_view_free(FlipsoMenuView* instance);
View* flipso_menu_view_get_view(FlipsoMenuView* instance);

void flipso_menu_view_set_callback(
    FlipsoMenuView* instance,
    FlipsoMenuViewCallback callback,
    void* context);

/** Drop every item and the header, ready to be rebuilt. */
void flipso_menu_view_reset(FlipsoMenuView* instance);

/** Set the title above the list. Pass NULL for a list with no header. */
void flipso_menu_view_set_header(FlipsoMenuView* instance, const char* header);

/**
 * Draw @p icon to the left of the header text, the two centred as one group.
 * Pass NULL for no icon. Not owned; the caller keeps the icon alive.
 */
void flipso_menu_view_set_header_icon(FlipsoMenuView* instance, const Icon* icon);

/**
 * Append a row.
 *
 * @param label the row text; copied, and truncated on screen if it does not fit.
 * @param icon  drawn at the left of the row, or NULL for no icon.
 * @param id    handed back to the callback; need not be the row's position.
 */
void flipso_menu_view_add_item(
    FlipsoMenuView* instance,
    const char* label,
    const Icon* icon,
    uint32_t id);

/**
 * Append a row with a short tag drawn at its right-hand end.
 *
 * The tag is what a row says about its item - "Expired", "Off card" - and it
 * is kept whole: the label is what gives way when the two do not fit, because
 * a status cut to "[expi..." is a status nobody can read.
 *
 * @param tag copied; NULL or empty for none.
 */
void flipso_menu_view_add_tagged_item(
    FlipsoMenuView* instance,
    const char* label,
    const char* tag,
    const Icon* icon,
    uint32_t id);

/** Highlight the row with this id, scrolling it into view. No-op if absent. */
void flipso_menu_view_set_selected(FlipsoMenuView* instance, uint32_t id);

#ifdef __cplusplus
}
#endif
