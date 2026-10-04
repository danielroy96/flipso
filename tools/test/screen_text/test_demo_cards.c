/**
 * @file test_demo_cards.c
 * @brief Every screen of every demo card, now and once expired.
 */
#include "test_format.h"

#include <dirent.h>

/** Load each demo card in @p directory and hold every screen of it to the
 *  house style, with the checks particular cards have of their own. */
void demo_cards(const char* directory, FlipsoFormat f) {
    DIR* dir = opendir(directory);
    check("the demo cards are there", dir != NULL);
    struct dirent* entry;
    int cards = 0, chips = 0;
    while(dir && (entry = readdir(dir))) {
        const char* ext = strrchr(entry->d_name, '.');
        if(!ext || strcmp(ext, ".flipso") != 0) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
        FlipsoCapture* demo = flipso_capture_alloc();
        static ItsoCard demo_card;
        char what[600];
        snprintf(what, sizeof(what), "%s loads", entry->d_name);
        check(what, load(demo, path) && flipso_capture_decode(demo, &demo_card));
        f.capture = demo;
        /* What the saved-card scene does with a card's chip block. */
        static FlipsoMedia demo_media;
        flipso_media_reset(&demo_media);
        size_t chip_len = 0;
        const uint8_t* chip = flipso_capture_chip(demo, &chip_len);
        if(chip) {
            flipso_media_parse_chip(&demo_media, chip, chip_len);
            FuriString* screen = furi_string_alloc();
            FlipsoFormat with_chip = f;
            with_chip.media = &demo_media;
            flipso_format_card(screen, &with_chip, &demo_card, "A name", false, 0);
            snprintf(what, sizeof(what), "%s shows its saved chip", entry->d_name);
            check(what, shows(screen, "Chip: MIFARE DESFire EV1\n"));
            furi_string_free(screen);
            chips++;
        }
        if(strncmp(entry->d_name, "Demo 01", 7) == 0) {
            /* Opened from the About menu rather than from Saved cards. */
            FuriString* screen = furi_string_alloc();
            flipso_format_card(screen, &f, &demo_card, "Demo 01", true, 0);
            check(
                "a demo card says it is one",
                shows(screen, "Demo card\nName: Demo 01\n") && !shows(screen, "Saved card"));
            furi_string_free(screen);
        }
        f.media = &demo_media;
        every_screen(entry->d_name, &f, &demo_card);
        if(strncmp(entry->d_name, "Demo 01", 7) == 0) demo_one(&f, &demo_card);
        if(strncmp(entry->d_name, "Demo 04", 7) == 0) demo_four(&f, &demo_card);
        if(strncmp(entry->d_name, "Demo 14", 7) == 0) demo_fourteen(&f, &demo_card);
        if(strncmp(entry->d_name, "Demo 08", 7) == 0) demo_type2_full(&f, &demo_card, true);
        if(strncmp(entry->d_name, "Demo 09", 7) == 0) demo_type2_full(&f, &demo_card, false);
        if(strncmp(entry->d_name, "Demo 07", 7) == 0) {
            /* Judged on the day after it was read, when both tickets ran. */
            FlipsoFormat read_day = f;
            read_day.now = 1790035200u; /* 2026-09-22 */
            demo_seven(&read_day, &demo_card);
        }
        f.media = NULL;
        FlipsoFormat expired = f;
        expired.now = FLIPSO_TEST_LATER;
        snprintf(what, sizeof(what), "%s, expired", entry->d_name);
        every_screen(what, &expired, &demo_card);
        flipso_capture_free(demo);
        cards++;
    }
    if(dir) closedir(dir);
    check("all fourteen demo cards were rendered", cards == 14);
    check("and a saved chip block was among them", chips > 0);
}
