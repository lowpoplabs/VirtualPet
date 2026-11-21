#include "pet_sound.h"

/* A food at or above this table value counts as a rare find. */
#define RARE_VALUE 3.0f

/* Least real time between eating sounds, by kind. */
#define GAP_MUNCH_MS   (3 * 60 * 1000)
#define GAP_JINGLE_MS  (30 * 1000)
#define GAP_FANFARE_MS (5 * 1000)

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Two quick rising pips: "something's coming". */
static const pet_note_t CHIRP[] = {{2637, 40}, {0, 25}, {3136, 55}};

/* Three low, falling blips: nom nom nom. */
static const pet_note_t MUNCH[] = {{392, 55}, {0, 60}, {349, 55}, {0, 60}, {330, 70}};

/* A bright rising arpeggio. */
static const pet_note_t JINGLE[] = {{1047, 70}, {1319, 70}, {1568, 70}, {2093, 140}};

/* A proper little fanfare. */
static const pet_note_t FANFARE[] = {{784, 110}, {1047, 110}, {1319, 110}, {1568, 240},
                                     {0, 50},    {1568, 90},  {2093, 320}};

static const pet_sound_t SOUNDS[SOUND_COUNT] = {
    [SOUND_CHIRP]   = {CHIRP, COUNT(CHIRP), 40},
    [SOUND_MUNCH]   = {MUNCH, COUNT(MUNCH), 35},
    [SOUND_JINGLE]  = {JINGLE, COUNT(JINGLE), 60},
    [SOUND_FANFARE] = {FANFARE, COUNT(FANFARE), 80},
};

const pet_sound_t *pet_sound(pet_sound_id_t id)
{
    return id < SOUND_COUNT ? &SOUNDS[id] : &SOUNDS[SOUND_MUNCH];
}

pet_sound_id_t pet_meal_sound(const pet_meal_t *meal)
{
    if (meal->new_foods_ever > 0) {
        return SOUND_FANFARE;
    }
    if (meal->best_value >= RARE_VALUE || meal->craving_satisfied) {
        return SOUND_JINGLE;
    }
    return SOUND_MUNCH;
}

bool pet_sound_allowed(pet_sound_level_t level, pet_sound_id_t id, bool quiet,
                       uint32_t ms_since_last)
{
    if (level == SOUND_LEVEL_OFF || quiet) {
        return false;
    }
    switch (id) {
    case SOUND_FANFARE:
        return ms_since_last >= GAP_FANFARE_MS;
    case SOUND_JINGLE:
        return ms_since_last >= GAP_JINGLE_MS;
    case SOUND_MUNCH:
        return level == SOUND_LEVEL_ALL && ms_since_last >= GAP_MUNCH_MS;
    default:
        return false;   /* the chirp only ever goes with one of the above */
    }
}
