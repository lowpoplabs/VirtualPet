/*
 * Pet brain: what the pet does between meals.
 *
 * A small state machine. The pet is always doing one action; when it
 * finishes, a picker chooses the next. The default picker scores every
 * action from how the pet feels and what is around it, bends the scores by
 * personality, then picks at random in proportion to the scores, so
 * behaviour is plausible but never scripted.
 *
 * The picker is one replaceable function. A trained model can take its
 * place later (the plan's stretch goal) without touching anything else:
 * it gets the same senses and returns an action.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ACT_IDLE,       /* stand and breathe */
    ACT_LOOK,       /* glance left and right */
    ACT_SNIFF,      /* antennae twitch */
    ACT_PLAY,       /* bounce about */
    ACT_NAP,        /* eyes shut */
    ACT_GRUMBLE,    /* hungry or queasy shudder */
    ACT_COUNT,
} pet_action_t;

/* Everything a picker may base its choice on. */
typedef struct {
    float fullness;     /* 0 empty .. 1 full */
    float mood;         /* 0 .. 1 */
    float busy;         /* 0 quiet .. 1 lots of food around */
    bool night;
    bool queasy;
    bool craving;
    bool underfed;
    uint8_t traits;     /* bit per pet_trait_t */
    pet_action_t previous;
} pet_senses_t;

typedef pet_action_t (*pet_picker_t)(const pet_senses_t *senses, uint32_t random);

/* The default picker, and the scores behind it (exposed for tests and tuning). */
pet_action_t pet_pick_default(const pet_senses_t *senses, uint32_t random);
void pet_action_scores(const pet_senses_t *senses, float scores[ACT_COUNT]);

const char *pet_action_name(pet_action_t action);

typedef struct {
    pet_picker_t picker;
    pet_action_t action;
    uint32_t until_ms;
    uint32_t rng;
    bool started;
} pet_brain_t;

/* picker may be NULL for the default. */
void pet_brain_init(pet_brain_t *brain, pet_picker_t picker, uint32_t seed);

/* Reads the senses off a pet. */
pet_senses_t pet_senses(const pet_t *pet, pet_action_t previous);

/*
 * Call a few times a second. real_ms is wall-clock milliseconds: actions
 * last seconds of real time however fast pet time runs. Returns true when
 * the action has just changed.
 */
bool pet_brain_step(pet_brain_t *brain, const pet_t *pet, uint32_t real_ms);

#ifdef __cplusplus
}
#endif
