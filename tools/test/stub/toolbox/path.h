/* Host stand-in for the path helpers: just the one the saved cards use. */
#pragma once

#include <furi.h>

void path_extract_filename_no_ext(const char* path, FuriString* filename);
