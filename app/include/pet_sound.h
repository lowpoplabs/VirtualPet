/*
 * Sounds: what the pet plays at an eating moment, and when it stays quiet.
 *
 * A sound is a short list of notes, so every platform can play it with a
 * plain tone generator and nothing needs audio files. The rules for which
 * sound fits a meal and whether it may play are pure logic, tested on the
 * desktop.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t hz;    /* 0 = silence */
    uint16_t ms;
} pet_note_t;

typedef struct pet_sound {
    const pet_note_t *notes;
    int count;
    int volume;     /* 0-100: how loud this sound is meant to be */
} pet_sound_t;

typedef enum {
    SOUND_CHIRP,    /* sniff cue, a second or two before the munch */
    SOUND_MUNCH,    /* common food: soft */
    SOUND_JINGLE,   /* rare food or a satisfied craving: brighter */
    SOUND_FANFARE,  /* a food never tasted before */
    SOUND_COUNT,
} pet_sound_id_t;

/* The setting from the plan: off, rare finds only, or everything. */
typedef enum {
    SOUND_LEVEL_OFF,
    SOUND_LEVEL_RARE,
    SOUND_LEVEL_ALL,
} pet_sound_level_t;

const pet_sound_t *pet_sound(pet_sound_id_t id);

/* Which sound an eating moment deserves: munch, jingle or fanfare. */
pet_sound_id_t pet_meal_sound(const pet_meal_t *meal);

/*
 * Whether that sound may play now. quiet is true during quiet hours.
 * ms_since_last is real time since the last eating sound (UINT32_MAX if
 * none yet): common sounds are spaced well apart so a busy street is not a
 * stream of beeps, rare ones less so.
 */
bool pet_sound_allowed(pet_sound_level_t level, pet_sound_id_t id, bool quiet,
                       uint32_t ms_since_last);

#ifdef __cplusplus
}
#endif
