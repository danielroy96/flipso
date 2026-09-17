/* Host stand-in for the storage service: a File is a FILE*. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* The test points both app paths at files it controls. */
#define APP_DATA_PATH(path)   "stub_data_" path
#define APP_ASSETS_PATH(path) "stub_assets_" path

typedef struct Storage Storage;
typedef struct File File;

typedef enum {
    FSAM_READ = 1,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
} FS_OpenMode;

File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
void storage_file_close(File* file);
uint64_t storage_file_size(File* file);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
uint16_t storage_file_read(File* file, void* buffer, uint16_t size);
