/*
 * Host-side test for saved cards on disk: the naming, the file, and what
 * happens to each when something is wrong.
 *
 * The capture test next door covers the blocks and the format. This covers the
 * layer around them, against real files in a real directory, so that a write
 * that half-succeeds or a read of somebody else's file is a test failure here
 * rather than a puzzle on the device.
 */
#include "flipso_saved.h"
#include "card_data.h"

#include <dialogs/dialogs.h>
#include <gui/canvas.h>
#include <toolbox/path.h>
#include <toolbox/stream/file_stream.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- storage stubs ------------------------------------------------------ */

struct File {
    FILE* handle;
    DIR* dir;
};

struct Stream {
    FILE* handle;
};

const Icon I_card_10px = {0};

void* furi_record_open(const char* name) {
    (void)name;
    return NULL;
}

void furi_record_close(const char* name) {
    (void)name;
}

File* storage_file_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(File));
}

void storage_file_free(File* file) {
    free(file);
}

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    (void)mode;
    file->handle = fopen(path, access == FSAM_WRITE ? "wb" : "rb");
    return file->handle != NULL;
}

void storage_file_close(File* file) {
    if(file->handle) fclose(file->handle);
    file->handle = NULL;
}

/* Bytes a write may still put down before the "SD card" is full; negative for
 * a card with room. How a test makes a save fail part way through. */
static long stub_write_budget = -1;

uint16_t storage_file_write(File* file, const void* buffer, uint16_t size) {
    if(!file->handle) return 0;
    if(stub_write_budget >= 0) {
        if((long)size > stub_write_budget) size = (uint16_t)stub_write_budget;
        stub_write_budget -= size;
    }
    return (uint16_t)fwrite(buffer, 1, size, file->handle);
}

uint64_t storage_file_size(File* file) {
    (void)file;
    return 0;
}

bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    (void)file;
    (void)offset;
    (void)from_start;
    return false;
}

uint16_t storage_file_read(File* file, void* buffer, uint16_t size) {
    if(!file->handle) return 0;
    return (uint16_t)fread(buffer, 1, size, file->handle);
}

bool storage_dir_open(File* file, const char* path) {
    file->dir = opendir(path);
    return file->dir != NULL;
}

bool storage_dir_read(File* file, FileInfo* fileinfo, char* name, uint16_t name_length) {
    if(!file->dir) return false;
    struct dirent* entry;
    while((entry = readdir(file->dir))) {
        if(entry->d_name[0] == '.') continue;
        /* The firmware ends the walk rather than truncating, which is what the
         * caller's buffer size is chosen against. */
        if(strlen(entry->d_name) >= name_length) return false;
        strcpy(name, entry->d_name);
        fileinfo->flags = (entry->d_type == DT_DIR) ? FSF_DIRECTORY : 0;
        fileinfo->size = 0;
        return true;
    }
    return false;
}

bool storage_dir_close(File* file) {
    if(file->dir) closedir(file->dir);
    file->dir = NULL;
    return true;
}

bool storage_simply_mkdir(Storage* storage, const char* path) {
    (void)storage;
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}

bool storage_simply_remove(Storage* storage, const char* path) {
    (void)storage;
    return remove(path) == 0;
}

bool storage_common_exists(Storage* storage, const char* path) {
    (void)storage;
    return access(path, F_OK) == 0;
}

/* Set to make every rename fail, as an SD card error would. */
static bool stub_rename_fails = false;

FS_Error storage_common_rename(Storage* storage, const char* old_path, const char* new_path) {
    (void)storage;
    if(stub_rename_fails) return FSE_INTERNAL;
    /* Firmware 1.4 replaces a destination that exists, as rename(2) does, so
     * the stub does too: refusing a taken name is flipso_saved_rename()'s job,
     * and a stub that did it for it would hide the check going missing. */
    return rename(old_path, new_path) == 0 ? FSE_OK : FSE_NOT_EXIST;
}

Stream* file_stream_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(struct Stream));
}

bool file_stream_open(Stream* stream, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    (void)access;
    (void)mode;
    stream->handle = fopen(path, "rb");
    return stream->handle != NULL;
}

void file_stream_close(Stream* stream) {
    if(stream->handle) fclose(stream->handle);
    stream->handle = NULL;
}

void stream_free(Stream* stream) {
    free(stream);
}

/* The firmware hands the caller the line terminator along with the line. */
bool stream_read_line(Stream* stream, FuriString* line) {
    /* Longer than the longest line a card produces, so a real saved card is
     * read in one piece the way the device reads it. */
    static char buffer[FLIPSO_CAPTURE_LINE_MAX + 2];
    if(!fgets(buffer, sizeof(buffer), stream->handle)) return false;
    furi_string_set_str(line, buffer);
    return true;
}

void path_extract_filename_no_ext(const char* path, FuriString* filename) {
    const char* slash = strrchr(path, '/');
    const char* base = slash ? slash + 1 : path;
    const char* dot = strrchr(base, '.');
    furi_string_reset(filename);
    for(const char* c = base; *c && c != dot; c++) {
        furi_string_push_back(filename, *c);
    }
}

/* ---- dialogs stubs ------------------------------------------------------ */

static char last_alert[128];

void dialog_file_browser_set_basic_options(
    DialogsFileBrowserOptions* options,
    const char* extension,
    const Icon* icon) {
    memset(options, 0, sizeof(*options));
    options->extension = extension;
    options->icon = icon;
}

bool dialog_file_browser_show(
    DialogsApp* context,
    FuriString* result_path,
    FuriString* path,
    const DialogsFileBrowserOptions* options) {
    (void)context;
    (void)result_path;
    (void)path;
    (void)options;
    return false; /* Nobody is here to choose one. */
}

struct DialogMessage {
    char header[64];
    char text[64];
};

DialogMessage* dialog_message_alloc(void) {
    return calloc(1, sizeof(DialogMessage));
}

void dialog_message_free(DialogMessage* message) {
    free(message);
}

void dialog_message_set_header(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical) {
    (void)x;
    (void)y;
    (void)horizontal;
    (void)vertical;
    snprintf(message->header, sizeof(message->header), "%s", text);
}

void dialog_message_set_text(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical) {
    (void)x;
    (void)y;
    (void)horizontal;
    (void)vertical;
    snprintf(message->text, sizeof(message->text), "%s", text);
}

void dialog_message_set_buttons(
    DialogMessage* message,
    const char* left,
    const char* center,
    const char* right) {
    (void)message;
    (void)left;
    (void)center;
    (void)right;
}

int dialog_message_show(DialogsApp* context, const DialogMessage* message) {
    (void)context;
    snprintf(last_alert, sizeof(last_alert), "%s", message->header);
    return 0;
}

/* ---- test --------------------------------------------------------------- */

static int failures = 0;

static void check(const char* what, int ok) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if(!ok) failures++;
}

static void write_text(const char* path, const char* text) {
    FILE* fh = fopen(path, "wb");
    fputs(text, fh);
    fclose(fh);
}

/** Empty and remove the saved-cards folder, however the last run left it. */
static void clean(void) {
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

static void names(void) {
    ItsoCard card;
    itso_card_reset(&card);
    itso_parse_shell(&card, card_shell, sizeof(card_shell));

    char name[FLIPSO_SAVED_NAME_LEN];
    flipso_saved_suggest_name(name, sizeof(name), &card, "Freedom Pass");
    check("a brand and the last four digits", strcmp(name, "Freedom Pass 3458") == 0);

    flipso_saved_suggest_name(name, sizeof(name), &card, NULL);
    check("an unbranded card still gets a name", strcmp(name, "ITSO Card 3458") == 0);

    /* Characters a file name cannot carry are dropped, and the digits are kept
     * whatever the brand does: they are what tells two of these apart. */
    flipso_saved_suggest_name(name, sizeof(name), &card, "Greater London (Freedom Pass)");
    check("a long brand keeps the digits", strcmp(name, "Greater London Freedom 3458") == 0);
    check("and still fits the field", strlen(name) < sizeof(name));

    flipso_saved_suggest_name(name, sizeof(name), &card, "?*:/");
    check("a brand of nothing usable is just the digits", strcmp(name, "3458") == 0);

    /* A card whose shell never read has no number to append. */
    ItsoCard blank;
    itso_card_reset(&blank);
    flipso_saved_suggest_name(name, sizeof(name), &blank, NULL);
    check("a card with no number is named anyway", strcmp(name, "ITSO Card") == 0);

    char tiny[4];
    flipso_saved_suggest_name(tiny, sizeof(tiny), &card, "Freedom Pass");
    check("a buffer too small to say much is not overrun", strlen(tiny) < sizeof(tiny));

    FuriString* shown = furi_string_alloc();
    flipso_saved_name(shown, "/ext/apps_data/flipso/cards/Mum's pass.flipso");
    check(
        "a saved card is shown by its file name",
        strcmp(furi_string_get_cstr(shown), "Mum's pass") == 0);
    furi_string_free(shown);
}

static void round_trip(void) {
    clean();

    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, card_shell, sizeof(card_shell));
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, card_dir, sizeof(card_dir));
    flipso_capture_add(capture, FlipsoBlockLog, 0, card_log, sizeof(card_log));
    flipso_capture_set_time(capture, 1758400000u);

    check("nothing is saved yet", !flipso_saved_any());

    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, "Test Card");
    check(
        "the path is the folder, the name and the extension",
        strcmp(furi_string_get_cstr(path), FLIPSO_SAVED_FOLDER "/Test Card.flipso") == 0);

    /* The folder does not exist yet: writing has to make it. */
    check("writing creates the folder", flipso_saved_write(capture, furi_string_get_cstr(path)));
    check("and there is now a saved card", flipso_saved_any());

    FlipsoCapture* loaded = flipso_capture_alloc();
    check("it reads back", flipso_saved_read(loaded, furi_string_get_cstr(path)));
    check("with its read time", flipso_capture_time(loaded) == 1758400000u);

    ItsoCard from_file, from_memory;
    check("and decodes", flipso_capture_decode(loaded, &from_file));
    flipso_capture_decode(capture, &from_memory);
    check(
        "to the card that was saved",
        memcmp(&from_file, &from_memory, sizeof(ItsoCard)) == 0);

    check("deleting it works", flipso_saved_delete(furi_string_get_cstr(path)));
    check("and it is gone", !flipso_saved_any());
    check("deleting it twice does not", !flipso_saved_delete(furi_string_get_cstr(path)));

    furi_string_free(path);
    flipso_capture_free(loaded);
    flipso_capture_free(capture);
}

static void bad_files(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* capture = flipso_capture_alloc();

    check(
        "a card that is not there does not read",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/missing.flipso"));

    write_text(FLIPSO_SAVED_FOLDER "/other.flipso", "Filetype: Flipper NFC device\nVersion: 4\n");
    check(
        "somebody else's file is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/other.flipso"));
    check("and leaves nothing behind", !flipso_capture_valid(capture));

    write_text(
        FLIPSO_SAVED_FOLDER "/future.flipso", "Filetype: Flipso card\nVersion: 99\nShell: 18 11\n");
    check(
        "a card from a later Flipso is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/future.flipso"));

    write_text(
        FLIPSO_SAVED_FOLDER "/empty.flipso",
        "Filetype: Flipso card\nVersion: 1\nRead at: 1\n");
    check(
        "a card with no shell in it is refused",
        !flipso_saved_read(capture, FLIPSO_SAVED_FOLDER "/empty.flipso"));

    /* Writing somewhere that cannot be written must not leave a file that later
     * looks like a card. */
    check(
        "a write that cannot open its file fails",
        !flipso_saved_write(capture, FLIPSO_SAVED_FOLDER "/nope/deep.flipso"));

    flipso_capture_free(capture);

    /* The browser has nobody in front of it, so it says no; the alert is the
     * only other thing that reaches a person, and it must not crash. */
    FuriString* picked = furi_string_alloc();
    check("a browser nobody answers picks nothing", !flipso_saved_pick(picked, NULL));
    furi_string_free(picked);

    flipso_saved_alert("Cannot save card", "Check the SD card.");
    check("the alert says what went wrong", strcmp(last_alert, "Cannot save card") == 0);

    clean();
}

/** A capture holding one card's shell and directory, stamped with a time. */
static FlipsoCapture* make(
    const uint8_t* shell,
    size_t shell_len,
    const uint8_t* dir,
    size_t dir_len,
    uint32_t when) {
    FlipsoCapture* capture = flipso_capture_alloc();
    flipso_capture_add(capture, FlipsoBlockShell, 0, shell, shell_len);
    flipso_capture_add(capture, FlipsoBlockDirectory, 0, dir, dir_len);
    flipso_capture_set_time(capture, when);
    return capture;
}

static size_t count_files(void) {
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

/*
 * The same card read twice should update the record it already has rather than
 * leaving two of them: what changes between reads is the products on the card,
 * which is the thing worth keeping current.
 */
static void finding(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* mine = make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), 1000);
    FlipsoCapture* other =
        make(cmd2_shell, sizeof(cmd2_shell), cmd2_dir, sizeof(cmd2_dir), 2000);

    char a[ITSO_ISRN_DIGITS + 1], b[ITSO_ISRN_DIGITS + 1];
    flipso_capture_card_number(mine, a);
    flipso_capture_card_number(other, b);
    check("the two test cards are different cards", strcmp(a, b) != 0);

    FuriString* found = furi_string_alloc();
    uint32_t read_at = 0;
    check("an empty folder holds no record", !flipso_saved_find(mine, found, &read_at));

    /* Somebody else's card, and a file that is not a card at all, both under
     * names that say nothing about which card is in them. */
    FuriString* path = furi_string_alloc();
    flipso_saved_path(path, "A card");
    flipso_saved_write(other, furi_string_get_cstr(path));
    write_text(FLIPSO_SAVED_FOLDER "/notes.txt", "not a card at all\n");
    write_text(FLIPSO_SAVED_FOLDER "/junk.flipso", "Filetype: Flipper NFC device\n");
    check("a different card is not a match", !flipso_saved_find(mine, found, &read_at));

    flipso_saved_path(path, "Another card");
    check("saving it writes a second file", flipso_saved_write(mine, furi_string_get_cstr(path)));

    check("now its record is found", flipso_saved_find(mine, found, &read_at));
    check(
        "under the name it was given, not the card's",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/Another card.flipso") == 0);
    check("with the time that record was read", read_at == 1000);

    /* And the other card still finds its own, so the walk is matching rather
     * than returning whatever it reached first. */
    check("the other card finds its own record", flipso_saved_find(other, found, &read_at));
    check(
        "which is the other file",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/A card.flipso") == 0);
    check("with its own read time", read_at == 2000);

    /* The same card read again: written over its own record, under the name it
     * already had, leaving the file count where it was. */
    size_t before = count_files();
    flipso_capture_set_time(mine, 3000);
    flipso_saved_find(mine, found, NULL);
    check("updating writes over it", flipso_saved_write(mine, furi_string_get_cstr(found)));
    check("and adds no file", count_files() == before);
    flipso_saved_find(mine, found, &read_at);
    check("the record now carries the newer read", read_at == 3000);

    /* An update that runs out of room part way must leave the record it was
     * replacing alone: that file is the only copy of what has rolled off the
     * card since, and losing it to a full SD card loses it for good. */
    flipso_capture_set_time(mine, 4000);
    stub_write_budget = 100;
    check("an update that runs out of room fails",
          !flipso_saved_write(mine, furi_string_get_cstr(found)));
    stub_write_budget = -1;
    flipso_saved_find(mine, found, &read_at);
    check("and the record it was replacing is untouched", read_at == 3000);
    FlipsoCapture* reread = flipso_capture_alloc();
    check("and still loads", flipso_saved_read(reread, furi_string_get_cstr(found)));
    flipso_capture_free(reread);
    check("and nothing is left beside it", count_files() == before);
    {
        FuriString* temp = furi_string_alloc();
        furi_string_printf(temp, "%s.tmp", furi_string_get_cstr(found));
        check("not even the half-written file", access(furi_string_get_cstr(temp), F_OK) != 0);
        furi_string_free(temp);
    }

    /* The same for a write that completes and then cannot be moved into place:
     * the rename is the step that replaces the record, and when it fails the
     * record must still be there - not removed to make way for a second try. */
    stub_rename_fails = true;
    check("an update whose rename fails fails",
          !flipso_saved_write(mine, furi_string_get_cstr(found)));
    stub_rename_fails = false;
    flipso_saved_find(mine, found, &read_at);
    check("and the record it was replacing survives it", read_at == 3000);
    check("with nothing left beside it", count_files() == before);

    /* A capture with nothing in it cannot match anything, and must not be
     * answered with somebody else's file. */
    FlipsoCapture* empty = flipso_capture_alloc();
    check("an empty capture matches nothing", !flipso_saved_find(empty, found, &read_at));
    flipso_capture_free(empty);

    furi_string_free(path);
    furi_string_free(found);
    flipso_capture_free(other);
    flipso_capture_free(mine);
    clean();
}

static void renaming(void) {
    clean();
    flipso_saved_mkdir();

    FlipsoCapture* card = make(card_shell, sizeof(card_shell), card_dir, sizeof(card_dir), 1000);
    FuriString* from = furi_string_alloc();
    FuriString* to = furi_string_alloc();

    flipso_saved_path(from, "Before");
    flipso_saved_write(card, furi_string_get_cstr(from));
    flipso_saved_path(to, "After");

    check(
        "a card can be renamed",
        flipso_saved_rename(furi_string_get_cstr(from), furi_string_get_cstr(to)));
    check("the old name has gone", access(furi_string_get_cstr(from), F_OK) != 0);
    check("and the new one is there", access(furi_string_get_cstr(to), F_OK) == 0);

    /* The card is the same card, so it is still found by its number, now under
     * the new name. */
    FuriString* found = furi_string_alloc();
    check("it is still its own record", flipso_saved_find(card, found, NULL));
    check(
        "under the new name",
        strcmp(furi_string_get_cstr(found), FLIPSO_SAVED_FOLDER "/After.flipso") == 0);

    check(
        "renaming to the name it has is allowed",
        flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(to)));
    check("and leaves it there", access(furi_string_get_cstr(to), F_OK) == 0);

    /* Onto a name another card holds: refused, and neither file is touched. */
    FlipsoCapture* other =
        make(cmd2_shell, sizeof(cmd2_shell), cmd2_dir, sizeof(cmd2_dir), 2000);
    flipso_saved_path(from, "Occupied");
    flipso_saved_write(other, furi_string_get_cstr(from));
    check(
        "renaming onto a name in use is refused",
        !flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(from)));
    check("the card stays where it was", access(furi_string_get_cstr(to), F_OK) == 0);
    check("and the card in the way is untouched", access(furi_string_get_cstr(from), F_OK) == 0);
    {
        /* Only the case of the name changing. The SD card, like this host's
         * disk, ignores case, so this is a rename onto a name that "exists" -
         * the card's own - and must neither be refused nor lose the card. */
        FuriString* lower = furi_string_alloc();
        flipso_saved_path(lower, "after");
        check(
            "changing only the case of a name works",
            flipso_saved_rename(furi_string_get_cstr(to), furi_string_get_cstr(lower)));
        check("and the card is still there", access(furi_string_get_cstr(lower), F_OK) == 0);
        FuriString* where = furi_string_alloc();
        check("and still found by its number", flipso_saved_find(card, where, NULL));
        flipso_saved_rename(furi_string_get_cstr(lower), furi_string_get_cstr(to));
        furi_string_free(where);
        furi_string_free(lower);
    }

    FuriString* missing = furi_string_alloc();
    flipso_saved_path(missing, "Never existed");
    flipso_saved_path(to, "Somewhere else");
    check(
        "renaming a card that is not there fails",
        !flipso_saved_rename(furi_string_get_cstr(missing), furi_string_get_cstr(to)));

    furi_string_free(missing);
    furi_string_free(found);
    furi_string_free(to);
    furi_string_free(from);
    flipso_capture_free(other);
    flipso_capture_free(card);
    clean();
}

int main(void) {
    printf("Naming\n");
    names();
    printf("\nSaving and loading\n");
    round_trip();
    printf("\nFiles that are not ours\n");
    bad_files();
    printf("\nFinding a card's own record\n");
    finding();
    printf("\nRenaming\n");
    renaming();

    printf("\n%s\n", failures ? "FAILURES" : "All saved card tests passed");
    return failures ? 1 : 0;
}
