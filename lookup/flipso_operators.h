/**
 * @file flipso_operators.h
 * @brief Operator lookup: user-supplied names and branding layered over the built-in table.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipsoOperators FlipsoOperators;

/**
 * Load the user's operator names, if they have supplied any.
 *
 * Reads `/ext/apps_data/flipso/operators.txt`, one `<oid>,<name>[,<brand>]` per
 * line with `#` comments. Missing or unreadable files are not an error: the
 * built-in table is used on its own.
 */
FlipsoOperators* flipso_operators_alloc(void);
void flipso_operators_free(FlipsoOperators* instance);

/** Entries read from the user's operators file; 0 when there is none. */
uint16_t flipso_operators_user_count(const FlipsoOperators* instance);

/**
 * Resolve an OID to a name, preferring the user's file over the built-in table.
 * @return the name, or NULL when the operator is unknown.
 */
const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid);

/**
 * Resolve an OID to the branding of the card it issues, preferring the user's
 * file over the built-in table. Only meaningful for a shell owner's OID.
 * @return the brand, or NULL when this operator brands no card we know of.
 */
const char* flipso_operators_brand(const FlipsoOperators* instance, uint16_t oid);

#ifdef __cplusplus
}
#endif
