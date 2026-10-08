/**
 * @file test_about_media.c
 * @brief The About screen, and the screen for a DESFire Flipso cannot decode.
 */
#include "test_format.h"

/** About, with every table present and with none. */
void about_screens(FuriString* text) {
    furi_string_reset(text);
    flipso_format_about(text, "1.0", 4009, 400000, 3467, 3);
    house_style("about", text);
    check(
        "about is a page to each thing it says",
        titles_are(
            text, "Flipso|Station names|Ticket types|Bus stop names|Operator names|Saved cards"));
    check(
        "it counts the ticket types",
        on_page(text, "Ticket types", "Installed: 3467 rail ticket types\n"));
    furi_string_reset(text);
    flipso_format_about(text, NULL, 0, 0, 0, 0);
    house_style("about, nothing installed", text);
}

/** The card details screen for a DESFire Flipso cannot decode, which is held
 *  to the same style as the ITSO screens. */
void media_screens(FuriString* text) {
    static const uint8_t chip[FLIPSO_MEDIA_CHIP_LEN] = {
        0x04, 0x01, 0x01, 0x01, 0x00, 0x16, 0x05, 0x04, 0x01, 0x01, 0x01,
        0x03, 0x16, 0x05, 0x04, 0x8B, 0x1F, 0xF1, 0xAD, 0x26, 0x80, 0xBA,
        0x34, 0xCD, 0x56, 0xEF, 0x42, 0x08, 0xE0, 0x04, 0x00,
    };
    static FlipsoMedia media;
    flipso_media_reset(&media);
    furi_string_reset(text);
    flipso_format_media(text, &media);
    house_style("card details, undescribed", text);
    flipso_media_parse_chip(&media, chip, sizeof(chip));
    flipso_media_add_app(&media, FLIPSO_AID_OYSTER);
    flipso_media_add_app(&media, 0xABCDEFu);
    FlipsoMediaApp* app = flipso_media_open_app(&media, FLIPSO_AID_OYSTER);
    app->file_count = 3;
    app->files[0] = (FlipsoMediaFile){.id = 0, .settings_valid = true, .access = 0xEEEE};
    app->files[0].data.size = 8;
    app->files[0].data_len = 8;
    memcpy(app->data, "\xDE\xAD\xBE\xEF\x01\x02\x03\x04", 8);
    app->data_len = 8;
    app->files[1] = (FlipsoMediaFile){
        .id = 1, .settings_valid = true, .type = FLIPSO_FILE_VALUE, .access = 0x1111};
    app->files[2] = (FlipsoMediaFile){.id = 2};
    furi_string_reset(text);
    flipso_format_media(text, &media);
    printf("\n%s\n", furi_string_get_cstr(text));
    house_style("card details", text);
    check("the details open on the chip", shows(text, "Chip: MIFARE DESFire EV1"));
    check(
        "contents wrap between groups of bytes, a page to each file",
        on_page(text, "File 0", "Contents: DEADBEEF 01020304\n"));
    check(
        "the files' application is named with the others",
        on_page(text, "Applications", "Files read from: Oyster\n"));
    check(
        "a file the card would not describe says so",
        page_starts(text, "File 2", "Details: Locked\n"));
    check("a value file has its range", page_starts(text, "File 1", "Type: Value\nRange: "));
    flipso_media_reset(&media);
}
