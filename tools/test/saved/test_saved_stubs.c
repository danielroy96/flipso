/**
 * @file test_saved_stubs.c
 * @brief The firmware's storage, stream and dialog calls, backed by real files.
 *
 * What flipso_saved.c asks of the firmware, done against a real directory, so
 * that what reaches the disk is what the device would write.
 */
#include "test_saved.h"

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
long stub_write_budget = -1;

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

/* Prefixed to a path the stub resolves, so a test can tell a resolved path
 * from the alias the app was built with. */
#define STUB_RESOLVED "resolved/"

void storage_common_resolve_path_and_ensure_app_directory(Storage* storage, FuriString* path) {
    (void)storage;
    if(strncmp(furi_string_get_cstr(path), STUB_RESOLVED, strlen(STUB_RESOLVED)) == 0) return;
    char real[256];
    snprintf(real, sizeof(real), STUB_RESOLVED "%s", furi_string_get_cstr(path));
    furi_string_set(path, real);
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
bool stub_rename_fails = false;

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

char last_alert[128];

void dialog_file_browser_set_basic_options(
    DialogsFileBrowserOptions* options,
    const char* extension,
    const Icon* icon) {
    memset(options, 0, sizeof(*options));
    options->extension = extension;
    options->icon = icon;
}

char last_browser_base[128];
char last_browser_start[128];

bool dialog_file_browser_show(
    DialogsApp* context,
    FuriString* result_path,
    FuriString* path,
    const DialogsFileBrowserOptions* options) {
    (void)context;
    (void)result_path;
    snprintf(last_browser_base, sizeof(last_browser_base), "%s", options->base_path);
    snprintf(last_browser_start, sizeof(last_browser_start), "%s", furi_string_get_cstr(path));
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
