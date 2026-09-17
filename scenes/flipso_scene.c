/**
 * @file flipso_scene.c
 * @brief Builds the scene handler tables from flipso_scene_config.h.
 */
#include "flipso_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
static void (*const flipso_scene_on_enter_handlers[])(void*) = {
#include "flipso_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
static bool (*const flipso_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "flipso_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
static void (*const flipso_scene_on_exit_handlers[])(void*) = {
#include "flipso_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers flipso_scene_handlers = {
    .on_enter_handlers = flipso_scene_on_enter_handlers,
    .on_event_handlers = flipso_scene_on_event_handlers,
    .on_exit_handlers = flipso_scene_on_exit_handlers,
    .scene_num = FlipsoSceneNum,
};
