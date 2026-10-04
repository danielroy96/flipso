/**
 * @file test_demos.c
 * @brief The demo cards, which are files of the same kind kept apart from the user's.
 */
#include "test_saved.h"

/** Empty and remove the demo folder, which holds only files and one folder. */
static void clean_demos(void) {
    DIR* dir = opendir(FLIPSO_DEMO_FOLDER);
    if(dir) {
        struct dirent* entry;
        while((entry = readdir(dir))) {
            if(entry->d_name[0] == '.') continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", FLIPSO_DEMO_FOLDER, entry->d_name);
            if(remove(path) != 0) rmdir(path);
        }
        closedir(dir);
    }
    rmdir(FLIPSO_DEMO_FOLDER);
}

static void demo(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FLIPSO_DEMO_FOLDER, name);
    write_text(path, "Filetype: Flipso card\n");
}

void demos(void) {
    clean_demos();
    check("no folder is no demo cards", flipso_saved_demos(NULL) == 0);

    mkdir(FLIPSO_DEMO_FOLDER, 0777);
    /* Written out of order, since the SD card's order is its allocation's. */
    demo("Demo 03 Third.flipso");
    demo("Demo 01 First.flipso");
    demo("Demo 02 Second.flipso");
    demo("README.txt");
    demo("Demo 04 A name far too long for the list.flipso");
    mkdir(FLIPSO_DEMO_FOLDER "/Demo 00 Folder.flipso", 0777);

    FlipsoDemos* list = malloc(sizeof(FlipsoDemos));
    uint8_t count = flipso_saved_demos(list);
    check("only the cards are listed", count == 3 && list->count == 3);
    check(
        "in the order they are numbered",
        strcmp(list->names[0], "Demo 01 First") == 0 &&
            strcmp(list->names[1], "Demo 02 Second") == 0 &&
            strcmp(list->names[2], "Demo 03 Third") == 0);
    check("counting alone agrees", flipso_saved_demos(NULL) == 3);

    FuriString* path = furi_string_alloc();
    flipso_saved_demo_path(path, list->names[0]);
    check(
        "a demo card's path",
        strcmp(furi_string_get_cstr(path), FLIPSO_DEMO_FOLDER "/Demo 01 First.flipso") == 0);
    check("is a demo card's", flipso_saved_is_demo(furi_string_get_cstr(path)));
    flipso_saved_path(path, "Demo 01 First");
    check(
        "and a saved card of the same name is not",
        !flipso_saved_is_demo(furi_string_get_cstr(path)));
    check(
        "nor is a folder whose name only starts the same",
        !flipso_saved_is_demo(FLIPSO_DEMO_FOLDER "s/x.flipso"));
    check("nor is a card just read", !flipso_saved_is_demo(""));
    furi_string_free(path);

    /* More than a menu holds: the first in order are kept, whatever order the
     * folder gives them in. */
    clean_demos();
    mkdir(FLIPSO_DEMO_FOLDER, 0777);
    for(int i = FLIPSO_DEMO_MAX + 4; i >= 0; i--) {
        char name[32];
        snprintf(name, sizeof(name), "Demo %02d.flipso", i);
        demo(name);
    }
    count = flipso_saved_demos(list);
    bool ordered = count == FLIPSO_DEMO_MAX;
    for(uint8_t i = 0; ordered && i < count; i++) {
        char want[32];
        snprintf(want, sizeof(want), "Demo %02u", i);
        ordered = strcmp(list->names[i], want) == 0;
    }
    check("a full list keeps the first in order", ordered);
    check("and counting stops where the list does", flipso_saved_demos(NULL) == FLIPSO_DEMO_MAX);

    free(list);
    clean_demos();
}
