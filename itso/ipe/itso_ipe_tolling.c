/**
 * @file itso_ipe_tolling.c
 * @brief TYP 26, the Open System Tolling Ticket (TS 1000-5 clause 2.13): a
 * bridge, tunnel or ferry crossing whose fee does not depend on distance. Its
 * value record is the voucher's (itso_ipe_voucher_value()).
 */
#include "itso_ipe_i.h"

#include <string.h>

/* The mandatory part of table 40 ends with UserDefined, bytes 13 to 19. */
#define ITSO_TOLL_MANDATORY_LEN 20
#define ITSO_TOLL_USER_DATA     13

/**
 * TS 1000-5 table 40, IPE Format Revision 1, the only one defined. Offsets are
 * from the start of the IPE data group.
 *
 * The voucher's shape with what it is for and worth taken out: no ServiceID,
 * no price and no ExpiryTime, and PassbackTime two bits earlier than any other
 * type's, after only two bits of RFU.
 */
void itso_ipe_tolling_dataset(ItsoProduct* product, const uint8_t* data, size_t len) {
    if(product->typ != ItsoTypTolling || product->format_rev != 1) return;
    if(len < ITSO_TOLL_MANDATORY_LEN) return;
    ItsoTicketTerms* t = &product->terms.ticket;

    product->passback = (uint8_t)itso_bits(data, 42, 6);
    product->has_passback = true;
    /* TYP26Flags, table 43: bits 5 and 6 are the print flags, the rest RFU. */
    product->print_defined = ITSO_PRINT_TICKET | ITSO_PRINT_RECEIPT;
    if(data[6] & 0x20) product->print_flags |= ITSO_PRINT_TICKET;
    if(data[6] & 0x40) product->print_flags |= ITSO_PRINT_RECEIPT;
    /* TYP26Class is the owner's own, not EN1545's AccommodationClassCode, so
     * it is kept apart from travel_class, which is read as Standard or First. */
    t->vehicle_class = data[7];
    t->issue_date = (ItsoDate)itso_bits(data, 66, 14);
    t->valid_from_dts = itso_bits(data, 80, 24);

    /* Bitmap bit 1, table 41: AutoRenewQuantity3, crossings added per renewal. */
    if((product->bitmap & (1 << 1)) && len > ITSO_TOLL_MANDATORY_LEN) {
        t->renew_quantity = data[ITSO_TOLL_MANDATORY_LEN];
    }
    t->valid = true;
}

bool itso_toll_user_data(const uint8_t* group, size_t len, uint8_t out[ITSO_TOLL_USER_DATA_LEN]) {
    if(len < 2 || itso_bits(group, 12, 4) != 1) return false;
    size_t dataset_len = (size_t)itso_bits(group, 0, 6) * ITSO_IPE_BLOCK_LEN;
    if(dataset_len < ITSO_TOLL_MANDATORY_LEN || dataset_len > len) return false;
    memcpy(out, group + ITSO_TOLL_USER_DATA, ITSO_TOLL_USER_DATA_LEN);
    return true;
}
