/**
 * @file test_format_fakes.c
 * @brief The lookups the screens name things with, faked, and the synthetic card.
 */
#include "test_format.h"

/* ---- lookups: the operator table is real, the SD card tables are faked --- */

const char* flipso_operators_name(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_name(oid);
}

const char* flipso_operators_brand(const FlipsoOperators* instance, uint16_t oid) {
    (void)instance;
    return itso_operator_brand(oid);
}

const char* flipso_stations_name(FlipsoStations* instance, const char* nlc) {
    (void)instance;
    if(strcmp(nlc, "1072") == 0) return "London Waterloo";
    if(strcmp(nlc, "5148") == 0) return "London Bridge";
    return NULL;
}

const char* flipso_ticket_types_name(
    FlipsoTicketTypes* instance,
    const uint8_t code[FLIPSO_TICKET_TYPE_CODE_LEN]) {
    (void)instance;
    if(memcmp(code, "SOR", FLIPSO_TICKET_TYPE_CODE_LEN) == 0) return "Anytime Return";
    return NULL;
}

const char* flipso_naptan_stop(FlipsoNaptan* instance, const char* digits) {
    (void)instance;
    if(strcmp(digits, "00062624") == 0) return "High Street";
    return NULL;
}

const char* flipso_naptan_atco(FlipsoNaptan* instance, const char* atco) {
    (void)instance;
    (void)atco;
    return NULL;
}

/* ---- the synthetic card, assembled as the capture tests do it ----------- */

static uint8_t group1[64 + sizeof(card_sector9)];
static uint8_t group3[64 + sizeof(card_sector11)];
static uint8_t group4[sizeof(card_sector4) + sizeof(card_sector10)];
static uint8_t group5[64 + sizeof(card_sector12)];

FlipsoCapture* synthetic_capture(void) {
    memcpy(group1, card_sector1, sizeof(card_sector1));
    memcpy(group1 + 64, card_sector9, sizeof(card_sector9));
    memcpy(group3, card_sector3, sizeof(card_sector3));
    memcpy(group3 + 64, card_sector11, sizeof(card_sector11));
    memcpy(group4, card_sector4, sizeof(card_sector4));
    memcpy(group4 + sizeof(card_sector4), card_sector10, sizeof(card_sector10));
    memcpy(group5, card_sector5, sizeof(card_sector5));
    memcpy(group5 + 64, card_sector12, sizeof(card_sector12));

    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(capture, FlipsoBlockProduct, 1, group1, sizeof(group1));
    flipso_capture_add(capture, FlipsoBlockProduct, 2, card_sector2, sizeof(card_sector2));
    flipso_capture_add(capture, FlipsoBlockProduct, 3, group3, sizeof(group3));
    flipso_capture_add(capture, FlipsoBlockProduct, 4, group4, sizeof(group4));
    flipso_capture_add(capture, FlipsoBlockProduct, 5, group5, sizeof(group5));
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);
    return capture;
}

/* ---- house style --------------------------------------------------------- */

/** Load a saved card file into @p capture. */
bool load(FlipsoCapture* capture, const char* path) {
    FILE* file = fopen(path, "r");
    if(!file) return false;
    static char line[FLIPSO_CAPTURE_LINE_MAX + 2];
    bool ok = true;
    while(ok && fgets(line, sizeof(line), file)) {
        ok = flipso_capture_parse_line(capture, line);
    }
    fclose(file);
    return ok && flipso_capture_valid(capture);
}
