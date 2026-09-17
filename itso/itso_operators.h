/**
 * @file itso_operators.h
 * @brief Names and card branding for ITSO issuer and operator identification numbers.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Name for an ITSO Issuer Identification Number.
 * @return the issuer name, or NULL if unrecognised.
 */
const char* itso_iin_name(uint32_t iin);

/**
 * Name for an ITSO Operator Identification Number.
 *
 * ITSO allocates OIDs to its licensed members and publishes the register only to
 * those members, so this table is assembled from public sources and is
 * necessarily partial. Unknown operators are reported by number.
 *
 * @return the operator name, or NULL if not in the built-in table.
 */
const char* itso_operator_name(uint16_t oid);

/**
 * Card branding issued under an ITSO Operator Identification Number.
 *
 * The scheme name printed on the card - "Freedom Pass", "SPT Subway" - which is
 * what the holder calls it and rarely what the operator is called. Only the
 * shell owner's OID brands a card; a product owner's does not, because an
 * operator's products travel on other issuers' cards.
 *
 * @return the brand, or NULL when this operator is unknown or brands no card.
 */
const char* itso_operator_brand(uint16_t oid);

#ifdef __cplusplus
}
#endif
