/**
 * @file flipso_capture_file.c
 * @brief A capture as the saved file holds it, a line at a time.
 */
#include "flipso_capture_i.h"

#include <stdio.h>
#include <string.h>

/** Bumped only when a file this build writes would mislead an older one. */
#define FLIPSO_CAPTURE_VERSION 1

#define FLIPSO_CAPTURE_FILETYPE "Flipso card"

/** Header lines that come before the blocks. */
#define FLIPSO_CAPTURE_HEADER_LINES 3

/** Write the key a block is stored under, e.g. "Product 3". */
static void flipso_capture_key(const FlipsoCaptureBlock* block, char* out, size_t out_len) {
    switch((FlipsoBlockKind)block->kind) {
    case FlipsoBlockShell:
        snprintf(out, out_len, "Shell");
        break;
    case FlipsoBlockDirectory:
        snprintf(out, out_len, "Directory");
        break;
    case FlipsoBlockLog:
        snprintf(out, out_len, "Log");
        break;
    case FlipsoBlockLogHistory:
        snprintf(out, out_len, "Log history");
        break;
    case FlipsoBlockChip:
        snprintf(out, out_len, "Chip");
        break;
    case FlipsoBlockType2:
        snprintf(out, out_len, "Type 2");
        break;
    case FlipsoBlockTag:
        snprintf(out, out_len, "Tag");
        break;
    case FlipsoBlockValueHistory:
        snprintf(out, out_len, "Value history %u", block->index);
        break;
    case FlipsoBlockProductHistory:
        snprintf(out, out_len, "Product history %u", block->index);
        break;
    case FlipsoBlockProduct:
    default:
        snprintf(out, out_len, "Product %u", block->index);
        break;
    }
}

size_t flipso_capture_lines(const FlipsoCapture* capture) {
    if(!capture) return 0;
    return FLIPSO_CAPTURE_HEADER_LINES + capture->count;
}

bool flipso_capture_line(const FlipsoCapture* capture, size_t index, char* out, size_t out_len) {
    if(!capture || !out || out_len == 0) return false;
    if(index >= flipso_capture_lines(capture)) return false;

    if(index == 0) {
        return (size_t)snprintf(out, out_len, "Filetype: %s", FLIPSO_CAPTURE_FILETYPE) < out_len;
    }
    if(index == 1) {
        return (size_t)snprintf(out, out_len, "Version: %u", FLIPSO_CAPTURE_VERSION) < out_len;
    }
    if(index == 2) {
        return (size_t)snprintf(out, out_len, "Read at: %lu", (unsigned long)capture->timestamp) <
               out_len;
    }

    const FlipsoCaptureBlock* block = &capture->blocks[index - FLIPSO_CAPTURE_HEADER_LINES];
    char key[FLIPSO_CAPTURE_KEY_MAX];
    flipso_capture_key(block, key, sizeof(key));

    /* Space-separated hex, as the firmware's own files write a byte array. */
    size_t pos = (size_t)snprintf(out, out_len, "%s:", key);
    if(pos >= out_len) return false;
    for(uint16_t i = 0; i < block->len; i++) {
        if(pos + 4 > out_len) return false;
        pos +=
            (size_t)snprintf(out + pos, out_len - pos, " %02X", capture->bytes[block->offset + i]);
    }
    return true;
}

/** Value of one hex digit, or -1. */
static int flipso_capture_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool flipso_capture_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * Count the bytes a hex value carries.
 * @return -1 for anything that is not whitespace-separated pairs of hex digits.
 */
static int flipso_capture_hex_len(const char* value) {
    int count = 0;
    while(*value) {
        if(flipso_capture_space(*value)) {
            value++;
            continue;
        }
        if(flipso_capture_nibble(value[0]) < 0 || flipso_capture_nibble(value[1]) < 0) return -1;
        value += 2;
        count++;
    }
    return count;
}

/** Fill @p out with the bytes counted by flipso_capture_hex_len(). */
static void flipso_capture_hex(const char* value, uint8_t* out, size_t len) {
    for(size_t i = 0; i < len; i++) {
        while(flipso_capture_space(*value)) {
            value++;
        }
        out[i] =
            (uint8_t)((flipso_capture_nibble(value[0]) << 4) | flipso_capture_nibble(value[1]));
        value += 2;
    }
}

/**
 * True when @p key is @p prefix followed by a number, which goes to @p index.
 *
 * Both the per-product blocks are keyed this way - "Product 3" and "Value
 * history 3" - so the entry number is parsed once.
 */
static bool flipso_capture_indexed_key(const char* key, const char* prefix, uint8_t* index) {
    size_t prefix_len = strlen(prefix);
    if(strncmp(key, prefix, prefix_len) != 0) return false;

    const char* digits = key + prefix_len;
    if(*digits < '0' || *digits > '9') return false;

    /* Bounded by what the field can hold rather than by how many products a
     * shell may have: an entry number no product claims simply goes unused when
     * the card is decoded, and refusing it here would silently drop a block
     * that the writing side was perfectly willing to write. */
    unsigned value = 0;
    while(*digits >= '0' && *digits <= '9') {
        value = value * 10 + (unsigned)(*digits++ - '0');
        if(value > UINT8_MAX) return false;
    }
    if(*digits != '\0') return false;

    *index = (uint8_t)value;
    return true;
}

bool flipso_capture_parse_line(FlipsoCapture* capture, const char* line) {
    if(!capture || !line) return false;

    const char* colon = strchr(line, ':');
    if(!colon) return true; /* Blank line, or a comment: nothing to take from it. */

    /* Split into a trimmed key and the value after it. The key is bounded by
     * the longest one we write, so a line with a runaway key is simply not one
     * of ours. */
    char key[FLIPSO_CAPTURE_KEY_MAX];
    size_t key_len = (size_t)(colon - line);
    while(key_len && flipso_capture_space(line[0])) {
        line++;
        key_len--;
    }
    while(key_len && flipso_capture_space(line[key_len - 1])) {
        key_len--;
    }
    if(key_len == 0 || key_len >= sizeof(key)) return true;
    memcpy(key, line, key_len);
    key[key_len] = '\0';

    const char* value = colon + 1;
    while(flipso_capture_space(*value)) {
        value++;
    }
    /* Lines arrive from the file with their newline still on them, and the
     * fields below are compared or scanned to the end of the value. */
    const char* value_end = value + strlen(value);
    while(value_end > value && flipso_capture_space(value_end[-1])) {
        value_end--;
    }

    if(strcmp(key, "Filetype") == 0) {
        size_t len = sizeof(FLIPSO_CAPTURE_FILETYPE) - 1;
        return (size_t)(value_end - value) == len &&
               memcmp(value, FLIPSO_CAPTURE_FILETYPE, len) == 0;
    }

    if(strcmp(key, "Version") == 0) {
        unsigned parsed = 0;
        if(*value < '0' || *value > '9') return false;
        while(*value >= '0' && *value <= '9') {
            parsed = parsed * 10 + (unsigned)(*value++ - '0');
            if(parsed > FLIPSO_CAPTURE_VERSION) return false;
        }
        /* A file from a later build may place blocks this one would misread, so
         * it is refused rather than half-loaded. */
        return parsed == FLIPSO_CAPTURE_VERSION;
    }

    if(strcmp(key, "Read at") == 0) {
        unsigned long parsed = 0;
        while(*value >= '0' && *value <= '9') {
            parsed = parsed * 10 + (unsigned long)(*value++ - '0');
        }
        capture->timestamp = (uint32_t)parsed;
        return true;
    }

    FlipsoBlockKind kind;
    uint8_t index = 0;
    if(strcmp(key, "Shell") == 0) {
        kind = FlipsoBlockShell;
    } else if(strcmp(key, "Directory") == 0) {
        kind = FlipsoBlockDirectory;
    } else if(strcmp(key, "Log") == 0) {
        kind = FlipsoBlockLog;
    } else if(strcmp(key, "Log history") == 0) {
        kind = FlipsoBlockLogHistory;
    } else if(strcmp(key, "Chip") == 0) {
        kind = FlipsoBlockChip;
    } else if(strcmp(key, "Type 2") == 0) {
        kind = FlipsoBlockType2;
    } else if(strcmp(key, "Tag") == 0) {
        kind = FlipsoBlockTag;
    } else if(flipso_capture_indexed_key(key, "Product history ", &index)) {
        kind = FlipsoBlockProductHistory;
    } else if(flipso_capture_indexed_key(key, "Product ", &index)) {
        kind = FlipsoBlockProduct;
    } else if(flipso_capture_indexed_key(key, "Value history ", &index)) {
        kind = FlipsoBlockValueHistory;
    } else {
        /* A key from a later build. Skipping it loses that block and no more,
         * which is why the blocks are keyed rather than positional. */
        return true;
    }

    int len = flipso_capture_hex_len(value);
    /* A block whose hex is malformed is dropped whole rather than truncated:
     * half a sector chain would decode to plausible nonsense, while a missing
     * one decodes to a product the card shows without its details. */
    if(len <= 0) return true;

    uint8_t* out = flipso_capture_reserve(capture, kind, index, (size_t)len);
    if(out) flipso_capture_hex(value, out, (size_t)len);
    return true;
}
