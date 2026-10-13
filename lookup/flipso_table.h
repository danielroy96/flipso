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

#include <furi.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    File* file;
    uint32_t size; /**< File size, so a corrupt offset cannot read past the end. */
    uint32_t names; /**< Absolute offset of the name blob, which the header check sets. */
} FlipsoTable;

/** The header the station, ticket type and restriction tables share. */
#define FLIPSO_TABLE_HEADER 16

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
 * Validate the 16-byte header the station, ticket type and restriction tables
 * share - @p magic, @p version, a longest text no more than @p max, then the
 * entry count and the blob's offset - with every field range checked against
 * the file, as a FlipsoTableHeaderCheck must. Sets @c table->names and
 * @p count. The stop table's header is its own.
 */
bool flipso_table_check_header(
    FlipsoTable* table,
    const char* magic,
    uint8_t version,
    uint8_t max,
    uint16_t entry_len,
    uint32_t* count);

/**
 * Open a table only for as long as a lookup takes: the user's own build at
 * @p user_path if there is a usable one, else the packaged one at
 * @p asset_path. Allocates @c table->file, which flipso_table_close() frees.
 * @return false, with nothing left open or allocated, when neither will do.
 */
bool flipso_table_open(
    FlipsoTable* table,
    Storage* storage,
    const char* user_path,
    const char* asset_path,
    FlipsoTableHeaderCheck check,
    void* context);

/** Close and free what flipso_table_open() opened. */
void flipso_table_close(FlipsoTable* table);

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

/**
 * The same text as flipso_table_name() finds, appended to @p out a few bytes
 * at a time: a string sized to the text rather than a buffer sized to the
 * longest there could be, for a caller that wants several at once.
 * @return false when the entry points outside the file or holds no text,
 *         leaving @p out as it was.
 */
bool flipso_table_cat(
    FlipsoTable* table,
    const uint8_t* entry,
    size_t at,
    FuriString* out,
    uint8_t max);

#ifdef __cplusplus
}
#endif
