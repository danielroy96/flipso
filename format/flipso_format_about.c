/**
 * @file flipso_format_about.c
 * @brief The About screen: what Flipso is, and what it has to work with.
 */
#include "flipso_format_i.h"

void flipso_format_about(
    FuriString* out,
    const char* version,
    uint32_t stations,
    uint32_t stops,
    uint32_t tickets,
    uint16_t operators) {
    flipso_cat_page(out, FlipsoIconInfo, "Flipso");
    if(version) furi_string_cat_printf(out, "Version: %s\n", version);
    furi_string_cat(
        out,
        "Reads UK ITSO travel smartcards, such as bus passes, rail smartcards "
        "and concessionary passes.\n");

    flipso_cat_page(out, FlipsoIconTrain, "Station names");
    if(stations) {
        furi_string_cat_printf(out, "Installed: %lu railway locations\n", (unsigned long)stations);
    } else {
        furi_string_cat(out, "Installed: No\nReinstall Flipso to restore them.\n");
    }

    flipso_cat_page(out, FlipsoIconTicket, "Ticket types");
    if(tickets) {
        furi_string_cat_printf(out, "Installed: %lu rail ticket types\n", (unsigned long)tickets);
    } else {
        furi_string_cat(out, "Installed: No\nReinstall Flipso to restore them.\n");
    }

    flipso_cat_page(out, FlipsoIconBus, "Bus stop names");
    if(stops) {
        furi_string_cat_printf(out, "Installed: %lu stops\n", (unsigned long)stops);
    } else {
        furi_string_cat(
            out,
            "Installed: No\n"
            "Bus stops show as numbers until naptan.dat is copied to "
            "apps_data/flipso on the SD card. It comes with Flipso's source, in "
            "its data folder.\n");
    }

    flipso_cat_page(out, FlipsoIconOperator, "Operator names");
    if(operators) {
        furi_string_cat_printf(out, "Your operators file: %u names\n", operators);
    } else {
        furi_string_cat(
            out,
            "Your operators file: None\n"
            /* The file and its folder said apart: the whole path is one word
             * wider than the screen, which the panel breaks mid-name. */
            "Add names to operators.txt in apps_data/flipso on the SD card.\n");
    }

    flipso_cat_page(out, FlipsoIconSave, "Saved cards");
    furi_string_cat(out, "Folder: apps_data/flipso/cards\n");
    furi_string_cat(
        out, "Saved cards can contain personal information. Take care sharing them.\n");
}
