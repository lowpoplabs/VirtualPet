#include "pet_brain.h"

/* How long each action lasts, in real milliseconds: {shortest, longest}. */
static const uint32_t DURATION_MS[ACT_COUNT][2] = {
    [ACT_IDLE]    = {4000, 10000},
    [ACT_LOOK]    = {3000, 5500},
    [ACT_SNIFF]   = {2000, 4000},
    [ACT_PLAY]    = {2000, 3500},
    [ACT_NAP]     = {20000, 60000},
    [ACT_GRUMBLE] = {2000, 4000},
};

/* Foods found lately that count as a thoroughly busy place. */
#define BUSY_FULL_SCALE 10.0f

static bool has(const pet_senses_t *s, pet_trait_t t)
{
    return (s->traits >> t) & 1;
}

void pet_action_scores(const pet_senses_t *s, float score[ACT_COUNT])
{
    /* A greedy pet starts complaining well before it is actually hungry. */
    float hungry_line = has(s, TRAIT_GREEDY) ? 0.35f : 0.2f;
    float hunger = s->fullness < hungry_line ? 1.0f - s->fullness / hungry_line : 0;

    score[ACT_IDLE] = 3.0f;
    /* Busy places make it perk up and look about; a craving makes it sniff for food. */
    score[ACT_LOOK] = 1.5f + 2.0f * s->busy;
    score[ACT_SNIFF] = 1.0f + 2.0f * s->busy + (s->craving ? 1.0f : 0);
    /* Play takes a good mood and something in the stomach. */
    score[ACT_PLAY] = 0.2f + 3.0f * s->mood * s->mood * (s->fullness > 0.2f ? 1.0f : 0.2f);
    /* Naps: mostly at night, otherwise when glum or when nothing is going on. */
    score[ACT_NAP] = s->night ? 8.0f : 0.3f + (1.0f - s->mood) + (s->busy < 0.1f ? 0.7f : 0);
    score[ACT_GRUMBLE] = 4.0f * hunger + (s->queasy ? 3.0f : 0) + (s->underfed ? 3.0f : 0);

    if (s->queasy) {
        score[ACT_PLAY] *= 0.2f;
    }

    /* Personality bends the scores. */
    if (has(s, TRAIT_LAZY)) {
        score[ACT_NAP] *= 2.0f;
        score[ACT_IDLE] *= 1.5f;
        score[ACT_PLAY] *= 0.5f;
    }
    if (has(s, TRAIT_GREEDY)) {
        score[ACT_GRUMBLE] *= 1.5f;
        score[ACT_SNIFF] *= 1.5f;
    }
    if (has(s, TRAIT_CURIOUS)) {
        score[ACT_LOOK] *= 2.0f;
        score[ACT_SNIFF] *= 1.5f;
    }
    if (has(s, TRAIT_PLAYFUL)) {
        score[ACT_PLAY] *= 2.0f;
    }
    if (has(s, TRAIT_SHY)) {
        /* Goes quiet when there is a lot going on. */
        score[ACT_PLAY] *= 1.0f - 0.6f * s->busy;
        score[ACT_IDLE] *= 1.0f + s->busy;
    }
    if (has(s, TRAIT_CHEERFUL)) {
        score[ACT_PLAY] *= 1.3f;
        score[ACT_GRUMBLE] *= 0.5f;
    }

    /* Doing the same thing twice running looks mechanical. Standing and sleeping are exempt. */
    if (s->previous != ACT_IDLE && s->previous != ACT_NAP && s->previous < ACT_COUNT) {
        score[s->previous] *= 0.4f;
    }
}

pet_action_t pet_pick_default(const pet_senses_t *senses, uint32_t random)
{
    float score[ACT_COUNT];
    pet_action_scores(senses, score);

    float total = 0;
    for (int i = 0; i < ACT_COUNT; i++) {
        total += score[i];
    }
    float pick = (random % 100000) / 100000.0f * total;
    for (int i = 0; i < ACT_COUNT; i++) {
        pick -= score[i];
        if (pick < 0) {
            return (pet_action_t)i;
        }
    }
    return ACT_IDLE;
}

const char *pet_action_name(pet_action_t action)
{
    static const char *const names[ACT_COUNT] = {"idle", "look", "sniff", "play", "nap", "grumble"};
    return action < ACT_COUNT ? names[action] : "?";
}

static uint32_t next_random(pet_brain_t *brain)
{
    uint32_t x = brain->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return brain->rng = x;
}

void pet_brain_init(pet_brain_t *brain, pet_picker_t picker, uint32_t seed)
{
    brain->picker = picker ? picker : pet_pick_default;
    brain->action = ACT_IDLE;
    brain->until_ms = 0;
    brain->rng = seed ? seed : 0x2545F491u;
    brain->started = false;
}

pet_senses_t pet_senses(const pet_t *pet, pet_action_t previous)
{
    float busy = pet->activity / BUSY_FULL_SCALE;
    return (pet_senses_t){
        .fullness = pet->stomach / pet->tune.stomach_max,
        .mood = pet->mood / 100.0f,
        .busy = busy > 1.0f ? 1.0f : busy,
        .night = pet_is_night(pet),
        .queasy = pet_is_queasy(pet),
        .craving = pet->craving != PET_NO_CRAVING,
        .underfed = pet->is_static,
        .traits = pet->traits,
        .previous = previous,
    };
}

bool pet_brain_step(pet_brain_t *brain, const pet_t *pet, uint32_t real_ms)
{
    /* Signed difference so the millisecond counter can wrap. */
    if (brain->started && (int32_t)(real_ms - brain->until_ms) < 0) {
        return false;
    }
    pet_senses_t senses = pet_senses(pet, brain->action);
    pet_action_t next = brain->picker(&senses, next_random(brain));
    if (next >= ACT_COUNT) {
        next = ACT_IDLE;   /* a replacement picker misbehaving must not break the pet */
    }
    uint32_t lo = DURATION_MS[next][0], hi = DURATION_MS[next][1];
    brain->until_ms = real_ms + lo + next_random(brain) % (hi - lo + 1);
    bool changed = !brain->started || next != brain->action;
    brain->action = next;
    brain->started = true;
    return changed;
}
