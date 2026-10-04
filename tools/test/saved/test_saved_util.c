/**
 * @file test_saved_util.c
 * @brief Helpers the saved-card tests share.
 */
#include "test_saved.h"

void write_text(const char* path, const char* text) {
    FILE* fh = fopen(path, "wb");
    fputs(text, fh);
    fclose(fh);
}

/** Empty and remove the saved-cards folder, however the last run left it. */
void clean(void) {
    DIR* dir = opendir(FLIPSO_SAVED_FOLDER);
    if(dir) {
        struct dirent* entry;
        while((entry = readdir(dir))) {
            if(entry->d_name[0] == '.') continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", FLIPSO_SAVED_FOLDER, entry->d_name);
            remove(path);
        }
        closedir(dir);
    }
    rmdir(FLIPSO_SAVED_FOLDER);
}

/** A capture holding one card's shell and directory, stamped with a time. */
FlipsoCapture* make(
    const uint8_t* shell,
    size_t shell_len,
    const uint8_t* dir,
    size_t dir_len,
    ItsoUnixTime when) {
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, shell, shell_len);
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, dir_len);
    flipso_capture_set_time(capture, when);
    return capture;
}

size_t count_files(void) {
    size_t n = 0;
    DIR* dir = opendir(FLIPSO_SAVED_FOLDER);
    if(!dir) return 0;
    struct dirent* entry;
    while((entry = readdir(dir))) {
        if(entry->d_name[0] != '.') n++;
    }
    closedir(dir);
    return n;
}

bool exists(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FLIPSO_SAVED_FOLDER, name);
    return access(path, F_OK) == 0;
}

/** The read time of the card saved as @p name, or 0 when it will not load. */
ItsoUnixTime read_time(const char* name) {
    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, name);
    FlipsoCapture* capture = flipso_capture_alloc();
    ItsoUnixTime at =
        flipso_saved_read(capture, furi_string_get_cstr(path)) ? flipso_capture_time(capture) : 0;
    flipso_capture_free(capture);
    furi_string_free(path);
    return at;
}

/** Write a card as @p name with @p suffix after its extension, as a save would. */
void leave(const char* name, const char* suffix, ItsoUnixTime read_at) {
    FlipsoCapture* card =
        make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), read_at);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.flipso%s", FLIPSO_SAVED_FOLDER, name, suffix);
    flipso_saved_write(card, path);
    flipso_capture_free(card);
}
