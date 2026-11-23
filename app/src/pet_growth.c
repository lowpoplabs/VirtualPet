/*
 * Growth and evolution: when the pet moves to its next life stage, and
 * which form its diet turns it into.
 */
#include <math.h>

#include "pet_core.h"

static const pet_group_t GROUP_OF[FOOD_TYPE_COUNT] = {
    [FOOD_WIFI_SECURED] = GROUP_NETWORKS, [FOOD_WIFI_WPA3] = GROUP_NETWORKS,
    [FOOD_WIFI_HIDDEN] = GROUP_NETWORKS,  [FOOD_WIFI_PRINTER] = GROUP_NETWORKS,
    [FOOD_WIFI_OPEN] = GROUP_SCAVENGE,    [FOOD_WIFI_HOTSPOT] = GROUP_SCAVENGE,
    [FOOD_WIFI_CAR] = GROUP_SCAVENGE,
    [FOOD_BLE_SPEAKER] = GROUP_MUSIC,     [FOOD_BLE_EARBUDS] = GROUP_MUSIC,
    [FOOD_BLE_CAR] = GROUP_CARS,
    [FOOD_BLE_PHONE] = GROUP_GADGETS,     [FOOD_BLE_WATCH] = GROUP_GADGETS,
    [FOOD_BLE_UNKNOWN] = GROUP_OTHER,     [FOOD_BLE_TRACKER] = GROUP_OTHER,
    [FOOD_BLE_TV] = GROUP_OTHER,
};

pet_group_t pet_food_group(food_type_t type)
{
    return type < FOOD_TYPE_COUNT ? GROUP_OF[type] : GROUP_OTHER;
}

const char *pet_group_name(pet_group_t group)
{
    static const char *const names[GROUP_COUNT] = {
        "networks", "junk and hotspots", "music", "cars", "phones and watches", "odds and ends",
    };
    return group < GROUP_COUNT ? names[group] : "?";
}

const char *pet_stage_name(pet_stage_t stage)
{
    static const char *const names[STAGE_COUNT] = {"egg", "hatchling", "juvenile", "adult"};
    return stage < STAGE_COUNT ? names[stage] : "?";
}

const char *pet_form_name(pet_form_t form)
{
    static const char *const names[FORM_COUNT] = {
        "", "Grazer", "Hunter", "Homebody", "Roamer", "Scavenger",
        "Singer", "Racer", "Glowbug", "Prism",
    };
    return form < FORM_COUNT ? names[form] : "?";
}

void pet_group_points(const pet_t *pet, float points[GROUP_COUNT])
{
    for (int g = 0; g < GROUP_COUNT; g++) {
        points[g] = 0;
    }
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        points[GROUP_OF[i]] += pet->points[i];
    }
}

/*
 * The group points as the form sees them: the owner's favourite counts
 * several times over. Only the form looks at these; the diet chart, the
 * card and Prism (which is about what it really ate) use the plain ones.
 */
static void form_points(const pet_t *pet, float g[GROUP_COUNT])
{
    pet_group_points(pet, g);
    if (pet->favourite >= 1 && pet->favourite <= PET_FAVOURITE_MAX) {
        g[pet->favourite - 1] *= pet->tune.favourite_weight >= 1 ? pet->tune.favourite_weight : 1;
    }
}

/*
 * Wi-Fi or Bluetooth. Odds and ends (crumbs, trackers, TVs) count on the
 * Bluetooth side. A food eaten again in one day is worth half each time,
 * so a day gives about twice a food's value however much of it there is,
 * and without them the Bluetooth side has too few kinds to keep up.
 */
static pet_form_t juvenile_form(const float g[GROUP_COUNT])
{
    float wifi = g[GROUP_NETWORKS] + g[GROUP_SCAVENGE];
    float ble = g[GROUP_MUSIC] + g[GROUP_CARS] + g[GROUP_GADGETS] + g[GROUP_OTHER];
    return wifi >= ble ? FORM_GRAZER : FORM_HUNTER;
}

static bool is_prism(const pet_t *pet)
{
    float g[GROUP_COUNT];
    pet_group_points(pet, g);
    float total = 0, biggest = 0;
    for (int i = 0; i < GROUP_COUNT; i++) {
        total += g[i];
        if (g[i] > biggest) {
            biggest = g[i];
        }
    }
    int types = 0;
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        types += pet->tasted[i] > 0;
    }
    return total > 0 && biggest <= pet->tune.prism_max_share * total &&
           types >= pet->tune.prism_min_types;
}

static pet_form_t adult_form(const pet_t *pet, pet_form_t branch, const float g[GROUP_COUNT])
{
    if (is_prism(pet)) {
        return FORM_PRISM;
    }
    if (branch == FORM_HUNTER) {
        /* The biggest of the three Hunter groups. A Hunter raised on crumbs is a Glowbug. */
        if (g[GROUP_MUSIC] > g[GROUP_CARS] && g[GROUP_MUSIC] > g[GROUP_GADGETS]) {
            return FORM_SINGER;
        }
        if (g[GROUP_CARS] > g[GROUP_GADGETS]) {
            return FORM_RACER;
        }
        return FORM_GLOWBUG;
    }

    if (g[GROUP_SCAVENGE] > g[GROUP_NETWORKS]) {
        return FORM_SCAVENGER;
    }
    /*
     * Homebody or Roamer: did the networks come from one place or from all
     * over? Once it knows a habitat and has eaten enough to say, that is
     * read straight off where its network food was eaten.
     */
    float share = places_count(&pet->places) > 0 ? places_top_share(&pet->places) : -1;
    if (share >= 0) {
        return share < pet->tune.roamer_max_home_share ? FORM_ROAMER : FORM_HOMEBODY;
    }
    /*
     * Until then the stand-in is how many plain networks it found per day
     * of its life: the same home gives a handful, getting about gives dozens.
     */
    float found = (float)(pet->tasted[FOOD_WIFI_SECURED] + pet->tasted[FOOD_WIFI_WPA3] +
                          pet->tasted[FOOD_WIFI_HIDDEN]);
    float days = (pet->now - pet->born) / 86400.0f * pet->tune.growth_speed;
    if (days < 1) {
        days = 1;
    }
    return found / days >= pet->tune.roamer_finds_per_day ? FORM_ROAMER : FORM_HOMEBODY;
}

pet_form_t pet_form_leaning(const pet_t *pet)
{
    float g[GROUP_COUNT];
    form_points(pet, g);
    switch (pet->stage) {
    case STAGE_EGG:
    case STAGE_HATCHLING: return juvenile_form(g);
    case STAGE_JUVENILE:  return adult_form(pet, pet->form, g);
    default:              return pet->form;
    }
}

/* What the current stage asks for, with the speed setting applied. */
static void stage_needs(const pet_t *pet, float *seconds, uint32_t *meals)
{
    const pet_tuning_t *t = &pet->tune;
    float days = 0;
    int m = 0;
    switch (pet->stage) {
    case STAGE_EGG:       days = 0;                 m = t->egg_meals;       break;
    case STAGE_HATCHLING: days = t->hatchling_days; m = t->hatchling_meals; break;
    case STAGE_JUVENILE:  days = t->juvenile_days;  m = t->juvenile_meals;  break;
    default:              days = t->adult_days;     m = t->adult_meals;     break;
    }
    float speed = t->growth_speed > 0 ? t->growth_speed : 1;
    *seconds = days * 86400.0f / speed;
    /* An egg always needs its handful of meals; later stages shrink with the speed setting. */
    float scaled = pet->stage == STAGE_EGG ? (float)m : ceilf(m / speed);
    *meals = scaled < 1 ? 1 : (uint32_t)scaled;
}

/* Moves a start time so the same stretch of pet time lies behind it at the new speed. */
static uint32_t respeed(uint32_t now, uint32_t since, double factor)
{
    if (since == 0 || since >= now) {
        return since;
    }
    double age = (double)(now - since) * factor + 0.5;
    return age >= (double)now ? 1 : now - (uint32_t)age;
}

void pet_set_growth_speed(pet_t *pet, float speed)
{
    float old = pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1;
    if (!(speed > 0)) {
        speed = 1;
    }
    float seconds;
    uint32_t needed_before, needed_after;
    stage_needs(pet, &seconds, &needed_before);
    pet->tune.growth_speed = speed;
    if (speed == old) {
        return;
    }
    stage_needs(pet, &seconds, &needed_after);

    double factor = (double)old / (double)speed;
    pet->born = respeed(pet->now, pet->born, factor);
    pet->stage_started = respeed(pet->now, pet->stage_started, factor);
    /* The same share of the stage's meals stays eaten. */
    double eaten = (double)(pet->meals - pet->stage_meal_base) * needed_after / needed_before;
    pet->stage_meal_base = eaten >= (double)pet->meals ? 0 : pet->meals - (uint32_t)eaten;
}

void pet_growth_progress(const pet_t *pet, float *time_done, float *meals_done)
{
    float seconds;
    uint32_t meals;
    stage_needs(pet, &seconds, &meals);
    float t = seconds > 0 ? (pet->now - pet->stage_started) / seconds : 1;
    float m = (float)(pet->meals - pet->stage_meal_base) / (float)meals;
    if (time_done) {
        *time_done = t > 1 ? 1 : t;
    }
    if (meals_done) {
        *meals_done = m > 1 ? 1 : m;
    }
}

uint32_t pet_growth_seconds_left(const pet_t *pet)
{
    float seconds;
    uint32_t meals;
    stage_needs(pet, &seconds, &meals);
    float spent = (float)(pet->now - pet->stage_started);
    return seconds > spent ? (uint32_t)(seconds - spent + 0.5f) : 0;
}

void pet_growth_counts(const pet_t *pet, int *days_done, int *days_needed,
                       int *meals_done, int *meals_needed)
{
    float seconds;
    uint32_t meals;
    stage_needs(pet, &seconds, &meals);
    float speed = pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1;
    /* In pet days, so a sped-up life still counts 7, 14 and 63 of them. */
    int needed = (int)(seconds * speed / 86400.0f + 0.5f);
    int done = (int)((pet->now - pet->stage_started) * speed / 86400.0f);
    uint32_t eaten = pet->meals - pet->stage_meal_base;
    *days_needed = needed;
    *days_done = done > needed ? needed : done;
    *meals_needed = (int)meals;
    *meals_done = eaten > meals ? (int)meals : (int)eaten;
}

bool pet_take_evolved(pet_t *pet)
{
    bool evolved = pet->evolved;
    pet->evolved = false;
    return evolved;
}

/* Called from pet_tick(): moves the pet on when its stage is complete. */
void pet_grow(pet_t *pet)
{
    float time_done, meals_done;
    pet_growth_progress(pet, &time_done, &meals_done);
    if (time_done < 1 || meals_done < 1) {
        return;   /* a pet left in a drawer waits: time alone is not enough */
    }

    float g[GROUP_COUNT];
    form_points(pet, g);
    switch (pet->stage) {
    case STAGE_EGG:
        pet->stage = STAGE_HATCHLING;
        break;
    case STAGE_HATCHLING:
        pet->stage = STAGE_JUVENILE;
        pet->form = juvenile_form(g);
        break;
    case STAGE_JUVENILE:
        pet->form = adult_form(pet, pet->form, g);
        pet->stage = STAGE_ADULT;
        break;
    default:
        /* Retirement, the farm and the next egg are a later build stage. */
        pet->retire_ready = true;
        return;
    }
    pet->stage_started = pet->now;
    pet->stage_meal_base = pet->meals;
    pet->evolved = true;
    pet->today.flags |= DAY_GREW;
}
