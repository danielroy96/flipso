/**
 * @file itso_ipe_i.h
 * @brief Internals shared between the IPE decoders: the table of types, and the
 * entry points each type's file provides to it.
 */
#pragma once

#include "../itso_i.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One IPE type's decoders. Either may be NULL: some types keep everything in
 * their value record, and the identity types have no value record tail.
 */
typedef struct {
    uint8_t typ;
    ItsoFamily family; /**< Which ItsoTerms the decoders below fill. */
    /**
     * Decode the type's dataset. @p product already holds its directory entry,
     * the elements every type shares, and any value record - a dataset's amounts
     * take their currency from it.
     *
     * @param data the IPE Data Group from its header; @p len bytes of dataset.
     */
    void (*dataset)(ItsoProduct* product, const uint8_t* data, size_t len);
    /**
     * Decode what the tail of the live value record says about the product as
     * it stands, beyond the balance or counter every record's history keeps.
     *
     * @param newest the live record, ITSO_VALUE_RECORD_LEN bytes.
     */
    void (*value)(ItsoProduct* product, const uint8_t* newest);
} ItsoIpeType;

/** The decoders for IPE type @p typ, or NULL for a type the table does not list. */
const ItsoIpeType* itso_ipe_type(uint8_t typ);

/* itso_ipe_purse.c */
void itso_ipe_purse_dataset(ItsoProduct* product, const uint8_t* data, size_t len);
void itso_ipe_stored_travel_rights_value(ItsoProduct* product, const uint8_t* newest);
void itso_ipe_charge_to_account1_value(ItsoProduct* product, const uint8_t* newest);
void itso_ipe_charge_to_account2_value(ItsoProduct* product, const uint8_t* newest);

/* itso_ipe_loyalty.c */
void itso_ipe_loyalty_value(ItsoProduct* product, const uint8_t* newest);

/* itso_ipe_id.c */
void itso_ipe_id_dataset(ItsoProduct* product, const uint8_t* data, size_t len);

/* itso_ipe_period.c */
void itso_ipe_period_dataset(ItsoProduct* product, const uint8_t* data, size_t len);
void itso_ipe_period_value(ItsoProduct* product, const uint8_t* newest);

/* itso_ipe_journey.c */
void itso_ipe_journey_dataset(ItsoProduct* product, const uint8_t* data, size_t len);
void itso_ipe_journey_value(ItsoProduct* product, const uint8_t* newest);

/* itso_ipe_reservation.c */
void itso_ipe_reservation_dataset(ItsoProduct* product, const uint8_t* data, size_t len);
void itso_ipe_reservation_value(ItsoProduct* product, const uint8_t* newest);

/* itso_ipe_voucher.c */
void itso_ipe_voucher_value(ItsoProduct* product, const uint8_t* newest);

/**
 * A ticket's RouteCode, five bytes at @p pos, into @p t (TYP 22 and 23).
 *
 * @return the offset after it, or 0 when it does not fit.
 */
size_t itso_parse_route(ItsoTicketTerms* t, const uint8_t* data, size_t len, size_t pos);

/**
 * A ticket's two LOC1 locations, into @c from and @c to (TYP 22 and 23).
 *
 * @return the offset after the second, or 0 when either does not fit.
 */
size_t itso_parse_journey_ends(ItsoProduct* product, const uint8_t* data, size_t len, size_t pos);

/**
 * Decode the Value Record Data Group bound to an IPE (TS 1000-2 clause 7) into
 * @p product: every record into its history, and the live one's tail through
 * the type table.
 */
void itso_parse_value_records(
    ItsoProduct* product,
    const uint8_t* group,
    size_t len,
    uint8_t sector_size);

/**
 * Where the Value Group Extension a value group carries starts within @p group,
 * or 0 when it has none or its header is cut off.
 *
 * @param records_offset where the value records start, as itso_value_records()
 *                       reported it.
 */
size_t itso_vgx_offset(const uint8_t* group, size_t len, size_t records_offset);

/** The VGXRef of the Value Group Extension a value group carries, or 0. */
uint8_t itso_vgx_ref(const uint8_t* group, size_t len, size_t records_offset);

#ifdef __cplusplus
}
#endif
