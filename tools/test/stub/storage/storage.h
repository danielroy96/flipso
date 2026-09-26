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
    FSAM_WRITE = 2,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_CREATE_ALWAYS = 2,
} FS_OpenMode;

typedef enum {
    FSF_DIRECTORY = (1 << 0),
} FS_Flags;

typedef struct {
    uint8_t flags;
    uint64_t size;
} FileInfo;

File* storage_file_alloc(Storage* storage);
void storage_file_free(File* file);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
void storage_file_close(File* file);
uint64_t storage_file_size(File* file);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
uint16_t storage_file_read(File* file, void* buffer, uint16_t size);
uint16_t storage_file_write(File* file, const void* buffer, uint16_t size);

/* Directory walking and the whole-path helpers, for the saved-card folder. */
bool storage_dir_open(File* file, const char* path);
bool storage_dir_read(File* file, FileInfo* fileinfo, char* name, uint16_t name_length);
bool storage_dir_close(File* file);
bool storage_simply_mkdir(Storage* storage, const char* path);
bool storage_simply_remove(Storage* storage, const char* path);

typedef enum {
    FSE_OK = 0,
    FSE_NOT_EXIST,
    FSE_EXIST,
    FSE_INTERNAL,
} FS_Error;

bool storage_common_exists(Storage* storage, const char* path);
FS_Error storage_common_rename(Storage* storage, const char* old_path, const char* new_path);
