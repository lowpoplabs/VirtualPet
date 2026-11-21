#include "pet_core.h"

#include <math.h>
#include <string.h>

void pet_grow(pet_t *pet);   /* pet_growth.c */
void pet_badges_check(pet_t *pet, bool day_ended);   /* pet_badges.c */
void pet_badge_award(pet_t *pet, pet_badge_t badge);

#define RARE_VALUE 3.0f   /* table value from which a find counts as rare */

#define ACTIVITY_HALF_LIFE_S 600
#define MOOD_MAX       100.0f
#define MOOD_PAT       1.5f
#define MOOD_QUEASY    15.0f   /* how far queasy pulls the resting mood down */
/* Chance (in 100) that a craving is for something never tasted. */
#define CRAVE_NEW_PCT  20

pet_tuning_t pet_tuning_default(void)
{
    return (pet_tuning_t){
        .stomach_max = 20,
        .drain_per_hour = 1,
        .start_stomach = 10,
        .repeat_factor = 0.5f,
        .meal_gap_s = 60,
        .junk_limit = 5,
        .queasy_s = 2 * 3600,
        .mood_per_new_type = 3,
        .mood_per_value = 1,
        .mood_drift_per_hour = 8,
        .craving_value_factor = 1.5f,
        .craving_mood = 10,
        .craving_every_s = 4 * 3600,
        .craving_lasts_s = 12 * 3600,
        .hungry_below = 4,
        .empty_below = 1,
        .static_after_s = 48 * 3600,
        .static_recover_at = 5,
        .night_start_hour = 22,
        .night_end_hour = 7,
        .growth_speed = 1,
        .egg_meals = 5,
        .hatchling_days = 7,
        .hatchling_meals = 60,
        .juvenile_days = 14,
        .juvenile_meals = 150,
        .adult_days = 63,
        .adult_meals = 600,
        .roamer_max_home_share = 0.6f,
        .roamer_finds_per_day = 60,
        .new_place_mood = 8,
        .visit_mood = 3,
        .prism_max_share = 0.25f,
        .prism_min_types = 10,
        .greedy_grumble_at = 0.35f,
        .lazy_nap = 2.0f,
        .lazy_idle = 1.5f,
        .lazy_play = 0.5f,
        .greedy_grumble = 1.5f,
        .greedy_sniff = 1.5f,
        .curious_look = 2.0f,
        .curious_sniff = 1.5f,
        .playful_play = 2.0f,
        .shy_play_less = 0.6f,
        .shy_idle_more = 1.0f,
        .cheerful_play = 1.3f,
        .cheerful_grumble = 0.5f,
    };
}

static uint32_t next_random(pet_t *pet)
{
    /* xorshift32: small, and repeatable from a seed for tests and replays. */
    uint32_t x = pet->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return pet->rng = x;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void pet_init(pet_t *pet, const pet_tuning_t *tune, uint32_t now, uint32_t seed)
{
    memset(pet, 0, sizeof(*pet));
    pet->tune = tune ? *tune : pet_tuning_default();
    pet->now = now;
    pet->rng = seed ? seed : 0x9E3779B9u;
    pet->stomach = pet->tune.start_stomach;
    pet->mood = 50;
    pet->day = now / 86400;
    pet->craving = PET_NO_CRAVING;
    pet->next_craving = now + pet->tune.craving_every_s;
    pet->pending.best = FOOD_TYPE_COUNT;
    pet->today.day = pet->day;
    pet->today.best = FOOD_TYPE_COUNT;
    places_init(&pet->places);
    pet->generation = 1;

    /* A new egg: two different traits at random. */
    pet->born = now;
    pet->stage = STAGE_EGG;
    pet->form = FORM_NONE;
    pet->stage_started = now;
    for (int n = 0; n < PET_TRAITS_PER_PET;) {
        uint8_t bit = (uint8_t)(1u << (next_random(pet) % TRAIT_COUNT));
        if (!(pet->traits & bit)) {
            pet->traits |= bit;
            n++;
        }
    }
}

void pet_badges_check(pet_t *pet, bool day_ended);      /* pet_badges.c */

bool pet_can_retire(const pet_t *pet)
{
    return pet->stage == STAGE_ADULT && pet->retire_ready;
}

bool pet_retire(pet_t *pet, uint32_t seed)
{
    if (!pet_can_retire(pet)) {
        return false;
    }

    /* What the farm keeps of it. */
    pet_resident_t r = {0};
    memcpy(r.name, pet->name, sizeof(r.name));
    r.form = (uint8_t)pet->form;
    r.traits = pet->traits;
    r.generation = pet->generation;
    float speed = pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1;
    float days = (float)(pet->now - pet->born) * speed / 86400.0f;
    r.days = days > 65535 ? 65535 : (uint16_t)days;
    r.retired_day = pet->day;
    r.meals = pet->meals;
    float g[GROUP_COUNT];
    pet_group_points(pet, g);
    r.favourite = GROUP_COUNT;
    for (int i = 0; i < GROUP_COUNT; i++) {
        if (g[i] > 0 && (r.favourite == GROUP_COUNT || g[i] > g[r.favourite])) {
            r.favourite = (uint8_t)i;
        }
    }

    /* What outlives the pet: the farm, the badges, and the owner's places. */
    pet_farm_t farm = pet->farm;
    if (farm.count >= FARM_MAX) {
        memmove(&farm.resident[0], &farm.resident[1], (FARM_MAX - 1) * sizeof(farm.resident[0]));
        farm.count = FARM_MAX - 1;
        if (farm.moved_on < UINT16_MAX) {
            farm.moved_on++;
        }
    }
    farm.resident[farm.count++] = r;
    farm.forms |= (uint16_t)(1u << pet->form);
    const pet_tuning_t tune = pet->tune;
    const pet_places_t places = pet->places;
    uint32_t badges = pet->badges, new_badges = pet->new_badges, now = pet->now;
    uint16_t generation = pet->generation;
    uint8_t parent_traits = pet->traits;

    pet_init(pet, &tune, now, seed);
    pet->farm = farm;
    pet->places = places;
    pet->badges = badges;
    pet->new_badges = new_badges;
    pet->generation = generation < UINT16_MAX ? generation + 1 : generation;

    /* One trait comes down from the parent; the other is the egg's own. */
    int parents = 0;
    for (int t = 0; t < TRAIT_COUNT; t++) {
        parents += (parent_traits >> t) & 1;
    }
    if (parents > 0) {
        int pick = (int)(next_random(pet) % (uint32_t)parents);
        uint8_t inherited = 0;
        for (int t = 0; t < TRAIT_COUNT && !inherited; t++) {
            if (((parent_traits >> t) & 1) && pick-- == 0) {
                inherited = (uint8_t)(1u << t);
            }
        }
        if (!(pet->traits & inherited)) {
            /* It takes the place of one of the two the egg drew. */
            int drop = (int)(next_random(pet) % PET_TRAITS_PER_PET);
            for (int t = 0; t < TRAIT_COUNT; t++) {
                if (((pet->traits >> t) & 1) && drop-- == 0) {
                    pet->traits &= (uint8_t)~(1u << t);
                    break;
                }
            }
            pet->traits |= inherited;
        }
    }

    pet_badge_award(pet, BADGE_RETIRED);
    pet_badges_check(pet, false);
    return true;
}

const char *pet_trait_name(pet_trait_t t)
{
    switch (t) {
    case TRAIT_LAZY:     return "lazy";
    case TRAIT_GREEDY:   return "greedy";
    case TRAIT_CURIOUS:  return "curious";
    case TRAIT_PLAYFUL:  return "playful";
    case TRAIT_SHY:      return "shy";
    case TRAIT_CHEERFUL: return "cheerful";
    default:             return "?";
    }
}

float pet_feed(pet_t *pet, food_type_t type, float base_value)
{
    if (type >= FOOD_TYPE_COUNT) {
        return 0;
    }
    const pet_tuning_t *t = &pet->tune;

    /* Diminishing returns: full, then half, then a quarter... within a day. */
    float value = base_value;
    for (int i = 0; i < pet->eaten_today[type] && value > 0.01f; i++) {
        value *= t->repeat_factor;
    }

    pet_day_t *d = &pet->today;
    if (pet->eaten_today[type] == 0) {
        pet->pending.new_types_today++;
        d->types++;
    }
    if (pet->eaten_today[type] < UINT16_MAX) {
        pet->eaten_today[type]++;
    }
    if (pet->tasted[type] == 0) {
        pet->pending.new_foods_ever++;
        d->new_foods++;
    }
    pet->tasted[type]++;

    if (type == pet->craving) {
        value *= t->craving_value_factor;
        pet->pending.craving_satisfied = true;
        d->flags |= DAY_CRAVING;
        pet->craving = PET_NO_CRAVING;
        pet->next_craving = pet->now + t->craving_every_s;
    }

    if (type == FOOD_WIFI_OPEN) {
        pet->junk_today++;
        if (pet->junk_today == t->junk_limit + 1) {
            pet->pending.became_queasy = true;
        }
    }

    /* Diet shapes evolution whether or not there was room in the stomach. */
    pet->points[type] += value;
    if (pet_food_group(type) == GROUP_NETWORKS) {
        places_fed(&pet->places, value);    /* where it came from decides Homebody or Roamer */
    }

    /* The day's record, for the diet chart and the journal. */
    if (d->foods < UINT16_MAX) {
        d->foods++;
    }
    uint32_t pts = d->group_pts[pet_food_group(type)] + (uint32_t)(value * 10 + 0.5f);
    d->group_pts[pet_food_group(type)] = pts > UINT16_MAX ? UINT16_MAX : (uint16_t)pts;
    if (d->best == FOOD_TYPE_COUNT || base_value > pet->today_best_value) {
        d->best = (uint8_t)type;
        pet->today_best_value = base_value;
    }
    if (base_value >= RARE_VALUE && pet->rare_today < UINT8_MAX) {
        pet->rare_today++;
    }
    if (type == FOOD_WIFI_HIDDEN && base_value >= 5) {
        pet_badge_award(pet, BADGE_LUCKY_FIND);
    }

    pet->activity += 1;
    pet->pending.foods++;
    pet->pending.value += value;
    if (pet->pending.best == FOOD_TYPE_COUNT || base_value > pet->pending.best_value) {
        pet->pending.best = type;
        pet->pending.best_value = base_value;
    }
    return value;
}

static void pick_craving(pet_t *pet)
{
    /*
     * Crave something not eaten yet today. Mostly something the pet has
     * tasted before, so the craving can be met; now and then something it
     * has never had, as a nudge to go exploring. Crumbs are never craved.
     */
    bool want_new = (next_random(pet) % 100) < CRAVE_NEW_PCT;
    food_type_t pool[FOOD_TYPE_COUNT];
    int n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++) {
        for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
            if (i == FOOD_BLE_UNKNOWN || pet->eaten_today[i] > 0) {
                continue;
            }
            if ((pet->tasted[i] == 0) == want_new) {
                pool[n++] = (food_type_t)i;
            }
        }
        want_new = !want_new;   /* nothing of that kind: try the other */
    }
    if (n == 0) {
        return;                 /* it has had everything today */
    }
    pet->craving = pool[next_random(pet) % n];
    pet->craving_until = pet->now + pet->tune.craving_lasts_s;
}

/* Midnight: today's record joins the week and a fresh one begins. */
void pet_day_roll(pet_t *pet, uint32_t new_day)
{
    if (pet->today.day != 0 && pet->today.day != new_day) {
        pet_badges_check(pet, true);
        memmove(&pet->history[1], &pet->history[0],
                sizeof(pet->history) - sizeof(pet->history[0]));
        pet->history[0] = pet->today;
    }
    memset(&pet->today, 0, sizeof(pet->today));
    pet->today.day = new_day;
    pet->today.best = FOOD_TYPE_COUNT;
    pet->today_best_value = 0;
    pet->rare_today = 0;
    memset(pet->eaten_today, 0, sizeof(pet->eaten_today));
    pet->junk_today = 0;
}

bool pet_is_queasy(const pet_t *pet)
{
    return pet->now < pet->queasy_until;
}
#define is_queasy pet_is_queasy

#define is_night pet_is_night
bool pet_is_night(const pet_t *pet)
{
    const pet_tuning_t *t = &pet->tune;
    int hour = (int)(pet->now % 86400) / 3600;
    return t->night_start_hour > t->night_end_hour
               ? (hour >= t->night_start_hour || hour < t->night_end_hour)
               : (hour >= t->night_start_hour && hour < t->night_end_hour);
}

/* Where the mood settles when nothing is happening. */
static float resting_mood(const pet_t *pet)
{
    float fullness = pet->stomach / pet->tune.stomach_max;   /* 0..1 */
    float rest = 30 + 40 * fullness;
    if (is_queasy(pet)) {
        rest -= MOOD_QUEASY;
    }
    return rest;
}

static void advance(pet_t *pet, uint32_t dt)
{
    const pet_tuning_t *t = &pet->tune;
    float hours = dt / 3600.0f;

    pet->stomach = clampf(pet->stomach - t->drain_per_hour * hours, 0, t->stomach_max);

    /* Busy-ness halves every 10 minutes. */
    pet->activity *= powf(0.5f, dt / (float)ACTIVITY_HALF_LIFE_S);
    if (pet->activity < 0.01f) {
        pet->activity = 0;
    }

    float rest = resting_mood(pet);
    float step = t->mood_drift_per_hour * hours;
    if (pet->mood > rest) {
        pet->mood = pet->mood - step < rest ? rest : pet->mood - step;
    } else {
        pet->mood = pet->mood + step > rest ? rest : pet->mood + step;
    }

    /* An egg is never hungry; after that the day's record notes it. */
    if (pet->stage != STAGE_EGG && pet->stomach < t->hungry_below) {
        pet->today.flags |= DAY_HUNGRY;
    }

    /* Static: near empty for two days straight. Never fatal, always recoverable. */
    if (pet->stomach < t->empty_below) {
        if (pet->empty_since == 0) {
            pet->empty_since = pet->now ? pet->now : 1;
        } else if (pet->now - pet->empty_since >= t->static_after_s) {
            pet->is_static = true;
        }
    } else {
        pet->empty_since = 0;
        if (pet->stomach >= t->static_recover_at) {
            pet->is_static = false;
        }
    }
}

bool pet_tick(pet_t *pet, uint32_t now, pet_meal_t *meal)
{
    const pet_tuning_t *t = &pet->tune;
    if (now < pet->now) {
        now = pet->now;   /* clock went backwards: stand still rather than undo */
    }

    /* Step in chunks so a long gap (device off) still crosses midnights properly. */
    while (pet->now < now) {
        uint32_t next_midnight = (pet->now / 86400 + 1) * 86400;
        uint32_t to = now < next_midnight ? now : next_midnight;
        uint32_t dt = to - pet->now;
        pet->now = to;
        advance(pet, dt);

        if (pet->now / 86400 != pet->day) {
            pet->day = pet->now / 86400;
            pet_day_roll(pet, pet->day);
        }
    }

    if (pet->craving != PET_NO_CRAVING && pet->now >= pet->craving_until) {
        pet->craving = PET_NO_CRAVING;
        pet->next_craving = pet->now + t->craving_every_s;
    }
    if (pet->craving == PET_NO_CRAVING && pet->now >= pet->next_craving && !is_night(pet)) {
        pick_craving(pet);
        if (pet->craving == PET_NO_CRAVING) {
            pet->next_craving = pet->now + 3600;
        }
    }

    /* Eating moment: the whole batch at once, but not more often than the gap. */
    if (pet->pending.foods == 0 ||
        (pet->meals > 0 && pet->now - pet->last_meal < t->meal_gap_s)) {
        pet_grow(pet);   /* the days may have run out with the meals already done */
        if (pet->evolved) {
            pet_badges_check(pet, false);
        }
        return false;
    }

    pet_meal_t m = pet->pending;
    float room = t->stomach_max - pet->stomach;
    m.wasted = m.value > room ? m.value - room : 0;
    pet->stomach = clampf(pet->stomach + m.value, 0, t->stomach_max);

    float lift = m.value * t->mood_per_value + m.new_types_today * t->mood_per_new_type;
    if (m.craving_satisfied) {
        lift += t->craving_mood;
    }
    pet->mood = clampf(pet->mood + lift, 0, MOOD_MAX);

    if (m.became_queasy) {
        pet->queasy_until = pet->now + t->queasy_s;
        pet->today.flags |= DAY_QUEASY;
    }
    if (pet->today.meals < UINT16_MAX) {
        pet->today.meals++;
    }

    pet->meals++;
    pet->last_meal = pet->now;
    memset(&pet->pending, 0, sizeof(pet->pending));
    pet->pending.best = FOOD_TYPE_COUNT;
    pet_grow(pet);
    pet_badges_check(pet, false);

    if (meal) {
        *meal = m;
    }
    return true;
}

uint32_t pet_scan(pet_t *pet, const pet_scan_t *scan, place_kind_t *badge_kind)
{
    int home_before = places_home(&pet->places);
    uint32_t ev = places_scan(&pet->places, scan, pet->now, pet->day, badge_kind);
    if (ev & PLACE_EV_NEW) {
        pet->mood = clampf(pet->mood + pet->tune.new_place_mood, 0, 100);
        pet->today.flags |= DAY_NEW_PLACE;
        if (home_before >= 0) {
            pet->today.flags |= DAY_OUT;
        }
    }
    if ((ev & PLACE_EV_ARRIVED) && home_before >= 0 && pet->places.here != home_before) {
        pet->mood = clampf(pet->mood + pet->tune.visit_mood, 0, 100);
        pet->today.flags |= DAY_OUT;
    }
    if (ev & (PLACE_EV_BADGE | PLACE_EV_OFFGRID)) {
        pet->today.flags |= DAY_BADGE_PLACE;
    }
    return ev;
}

void pet_pat(pet_t *pet)
{
    pet->mood = clampf(pet->mood + MOOD_PAT, 0, MOOD_MAX);
}

pet_mood_t pet_mood(const pet_t *pet)
{
    const pet_tuning_t *t = &pet->tune;
    if (is_queasy(pet)) {
        return PET_MOOD_QUEASY;
    }
    if (pet->stomach < t->hungry_below) {
        return PET_MOOD_HUNGRY;
    }
    if (is_night(pet)) {
        return PET_MOOD_SLEEPY;
    }
    if (pet->mood >= 70) {
        return PET_MOOD_HAPPY;
    }
    if (pet->mood < 40) {
        return PET_MOOD_BORED;
    }
    return PET_MOOD_CONTENT;
}

const char *pet_craving_text(const pet_t *pet)
{
    switch (pet->craving) {
    case FOOD_WIFI_SECURED: return "wants a plain snack";
    case FOOD_WIFI_WPA3:    return "wants something crunchy";
    case FOOD_WIFI_OPEN:    return "wants junk food";
    case FOOD_WIFI_PRINTER: return "wants something papery";
    case FOOD_WIFI_HOTSPOT: return "wants something on the move";
    case FOOD_WIFI_HIDDEN:  return "wants something mysterious";
    case FOOD_WIFI_CAR:
    case FOOD_BLE_CAR:      return "wants something fast";
    case FOOD_BLE_PHONE:    return "wants something chatty";
    case FOOD_BLE_WATCH:    return "wants something ticking";
    case FOOD_BLE_EARBUDS:
    case FOOD_BLE_SPEAKER:  return "wants something musical";
    case FOOD_BLE_TRACKER:  return "wants something lost";
    case FOOD_BLE_TV:       return "wants something flickery";
    default:                return "";
    }
}
