/**
 * @file flipso_ticket_types.h
 * @brief Resolves rail Fares Type of Ticket codes to ticket names.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest name the table may hold. Must match NAME_MAX in build_ticket_types.py. */
#define FLIPSO_TICKET_TYPE_NAME_MAX 64

/** A ticket type code's length, as the card and the table hold it. */
#define FLIPSO_TICKET_TYPE_CODE_LEN 3

typedef struct FlipsoTicketTypes FlipsoTicketTypes;

/**
 * Get ready to name ticket types.
 *
 * The table ships inside the .fap as a file asset, which the firmware unpacks
 * to /ext/apps_assets/flipso/ when the app is installed; a build of your own
 * at /ext/apps_data/flipso/ticket_types.dat is preferred when present. It is
 * searched in place, and opened only for as long as a lookup takes: only a
 * reserved journey carries a ticket type, so holding the file open for the
 * whole run would cost heap for nothing. See tools/ticket_types/FORMAT.md.
 */
FlipsoTicketTypes* flipso_ticket_types_alloc(void);
void flipso_ticket_types_free(FlipsoTicketTypes* instance);

/** Ticket types the table names; 0 when there is no usable table. */
uint32_t flipso_ticket_types_count(FlipsoTicketTypes* instance);

/**
 * Look up a Fares Type of Ticket code, "SOR", as the card holds it.
 * @return the ticket's name, "Anytime Return", or NULL when there is no table
 *         or no such code. Valid until the next call.
 */
const char* flipso_ticket_types_name(
    FlipsoTicketTypes* instance,
    const uint8_t code[FLIPSO_TICKET_TYPE_CODE_LEN]);

#ifdef __cplusplus
}
#endif
