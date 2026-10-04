/**
 * @file flipso_capture_i.h
 * @brief What the flipso_capture*.c files share: how the blocks are stored.
 *
 * The storage is one arena that grows as blocks arrive, plus a small index over
 * it; see flipso_capture.c.
 */
#pragma once

#include "flipso_capture.h"

#ifdef __cplusplus
extern "C" {
#endif

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
    ItsoUnixTime timestamp;
};

/** The block of this kind and directory entry, or NULL. */
const FlipsoCaptureBlock*
    flipso_capture_find(const FlipsoCapture* capture, FlipsoBlockKind kind, uint8_t index);

/**
 * Make room for one block and index it.
 * @return where to write @p len bytes, or NULL when the block cannot be kept.
 */
uint8_t*
    flipso_capture_reserve(FlipsoCapture* capture, FlipsoBlockKind kind, uint8_t index, size_t len);

#ifdef __cplusplus
}
#endif
