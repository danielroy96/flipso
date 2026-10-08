/**
 * @file flipso_table.h
 * @brief A packed lookup table on the SD card, searched in place.
 *
 * The station, stop and ticket type tables share one design: a header, one or
 * more sorted indexes of fixed-width entries, then a blob of names packed end
 * to end, each entry pointing at its name with a 3-byte offset and a length.
 * A lookup is a binary search of a dozen short reads, so none of a table is
 * ever loaded. This is the part they have in common; each reader keeps its
 * own header layout and its own idea of how a key compares.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    File* file;
    uint32_t size; /**< File size, so a corrupt offset cannot read past the end. */
    uint32_t names; /**< Absolute offset of the name blob, which the header check sets. */
} FlipsoTable;

/** What flipso_table_try() found at a path. */
typedef enum {
    FlipsoTableMissing, /**< Nothing there, or nothing that would open. */
    FlipsoTableUnusable, /**< A file whose header the reader refused. */
    FlipsoTableReady, /**< Open, and its header accepted. */
} FlipsoTableState;

/**
 * Validate a table's header: range check every field against
 * @c table->size, and set @c table->names. A table lives on a removable card,
 * so a truncated or unrelated file has to fail here rather than send a
 * search off the end of it.
 */
typedef bool (*FlipsoTableHeaderCheck)(FlipsoTable* table, void* context);

/** Order an index entry against the key sought, as memcmp() would. */
typedef int (*FlipsoTableOrder)(const uint8_t* entry, const void* key);

/** An unsigned little-endian integer of @p n bytes. */
uint32_t flipso_table_le(const uint8_t* bytes, size_t n);

/** Read @p length bytes at @p offset. */
bool flipso_table_read_at(FlipsoTable* table, uint32_t offset, void* into, uint16_t length);

/**
 * Open @p path in @c table->file, which the caller has allocated, and keep it
 * open if @p check accepts its header. Anything else leaves it closed - a
 * failed open included, which the storage service needs closing too.
 */
FlipsoTableState flipso_table_try(
    FlipsoTable* table,
    const char* path,
    FlipsoTableHeaderCheck check,
    void* context);

/**
 * Binary-search @p count entries of @p entry_len bytes, starting at absolute
 * offset @p index and sorted as @p order sorts them, for @p key.
 * @return true with the matching entry in @p entry, which holds @p entry_len.
 */
bool flipso_table_find(
    FlipsoTable* table,
    uint32_t index,
    uint32_t count,
    uint16_t entry_len,
    FlipsoTableOrder order,
    const void* key,
    uint8_t* entry);

/**
 * The name an index entry points at - a 3-byte offset into the blob at
 * @p entry + @p at, then a length byte - copied into @p out, which has room
 * for @p max characters and a terminator.
 * @return @p out, or NULL when the entry points outside the file.
 */
const char*
    flipso_table_name(FlipsoTable* table, const uint8_t* entry, size_t at, char* out, uint8_t max);

#ifdef __cplusplus
}
#endif
