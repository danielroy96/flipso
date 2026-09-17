/* Host stand-in for the file stream: enough of it to read a text file line by
 * line, which is all the operator table loader does with one. */
#pragma once

#include <furi.h>
#include <storage/storage.h>

typedef struct Stream Stream;

Stream* file_stream_alloc(Storage* storage);
bool file_stream_open(Stream* stream, const char* path, FS_AccessMode access, FS_OpenMode mode);
void file_stream_close(Stream* stream);
void stream_free(Stream* stream);

/** Read the next line into @p line, newline included, as the firmware does. */
bool stream_read_line(Stream* stream, FuriString* line);
