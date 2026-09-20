/*
 * Host stand-in for the dialogs service.
 *
 * Nothing here draws anything: the file browser and the alert are the two parts
 * of saving a card that need a person in front of them, so on the host they are
 * recorded and asserted on rather than shown.
 */
#pragma once

#include <furi.h>
#include <gui/canvas.h>

#define RECORD_DIALOGS "dialogs"

typedef struct DialogsApp DialogsApp;
typedef struct DialogMessage DialogMessage;

typedef struct {
    const char* extension;
    const char* base_path;
    bool skip_assets;
    bool hide_dot_files;
    const Icon* icon;
    bool hide_ext;
    void* item_loader_callback;
    void* item_loader_context;
} DialogsFileBrowserOptions;

void dialog_file_browser_set_basic_options(
    DialogsFileBrowserOptions* options,
    const char* extension,
    const Icon* icon);

bool dialog_file_browser_show(
    DialogsApp* context,
    FuriString* result_path,
    FuriString* path,
    const DialogsFileBrowserOptions* options);

DialogMessage* dialog_message_alloc(void);
void dialog_message_free(DialogMessage* message);
void dialog_message_set_header(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);
void dialog_message_set_text(
    DialogMessage* message,
    const char* text,
    uint8_t x,
    uint8_t y,
    Align horizontal,
    Align vertical);
void dialog_message_set_buttons(
    DialogMessage* message,
    const char* left,
    const char* center,
    const char* right);
int dialog_message_show(DialogsApp* context, const DialogMessage* message);
