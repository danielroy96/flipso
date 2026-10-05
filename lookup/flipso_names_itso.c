/**
 * @file flipso_names_itso.c
 * @brief The device's itso_ name functions for the tables in itso/names/.
 *
 * The host builds link itso/names/ itself; the .fap links this instead, which
 * answers the same calls from names.dat (flipso_names.h). Every answer the file
 * holds was produced by those functions, so these only find it:
 * tools/test/test_names.c checks the two agree for every code.
 */
#include "flipso_names.h"
#include "../itso/itso.h"
#include "../itso/itso_operators.h"

#define FLIPSO_NAMES_FUNCTION(table, function, total)            \
    const char* function(uint8_t code) {                         \
        const char* name = flipso_names_byte(table, code);       \
        return (name || !(total)) ? name : FLIPSO_NAMES_UNKNOWN; \
    }
FLIPSO_NAMES_BYTE_TABLES(FLIPSO_NAMES_FUNCTION)
#undef FLIPSO_NAMES_FUNCTION

const char* itso_railcard_name(const uint8_t* code, size_t len, bool* card) {
    uint32_t key;
    if(!itso_railcard_key(code, len, &key)) return NULL;
    uint8_t extra = 0;
    const char* name = flipso_names_keyed(FlipsoNamesRailcard, key, &extra);
    if(name && card) *card = extra != 0;
    return name;
}

const char* itso_seat_attribute_name(const char* code) {
    uint32_t key;
    if(!itso_seat_attribute_key(code, &key)) return NULL;
    return flipso_names_keyed(FlipsoNamesSeat, key, NULL);
}

const char* itso_operator_name(uint16_t oid) {
    return flipso_names_keyed(FlipsoNamesOperator, oid, NULL);
}

const char* itso_operator_brand(uint16_t oid) {
    return flipso_names_keyed(FlipsoNamesBrand, oid, NULL);
}
