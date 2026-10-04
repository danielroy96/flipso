/**
 * @file flipso_saved_i.h
 * @brief What the flipso_saved*.c files share: the names a card passes through.
 */
#pragma once

#include "flipso_saved.h"

#define TAG "Flipso"

/*
 * The names a save or a rename passes a card through, each beside the record so
 * that every move is a rename within one folder. None is a card extension, so
 * the browser never lists one; flipso_saved_recover() finishes or undoes
 * whatever a power cut left half done.
 *
 *   .tmp  a new copy being written, which is only whole once it is renamed
 *   .old  the record being replaced, kept until the new copy is in its place
 *   .ren  a card part way through a change to the case of its name
 */
#define FLIPSO_SAVED_TEMP_SUFFIX ".tmp"
#define FLIPSO_SAVED_OLD_SUFFIX  ".old"
#define FLIPSO_SAVED_MOVE_SUFFIX ".ren"
