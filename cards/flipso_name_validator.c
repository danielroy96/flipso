/**
 * @file flipso_name_validator.c
 * @brief The check a saved card's name has to pass before it is written.
 */
#include "flipso_name_validator.h"

#include <gui/modules/validators.h>
#include <string.h>
#include <strings.h>

/* FAT will not take these, and the Flipper's keyboard offers some of them. */
#define FLIPSO_NAME_FORBIDDEN "\\/:*?\"<>|"

struct FlipsoNameValidator {
    ValidatorIsFile* is_file;
    char current_name[FLIPSO_SAVED_NAME_LEN];
};

FlipsoNameValidator* flipso_name_validator_alloc(const char* current_name) {
    FlipsoNameValidator* validator = malloc(sizeof(FlipsoNameValidator));
    snprintf(validator->current_name, sizeof(validator->current_name), "%s", current_name);
    validator->is_file =
        validator_is_file_alloc_init(FLIPSO_SAVED_FOLDER, FLIPSO_SAVED_EXTENSION, current_name);
    return validator;
}

void flipso_name_validator_free(FlipsoNameValidator* validator) {
    if(!validator) return;
    validator_is_file_free(validator->is_file);
    free(validator);
}

bool flipso_name_validator(const char* text, FuriString* error, void* context) {
    FlipsoNameValidator* validator = context;
    for(const char* c = text; *c; c++) {
        if(strchr(FLIPSO_NAME_FORBIDDEN, *c)) {
            furi_string_printf(error, "Name cannot\ncontain %c", *c);
            return false;
        }
    }
    /* A name that is only spaces is a file FAT will not make. */
    bool blank = true;
    for(const char* c = text; *c; c++) {
        if(*c != ' ') blank = false;
    }
    if(blank) {
        furi_string_set(error, "Name cannot\nbe blank");
        return false;
    }
    /* The card's own name in different case: the SD card compares names
     * without case, so the firmware's check would find the card itself and
     * call it taken. */
    if(validator->current_name[0] && strcasecmp(text, validator->current_name) == 0) return true;
    return validator_is_file_callback(text, error, validator->is_file);
}
