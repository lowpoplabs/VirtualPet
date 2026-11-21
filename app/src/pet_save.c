/*
 * Pet save format. Written field by field in a fixed order (little-endian
 * values, no struct padding), with a magic number, a version and a CRC.
 * Bump PET_SAVE_VERSION whenever a field is added, removed or reordered,
 * or when the list of food types changes.
 */
#include <string.h>

#include "pet_core.h"

#define PET_SAVE_MAGIC   0x54455056u   /* "VPET" */
#define PET_SAVE_VERSION 7
/* Version 6 had no generation or farm. It is still read: a first-generation pet with an empty farm. */
#define PET_SAVE_VERSION_NO_FARM 6
/* Version 5 did not say what growth speed the pet was living at. It is still read. */
#define PET_SAVE_VERSION_NO_SPEED 5
/* Version 4 kept the chains counted for place badges as a bit each. It is still read. */
#define PET_SAVE_VERSION_CHAIN_BITS 4
/* Version 3 had no places. It is still read; the pet starts with no habitats. */
#define PET_SAVE_VERSION_NO_PLACES 3
/* Version 1 had no growth fields. It is still read: such a pet resumes as an egg or hatchling. */
#define PET_SAVE_VERSION_NO_GROWTH 1
/* Version 2 had no day records, name or badges. It is still read; those start empty. */
#define PET_SAVE_VERSION_NO_DAYS 2

void pet_day_roll(pet_t *pet, uint32_t new_day);        /* pet_core.c */
void pet_badges_check(pet_t *pet, bool day_ended);      /* pet_badges.c */

typedef struct {
    uint8_t *p;
    const uint8_t *end;
    bool ok;
} cursor_t;

static void put(cursor_t *c, const void *v, size_t n)
{
    if (!c->ok || (size_t)(c->end - c->p) < n) {
        c->ok = false;
        return;
    }
    memcpy(c->p, v, n);
    c->p += n;
}

static void get(cursor_t *c, void *v, size_t n)
{
    if (!c->ok || (size_t)(c->end - c->p) < n) {
        c->ok = false;
        memset(v, 0, n);
        return;
    }
    memcpy(v, c->p, n);
    c->p += n;
}

static void put_u32(cursor_t *c, uint32_t v) { put(c, &v, 4); }
static void put_f32(cursor_t *c, float v) { put(c, &v, 4); }
static uint32_t get_u32(cursor_t *c) { uint32_t v; get(c, &v, 4); return v; }
static float get_f32(cursor_t *c) { float v; get(c, &v, 4); return v; }

static void put_day(cursor_t *c, const pet_day_t *d)
{
    put_u32(c, d->day);
    put(c, &d->foods, 2);
    put(c, &d->meals, 2);
    for (int g = 0; g < GROUP_COUNT; g++) {
        put(c, &d->group_pts[g], 2);
    }
    put(c, &d->best, 1);
    put(c, &d->types, 1);
    put(c, &d->new_foods, 1);
    put(c, &d->flags, 1);
}

static void get_day(cursor_t *c, pet_day_t *d)
{
    d->day = get_u32(c);
    get(c, &d->foods, 2);
    get(c, &d->meals, 2);
    for (int g = 0; g < GROUP_COUNT; g++) {
        get(c, &d->group_pts[g], 2);
    }
    get(c, &d->best, 1);
    get(c, &d->types, 1);
    get(c, &d->new_foods, 1);
    get(c, &d->flags, 1);
}

static void put_places(cursor_t *c, const pet_places_t *pl)
{
    put_u32(c, HABITAT_MAX);
    put_u32(c, PLACE_KIND_COUNT);
    for (int i = 0; i < HABITAT_MAX; i++) {
        const habitat_t *h = &pl->habitat[i];
        put(c, h->print, sizeof(h->print));
        put(c, h->score, sizeof(h->score));
        put(c, &h->prints, 1);
        put(c, h->name, HABITAT_NAME_MAX);
        put(c, &h->visits, 2);
        put_u32(c, h->minutes);
        put_u32(c, h->first_day);
        put_f32(c, h->net_points);
    }
    put_f32(c, pl->away_points);
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        put(c, &pl->kind[k].count, 1);
        put(c, &pl->kind[k].chains, 1);
        put(c, pl->kind[k].chain, pl->kind[k].chains * sizeof(uint16_t));
        put_u32(c, pl->kind[k].last_day);
    }
    put(c, &pl->offgrid_tier, 1);
}

static bool get_places(cursor_t *c, pet_places_t *pl, uint32_t version)
{
    places_init(pl);
    if (get_u32(c) != HABITAT_MAX) {
        return false;
    }
    /* Kinds of place are only ever added at the end; a save from before one was added has fewer. */
    uint32_t kinds = get_u32(c);
    if (kinds > PLACE_KIND_COUNT) {
        return false;
    }
    for (int i = 0; i < HABITAT_MAX; i++) {
        habitat_t *h = &pl->habitat[i];
        get(c, h->print, sizeof(h->print));
        get(c, h->score, sizeof(h->score));
        get(c, &h->prints, 1);
        get(c, h->name, HABITAT_NAME_MAX);
        h->name[HABITAT_NAME_MAX - 1] = 0;
        get(c, &h->visits, 2);
        h->minutes = get_u32(c);
        h->first_day = get_u32(c);
        h->net_points = get_f32(c);
        if (h->prints > HABITAT_PRINTS || !(h->net_points >= 0)) {
            return false;
        }
    }
    pl->away_points = get_f32(c);
    for (int k = 0; k < (int)kinds; k++) {
        get(c, &pl->kind[k].count, 1);
        if (version == PET_SAVE_VERSION_CHAIN_BITS) {
            uint32_t bits = get_u32(c);
            for (int b = 0; b < 32; b++) {
                uint16_t chain = (bits >> b) & 1 ? place_legacy_chain((place_kind_t)k, b) : 0;
                if (chain && pl->kind[k].chains < PLACE_CHAINS_MAX) {
                    pl->kind[k].chain[pl->kind[k].chains++] = chain;
                }
            }
        } else {
            get(c, &pl->kind[k].chains, 1);
            if (pl->kind[k].chains > PLACE_CHAINS_MAX) {
                return false;
            }
            get(c, pl->kind[k].chain, pl->kind[k].chains * sizeof(uint16_t));
        }
        pl->kind[k].last_day = get_u32(c);
    }
    get(c, &pl->offgrid_tier, 1);
    return pl->away_points >= 0 && pl->offgrid_tier <= PLACE_TIER_MAX;
}

/* The generation, then the farm: only the residents there are, each a fixed size. */
static void put_farm(cursor_t *c, const pet_t *pet)
{
    const pet_farm_t *f = &pet->farm;
    put_u32(c, pet->generation);
    put_u32(c, f->moved_on);
    put_u32(c, f->forms);
    put(c, &f->count, 1);
    for (int i = 0; i < f->count && i < FARM_MAX; i++) {
        const pet_resident_t *r = &f->resident[i];
        put(c, r->name, PET_NAME_MAX);
        put(c, &r->form, 1);
        put(c, &r->traits, 1);
        put(c, &r->favourite, 1);
        put(c, &r->generation, 2);
        put(c, &r->days, 2);
        put_u32(c, r->retired_day);
        put_u32(c, r->meals);
    }
}

static bool get_farm(cursor_t *c, pet_t *pet)
{
    pet_farm_t *f = &pet->farm;
    uint32_t generation = get_u32(c), moved_on = get_u32(c), forms = get_u32(c);
    get(c, &f->count, 1);
    if (!c->ok || generation < 1 || generation > UINT16_MAX || moved_on > UINT16_MAX ||
        forms >= (1u << FORM_COUNT) || f->count > FARM_MAX) {
        return false;
    }
    pet->generation = (uint16_t)generation;
    f->moved_on = (uint16_t)moved_on;
    f->forms = (uint16_t)forms;
    for (int i = 0; i < f->count; i++) {
        pet_resident_t *r = &f->resident[i];
        get(c, r->name, PET_NAME_MAX);
        r->name[PET_NAME_MAX - 1] = 0;
        get(c, &r->form, 1);
        get(c, &r->traits, 1);
        get(c, &r->favourite, 1);
        get(c, &r->generation, 2);
        get(c, &r->days, 2);
        r->retired_day = get_u32(c);
        r->meals = get_u32(c);
        if (r->form >= FORM_COUNT || r->traits >= (1u << TRAIT_COUNT) || r->favourite > GROUP_COUNT) {
            return false;
        }
    }
    return c->ok;
}

static uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

size_t pet_save(const pet_t *pet, uint8_t *buf, size_t cap)
{
    cursor_t c = {.p = buf, .end = buf + cap, .ok = true};

    put_u32(&c, PET_SAVE_MAGIC);
    put_u32(&c, PET_SAVE_VERSION);
    put_u32(&c, FOOD_TYPE_COUNT);
    put_u32(&c, pet->now);            /* when this save was made */
    put_u32(&c, pet->born);
    put_u32(&c, pet->rng);
    put_u32(&c, pet->traits);
    put_f32(&c, pet->stomach);
    put_f32(&c, pet->mood);
    put_u32(&c, pet->day);
    put_u32(&c, (uint32_t)pet->junk_today);
    put_u32(&c, pet->queasy_until);
    put_u32(&c, pet->meals);
    put_u32(&c, pet->last_meal);
    put_u32(&c, (uint32_t)pet->craving);
    put_u32(&c, pet->craving_until);
    put_u32(&c, pet->next_craving);
    put_u32(&c, pet->empty_since);
    put_u32(&c, pet->is_static);
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        put_u32(&c, pet->eaten_today[i]);
        put_u32(&c, pet->tasted[i]);
        put_f32(&c, pet->points[i]);
    }
    put_u32(&c, (uint32_t)pet->stage);
    put_u32(&c, (uint32_t)pet->form);
    put_u32(&c, pet->stage_started);
    put_u32(&c, pet->stage_meal_base);
    put_u32(&c, pet->retire_ready);
    put_day(&c, &pet->today);
    put_f32(&c, pet->today_best_value);
    put_u32(&c, pet->rare_today);
    for (int i = 0; i < PET_HISTORY_DAYS; i++) {
        put_day(&c, &pet->history[i]);
    }
    put(&c, pet->name, PET_NAME_MAX);
    put_u32(&c, pet->badges);
    put_u32(&c, pet->new_badges);
    put_places(&c, &pet->places);
    put_f32(&c, pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1);
    put_farm(&c, pet);
    if (!c.ok) {
        return 0;
    }
    put_u32(&c, crc32(buf, (size_t)(c.p - buf)));
    return c.ok ? (size_t)(c.p - buf) : 0;
}

/* Carries a saved timestamp across the time the device was off. 0 means "unset". */
static uint32_t shift_time(uint32_t t, int64_t delta)
{
    if (t == 0 || t == UINT32_MAX) {
        return t;
    }
    int64_t v = (int64_t)t + delta;
    return v < 1 ? 1 : (v >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)v);
}

bool pet_load(pet_t *pet, const pet_tuning_t *tune, const uint8_t *buf, size_t len, uint32_t now)
{
    if (len < 16 || crc32(buf, len - 4) != (uint32_t)(buf[len - 4] | buf[len - 3] << 8 |
                                                      buf[len - 2] << 16 |
                                                      (uint32_t)buf[len - 1] << 24)) {
        return false;
    }
    cursor_t c = {.p = (uint8_t *)buf, .end = buf + len - 4, .ok = true};
    if (get_u32(&c) != PET_SAVE_MAGIC) {
        return false;
    }
    uint32_t version = get_u32(&c);
    if ((version != PET_SAVE_VERSION && version != PET_SAVE_VERSION_NO_FARM &&
         version != PET_SAVE_VERSION_NO_SPEED &&
         version != PET_SAVE_VERSION_CHAIN_BITS &&
         version != PET_SAVE_VERSION_NO_PLACES &&
         version != PET_SAVE_VERSION_NO_DAYS &&
         version != PET_SAVE_VERSION_NO_GROWTH) ||
        get_u32(&c) != FOOD_TYPE_COUNT) {
        return false;
    }

    pet_t p;
    pet_init(&p, tune, now, 1);
    uint32_t saved_at = get_u32(&c);
    p.born = get_u32(&c);
    p.rng = get_u32(&c);
    p.traits = (uint8_t)get_u32(&c);
    p.stomach = get_f32(&c);
    p.mood = get_f32(&c);
    uint32_t saved_day = get_u32(&c);
    p.junk_today = (int)get_u32(&c);
    p.queasy_until = get_u32(&c);
    p.meals = get_u32(&c);
    p.last_meal = get_u32(&c);
    uint32_t craving = get_u32(&c);
    p.craving_until = get_u32(&c);
    p.next_craving = get_u32(&c);
    p.empty_since = get_u32(&c);
    p.is_static = get_u32(&c) != 0;
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        uint32_t eaten = get_u32(&c);
        p.eaten_today[i] = eaten > UINT16_MAX ? UINT16_MAX : (uint16_t)eaten;
        p.tasted[i] = get_u32(&c);
        p.points[i] = get_f32(&c);
    }
    uint32_t stage = STAGE_EGG, form = FORM_NONE;
    if (version == PET_SAVE_VERSION_NO_GROWTH) {
        /* Saved before growth existed: place it by its meals, at the start of that stage. */
        stage = p.meals >= (uint32_t)p.tune.egg_meals ? STAGE_HATCHLING : STAGE_EGG;
        p.stage_started = saved_at;
        p.stage_meal_base = stage == STAGE_HATCHLING ? p.meals : 0;
    } else {
        stage = get_u32(&c);
        form = get_u32(&c);
        p.stage_started = get_u32(&c);
        p.stage_meal_base = get_u32(&c);
        p.retire_ready = get_u32(&c) != 0;
    }
    if (version >= PET_SAVE_VERSION_NO_PLACES) {
        get_day(&c, &p.today);
        p.today_best_value = get_f32(&c);
        p.rare_today = (uint8_t)get_u32(&c);
        for (int i = 0; i < PET_HISTORY_DAYS; i++) {
            get_day(&c, &p.history[i]);
        }
        get(&c, p.name, PET_NAME_MAX);
        p.name[PET_NAME_MAX - 1] = 0;
        p.badges = get_u32(&c) & ((1u << BADGE_COUNT) - 1);
        p.new_badges = get_u32(&c) & p.badges;
    } else {
        /* Saved before days were recorded: today's record starts from the counts it did keep. */
        p.today.day = saved_day;
        for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
            p.today.foods = (uint16_t)(p.today.foods + p.eaten_today[i]);
            p.today.types += p.eaten_today[i] > 0;
        }
    }
    if (version >= PET_SAVE_VERSION_CHAIN_BITS && !get_places(&c, &p.places, version)) {
        return false;
    }
    /* Saved before the speed was recorded: taken to be the speed it wakes at. */
    float speed = p.tune.growth_speed > 0 ? p.tune.growth_speed : 1;
    float saved_speed = version > PET_SAVE_VERSION_NO_SPEED ? get_f32(&c) : speed;
    if (version > PET_SAVE_VERSION_NO_FARM && !get_farm(&c, &p)) {
        return false;
    }
    if (!c.ok || c.p != c.end || stage >= STAGE_COUNT || form >= FORM_COUNT ||
        p.stage_meal_base > p.meals || !(saved_speed > 0 && saved_speed <= 1e6f)) {
        return false;
    }
    p.stage = (pet_stage_t)stage;
    p.form = (pet_form_t)form;

    /* The checksum passed, but refuse values no real pet could have. */
    if (!(p.stomach >= 0 && p.stomach <= p.tune.stomach_max) || !(p.mood >= 0 && p.mood <= 100) ||
        craving > FOOD_TYPE_COUNT || p.rng == 0 || p.traits >= (1u << TRAIT_COUNT)) {
        return false;
    }
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        if (!(p.points[i] >= 0)) {
            return false;
        }
    }
    p.craving = (food_type_t)craving;

    /* Time stood still while the device was off: move every timer along by the gap. */
    int64_t gap = (int64_t)now - (int64_t)saved_at;
    p.born = shift_time(p.born, gap);
    p.stage_started = shift_time(p.stage_started, gap);
    p.queasy_until = shift_time(p.queasy_until, gap);
    p.last_meal = shift_time(p.last_meal, gap);
    p.craving_until = shift_time(p.craving_until, gap);
    p.next_craving = shift_time(p.next_craving, gap);
    p.empty_since = shift_time(p.empty_since, gap);

    /* The calendar did move on, so "today" counters restart on a new day. */
    p.day = now / 86400;
    p.now = now;
    /* Waking at another growth speed: it keeps its age and its place in the stage. */
    p.tune.growth_speed = saved_speed;
    pet_set_growth_speed(&p, speed);
    if (p.today.day != p.day) {
        pet_day_roll(&p, p.day);
    }
    if (version < PET_SAVE_VERSION_NO_PLACES) {
        pet_badges_check(&p, false);    /* credit what it had already done */
        p.new_badges = 0;
    }

    *pet = p;
    return true;
}
