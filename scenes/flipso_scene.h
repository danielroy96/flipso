/**
 * @file flipso_scene.h
 * @brief Scene identifiers and handler tables, generated from flipso_scene_config.h.
 */
#pragma once

#include <gui/scene_manager.h>

/* Scene enum: FlipsoSceneScan, FlipsoSceneMenu, ... */
#define ADD_SCENE(prefix, name, id) FlipsoScene##id,
typedef enum {
#include "flipso_scene_config.h"
    FlipsoSceneNum,
} FlipsoScene;
#undef ADD_SCENE

extern const SceneManagerHandlers flipso_scene_handlers;

/* Per-scene handler prototypes. */
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void* context);
#include "flipso_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "flipso_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "flipso_scene_config.h"
#undef ADD_SCENE
