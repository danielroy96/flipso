/**
 * @file flipso_capture.c
 * @brief Holds the raw blocks read off a card, and turns them back into a card.
 *
 * See flipso_capture.h for why a saved card is raw bytes rather than decoded
 * fields. The storage here is one arena that grows as blocks arrive, plus a
 * small index over it: a typical CMD7 card yields about 800 bytes across ten
 * blocks, and sizing for the 9.5 KB worst case up front would spend a twentieth
 * of the Flipper's heap on slack that almost no card uses.
 */
#include "flipso_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Bumped only when a file this build writes would mislead an older one. */
#define FLIPSO_CAPTURE_VERSION 1

#define FLIPSO_CAPTURE_FILETYPE "Flipso card"

/* First allocation, and the step the arena doubles from. Covers a whole CMD7
 * card in two grows, and the tiny cards in one. */
#define FLIPSO_CAPTURE_CHUNK 512

typedef struct {
    uint8_t kind;
    uint8_t index; /**< Directory entry, for FlipsoBlockProduct. */
    uint16_t offset; /**< Into FlipsoCapture::bytes. */
    uint16_t len;
} FlipsoCaptureBlock;

struct FlipsoCapture {
    uint8_t* bytes;
    size_t len;
    size_t capacity;
    FlipsoCaptureBlock blocks[FLIPSO_CAPTURE_MAX_BLOCKS];
    uint8_t count;
    uint32_t timestamp;
};

FlipsoCapture* flipso_capture_alloc(void) {
    FlipsoCapture* capture = malloc(sizeof(FlipsoCapture));
    memset(capture, 0, sizeof(FlipsoCapture));
    return capture;
}

void flipso_capture_free(FlipsoCapture* capture) {
    if(!capture) return;
    free(capture->bytes);
    free(capture);
}

void flipso_capture_reset(FlipsoCapture* capture) {
    if(!capture) return;
    /* The arena goes back too rather than being kept for the next card: between
     * scans the app sits on the idle screen, where holding a kilobyte of the
     * last card's bytes buys nothing. */
    free(capture->bytes);
    capture->bytes = NULL;
    capture->len = 0;
    capture->capacity = 0;
    capture->count = 0;
    capture->timestamp = 0;
}

/** The block of this kind and directory entry, or NULL. */
static const FlipsoCaptureBlock*
    flipso_capture_find(const FlipsoCapture* capture, FlipsoBlockKind kind, uint8_t index) {
    for(uint8_t i = 0; i < capture->count; i++) {
        const FlipsoCaptureBlock* block = &capture->blocks[i];
        if(block->kind != (uint8_t)kind) continue;
        /* Only products are distinguished by directory entry; the shell, the
         * directory and the log occur once per card. */
        if(kind == FlipsoBlockProduct && block->index != index) continue;
        return block;
    }
    return NULL;
}

/**
 * Make room for one block and index it.
 * @return where to write @p len bytes, or NULL when the block cannot be kept.
 */
static uint8_t* flipso_capture_reserve(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    size_t len) {
    if(!capture || len == 0 || len > ITSO_MAX_GROUP_LEN) return NULL;
    if(capture->count >= FLIPSO_CAPTURE_MAX_BLOCKS) return NULL;
    /* A repeat means a malformed file, or a reader reading the same sector
     * twice; either way the first answer is the one to keep. */
    if(flipso_capture_find(capture, kind, index)) return NULL;
    if(capture->len + len > FLIPSO_CAPTURE_MAX_BYTES) return NULL;

    if(capture->len + len > capture->capacity) {
        size_t capacity = capture->capacity ? capture->capacity : FLIPSO_CAPTURE_CHUNK;
        while(capacity < capture->len + len) {
            capacity *= 2;
        }
        if(capacity > FLIPSO_CAPTURE_MAX_BYTES) capacity = FLIPSO_CAPTURE_MAX_BYTES;

        uint8_t* grown = realloc(capture->bytes, capacity);
        if(!grown) return NULL;
        capture->bytes = grown;
        capture->capacity = capacity;
    }

    FlipsoCaptureBlock* block = &capture->blocks[capture->count++];
    block->kind = (uint8_t)kind;
    block->index = index;
    block->offset = (uint16_t)capture->len;
    block->len = (uint16_t)len;
    capture->len += len;
    return capture->bytes + block->offset;
}

bool flipso_capture_add(
    FlipsoCapture* capture,
    FlipsoBlockKind kind,
    uint8_t index,
    const uint8_t* data,
    size_t len) {
    if(!data) return false;
    uint8_t* out = flipso_capture_reserve(capture, kind, index, len);
    if(!out) return false;
    memcpy(out, data, len);
    return true;
}

bool flipso_capture_valid(const FlipsoCapture* capture) {
    return capture && flipso_capture_find(capture, FlipsoBlockShell, 0) != NULL;
}

uint32_t flipso_capture_time(const FlipsoCapture* capture) {
    return capture ? capture->timestamp : 0;
}

void flipso_capture_set_time(FlipsoCapture* capture, uint32_t timestamp) {
    if(capture) capture->timestamp = timestamp;
}

bool flipso_capture_card_number(const FlipsoCapture* capture, char* out) {
    if(!capture || !out) return false;

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(!shell) return false;
    return itso_shell_card_number(capture->bytes + shell->offset, shell->len, out);
}

bool flipso_capture_decode(const FlipsoCapture* capture, ItsoCard* card) {
    if(!capture || !card) return false;

    itso_card_reset(card);

    const FlipsoCaptureBlock* shell = flipso_capture_find(capture, FlipsoBlockShell, 0);
    if(!shell) return false;
    if(!itso_parse_shell(card, capture->bytes + shell->offset, shell->len)) return false;

    /* From here the sequence mirrors flipso_read_card(), including what it does
     * when a group is missing: a directory that will not parse still leaves the
     * card number and expiry on screen, and a product with no block keeps
     * whatever its directory entry said about it. */
    const FlipsoCaptureBlock* dir = flipso_capture_find(capture, FlipsoBlockDirectory, 0);
    if(dir) itso_parse_directory(card, capture->bytes + dir->offset, dir->len);

    for(uint8_t i = 0; i < card->product_count; i++) {
        ItsoProduct* product = &card->products[i];
        const FlipsoCaptureBlock* group =
            flipso_capture_find(capture, FlipsoBlockProduct, product->dir_index);
        if(group) {
            itso_parse_ipe(
                product, capture->bytes + group->offset, group->len, card->sector_size);
        }
    }

    const FlipsoCaptureBlock* log = flipso_capture_find(capture, FlipsoBlockLog, 0);
    if(log) itso_parse_log(card, capture->bytes + log->offset, log->len);

    return true;
}

/* ------------------------------------------------------------------ */
/* The saved file, a line at a time                                    */
/* ------------------------------------------------------------------ */

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
    char key[16];
    flipso_capture_key(block, key, sizeof(key));

    /* Space-separated hex, as the firmware's own files write a byte array. */
    size_t pos = (size_t)snprintf(out, out_len, "%s:", key);
    if(pos >= out_len) return false;
    for(uint16_t i = 0; i < block->len; i++) {
        if(pos + 4 > out_len) return false;
        pos += (size_t)snprintf(
            out + pos, out_len - pos, " %02X", capture->bytes[block->offset + i]);
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
        out[i] = (uint8_t)(
            (flipso_capture_nibble(value[0]) << 4) | flipso_capture_nibble(value[1]));
        value += 2;
    }
}

/** True when @p key is "Product <n>", writing the entry number to @p index. */
static bool flipso_capture_product_key(const char* key, uint8_t* index) {
    static const char prefix[] = "Product ";
    if(strncmp(key, prefix, sizeof(prefix) - 1) != 0) return false;

    const char* digits = key + sizeof(prefix) - 1;
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
    char key[16];
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
    } else if(flipso_capture_product_key(key, &index)) {
        kind = FlipsoBlockProduct;
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
