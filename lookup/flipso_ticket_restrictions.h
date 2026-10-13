/**
 * @file flipso_ticket_restrictions.h
 * @brief Resolves rail restriction codes to what they allow.
 */
#pragma once

#include <furi.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest text the table may hold. Must match MAX_TEXT in build_ticket_restrictions.py. */
#define FLIPSO_TICKET_RESTRICTION_TEXT_MAX 250

/** A restriction code's length, as the card and the table hold it. */
#define FLIPSO_TICKET_RESTRICTION_CODE_LEN 2

typedef struct FlipsoTicketRestrictions FlipsoTicketRestrictions;

/**
 * Get ready to look restrictions up.
 *
 * The table ships inside the .fap as a file asset, unpacked to
 * /ext/apps_assets/flipso/ when the app is installed; a build of your own at
 * /ext/apps_data/flipso/ticket_restrictions.dat is preferred when present. It
 * is searched in place and opened only for as long as a lookup takes, since
 * only a reserved journey carries a restriction code. See
 * tools/ticket_restrictions/FORMAT.md.
 */
FlipsoTicketRestrictions* flipso_ticket_restrictions_alloc(void);
void flipso_ticket_restrictions_free(FlipsoTicketRestrictions* instance);

/** Restrictions the table describes; 0 when there is no usable table. */
uint32_t flipso_ticket_restrictions_count(FlipsoTicketRestrictions* instance);

/**
 * Look up a restriction code, "OP", as the card holds it, setting what it
 * allows into strings the caller owns: its @p name, where it has one beyond
 * its code ("Chiltern Off-Peak (From 0930)"), and its @p outward and @p back
 * wording. A text the table does not have is left empty.
 *
 * Strings rather than buffers, so a lookup costs the length of the texts it
 * finds - fifty bytes for "OP" - rather than room for the longest three: it
 * runs as the reserved journey opens, Flipso's longest screen.
 *
 * @return false when there is no table or no such code, all three empty.
 */
bool flipso_ticket_restrictions_find(
    FlipsoTicketRestrictions* instance,
    const uint8_t code[FLIPSO_TICKET_RESTRICTION_CODE_LEN],
    FuriString* name,
    FuriString* outward,
    FuriString* back);

#ifdef __cplusplus
}
#endif
