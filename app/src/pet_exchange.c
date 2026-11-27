/*
 * Parcels: what travels when a pet moves to another device, or when two
 * devices bring their farms level (see "Moving between devices" in
 * pet_core.h).
 *
 * A parcel is, in order, little-endian and unpadded:
 *
 *   magic "BFEX", format number
 *   realm (1 byte), has a pet (1 byte), two spare bytes
 *   the pet's id and its hops as it travels (0 and 0 with no pet)
 *   when it was made, by the sender's clock
 *   the collection: its length, then
 *       badges, as bits
 *       the farm: pets ever retired, forms retired as bits, residents shown,
 *                 then each resident
 *       place badges: how many kinds, then each kind
 *       the off the grid tier
 *   the pet: its length (0 with no pet), then a save as pet_save() writes
 *            it, with the habitats taken out
 *   a CRC of all the above
 *
 * The two parts have their own lengths so a build can step over what a
 * later one adds to the collection. A pet from a later save version is
 * refused by pet_load(), and the whole parcel with it.
 */
#include <string.h>

#include "pet_core.h"

#define PARCEL_MAGIC  0x58454642u   /* "BFEX" */
#define PARCEL_FORMAT 1

uint32_t pet_crc32(const uint8_t *data, size_t len);            /* pet_save.c */
uint32_t pet_save_version(void);                                /* pet_save.c */
void pet_badges_check(pet_t *pet, bool day_ended);              /* pet_badges.c */
void pet_pasture_clear(pet_t *pet, uint32_t seed, bool away);   /* pet_core.c */

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

static void put_u8(cursor_t *c, uint8_t v) { put(c, &v, 1); }
static void put_u32(cursor_t *c, uint32_t v) { put(c, &v, 4); }
static uint8_t get_u8(cursor_t *c) { uint8_t v; get(c, &v, 1); return v; }
static uint32_t get_u32(cursor_t *c) { uint32_t v; get(c, &v, 4); return v; }

/* The owner's collection, as it travels. */
typedef struct {
    uint32_t badges;
    pet_farm_t farm;
    struct {
        uint8_t count;
        uint8_t chains;
        uint16_t chain[PLACE_CHAINS_MAX];
        uint32_t last_day;
    } kind[PLACE_KIND_COUNT];
    uint8_t offgrid_tier;
} collection_t;

static void put_collection(cursor_t *c, const pet_t *pet)
{
    const pet_farm_t *f = &pet->farm;
    put_u32(c, pet->badges);
    put_u32(c, (uint32_t)f->count + f->moved_on);
    put_u32(c, f->forms);
    put_u8(c, f->count);
    for (int i = 0; i < f->count && i < FARM_MAX; i++) {
        const pet_resident_t *r = &f->resident[i];
        put(c, r->name, PET_NAME_MAX);
        put_u8(c, r->form);
        put_u8(c, r->traits);
        put_u8(c, r->favourite);
        put(c, &r->generation, 2);
        put(c, &r->days, 2);
        put_u32(c, r->retired_day);
        put_u32(c, r->meals);
        put_u32(c, r->id);
    }
    put_u32(c, PLACE_KIND_COUNT);
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        put_u8(c, pet->places.kind[k].count);
        put_u8(c, pet->places.kind[k].chains);
        put(c, pet->places.kind[k].chain, pet->places.kind[k].chains * sizeof(uint16_t));
        put_u32(c, pet->places.kind[k].last_day);
    }
    put_u8(c, pet->places.offgrid_tier);
}

/* Reads what this build knows of a collection, and steps over the rest. */
static bool get_collection(cursor_t *c, collection_t *col)
{
    memset(col, 0, sizeof(*col));
    pet_farm_t *f = &col->farm;
    col->badges = get_u32(c) & ((1u << BADGE_COUNT) - 1);    /* a later build may have more */
    uint32_t retired = get_u32(c);
    f->forms = (uint16_t)(get_u32(c) & ((1u << FORM_COUNT) - 1));
    f->count = get_u8(c);
    if (!c->ok || f->count > FARM_MAX || retired < f->count) {
        return false;
    }
    retired -= f->count;
    f->moved_on = (uint16_t)(retired > UINT16_MAX ? UINT16_MAX : retired);
    for (int i = 0; i < f->count; i++) {
        pet_resident_t *r = &f->resident[i];
        get(c, r->name, PET_NAME_MAX);
        r->name[PET_NAME_MAX - 1] = 0;
        r->form = get_u8(c);
        r->traits = get_u8(c);
        r->favourite = get_u8(c);
        get(c, &r->generation, 2);
        get(c, &r->days, 2);
        r->retired_day = get_u32(c);
        r->meals = get_u32(c);
        r->id = get_u32(c);
        if (!c->ok || r->form >= FORM_COUNT || r->traits >= (1u << TRAIT_COUNT) ||
            r->favourite > GROUP_COUNT || r->id == 0) {
            return false;
        }
    }
    uint32_t kinds = get_u32(c);
    if (!c->ok || kinds > 255) {
        return false;
    }
    for (uint32_t k = 0; k < kinds; k++) {
        uint8_t count = get_u8(c), chains = get_u8(c);
        uint16_t chain[PLACE_CHAINS_MAX];
        if (chains > PLACE_CHAINS_MAX) {
            return false;
        }
        get(c, chain, chains * sizeof(uint16_t));
        uint32_t last_day = get_u32(c);
        if (k < PLACE_KIND_COUNT) {     /* a later build may have more kinds */
            col->kind[k].count = count;
            col->kind[k].chains = chains;
            memcpy(col->kind[k].chain, chain, chains * sizeof(uint16_t));
            col->kind[k].last_day = last_day;
        }
    }
    col->offgrid_tier = get_u8(c);
    return c->ok && col->offgrid_tier <= PLACE_TIER_MAX;
}

/* Older first; two retired on the same day go by their ids, so every device agrees. */
static bool resident_before(const pet_resident_t *a, const pet_resident_t *b)
{
    return a->retired_day != b->retired_day ? a->retired_day < b->retired_day : a->id < b->id;
}

/*
 * Joins another farm into this one. Residents are told apart by their
 * ids; the newest FARM_MAX are shown, on every device the same ones. The
 * count of pets ever retired can only be the larger of what each side
 * knew and what the two can show between them: of those that have moved
 * on, nothing is left to tell two apart.
 */
static void farm_merge(pet_farm_t *into, const pet_farm_t *from)
{
    pet_resident_t all[2 * FARM_MAX];
    int n = 0;
    for (int i = 0; i < into->count && i < FARM_MAX; i++) {
        all[n++] = into->resident[i];
    }
    for (int i = 0; i < from->count && i < FARM_MAX; i++) {
        bool have = false;
        for (int j = 0; j < n && !have; j++) {
            have = all[j].id == from->resident[i].id;
        }
        if (!have) {
            all[n++] = from->resident[i];
        }
    }
    for (int i = 1; i < n; i++) {
        pet_resident_t r = all[i];
        int j = i;
        for (; j > 0 && resident_before(&r, &all[j - 1]); j--) {
            all[j] = all[j - 1];
        }
        all[j] = r;
    }

    uint32_t retired = (uint32_t)into->count + into->moved_on;
    uint32_t theirs = (uint32_t)from->count + from->moved_on;
    retired = theirs > retired ? theirs : retired;
    retired = (uint32_t)n > retired ? (uint32_t)n : retired;

    int shown = n > FARM_MAX ? FARM_MAX : n;
    memset(into->resident, 0, sizeof(into->resident));
    memcpy(into->resident, &all[n - shown], (size_t)shown * sizeof(all[0]));
    into->count = (uint8_t)shown;
    retired -= (uint32_t)shown;
    into->moved_on = (uint16_t)(retired > UINT16_MAX ? UINT16_MAX : retired);
    into->forms |= from->forms;
}

/*
 * Joins the place badges of one kind. A kind's count is the different
 * chains visited plus the days a general word counted; the chains are
 * joined as a set, and of the days the larger number is kept.
 */
static void kind_merge(pet_places_t *pl, int k, const collection_t *col)
{
    uint16_t all[2 * PLACE_CHAINS_MAX];
    int n = 0;
    for (int i = 0; i < pl->kind[k].chains; i++) {
        all[n++] = pl->kind[k].chain[i];
    }
    for (int i = 0; i < col->kind[k].chains; i++) {
        bool have = false;
        for (int j = 0; j < n && !have; j++) {
            have = all[j] == col->kind[k].chain[i];
        }
        if (!have) {
            all[n++] = col->kind[k].chain[i];
        }
    }
    for (int i = 1; i < n; i++) {       /* in order, so the same set is the same bytes everywhere */
        uint16_t v = all[i];
        int j = i;
        for (; j > 0 && v < all[j - 1]; j--) {
            all[j] = all[j - 1];
        }
        all[j] = v;
    }

    int days_here = pl->kind[k].count - pl->kind[k].chains;
    int days_there = col->kind[k].count - col->kind[k].chains;
    int days = days_here > days_there ? days_here : days_there;
    int count = n + (days > 0 ? days : 0);
    /* A count can only go up, and a full list of chains has stopped telling them apart. */
    count = pl->kind[k].count > count ? pl->kind[k].count : count;
    count = col->kind[k].count > count ? col->kind[k].count : count;

    pl->kind[k].chains = (uint8_t)(n > PLACE_CHAINS_MAX ? PLACE_CHAINS_MAX : n);
    memset(pl->kind[k].chain, 0, sizeof(pl->kind[k].chain));
    memcpy(pl->kind[k].chain, all, pl->kind[k].chains * sizeof(uint16_t));
    pl->kind[k].count = (uint8_t)(count > UINT8_MAX ? UINT8_MAX : count);
    if (col->kind[k].last_day > pl->kind[k].last_day) {
        pl->kind[k].last_day = col->kind[k].last_day;
    }
}

static void collection_merge(pet_t *pet, const collection_t *col)
{
    pet->badges |= col->badges;
    farm_merge(&pet->farm, &col->farm);
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        kind_merge(&pet->places, k, col);
    }
    if (col->offgrid_tier > pet->places.offgrid_tier) {
        pet->places.offgrid_tier = col->offgrid_tier;
    }
}

/* Too big for a small stack, and only one parcel is ever in hand. */
static pet_t s_work;
static collection_t s_col;

size_t pet_export(const pet_t *pet, pet_realm_t realm, bool with_pet, uint8_t *buf, size_t cap)
{
    if (with_pet && pet->away) {
        return 0;
    }
    cursor_t c = {.p = buf, .end = buf + cap, .ok = true};
    uint16_t hops = (uint16_t)(pet->hops < UINT16_MAX ? pet->hops + 1 : pet->hops);

    put_u32(&c, PARCEL_MAGIC);
    put_u32(&c, PARCEL_FORMAT);
    put_u8(&c, (uint8_t)realm);
    put_u8(&c, with_pet);
    put_u8(&c, 0);
    put_u8(&c, 0);
    put_u32(&c, with_pet ? pet->id : 0);
    put_u32(&c, with_pet ? hops : 0);
    put_u32(&c, pet->now);

    uint8_t *len_at = c.p;
    put_u32(&c, 0);
    put_collection(&c, pet);
    if (!c.ok) {
        return 0;
    }
    uint32_t len = (uint32_t)(c.p - len_at - 4);
    memcpy(len_at, &len, 4);

    len_at = c.p;
    put_u32(&c, 0);
    if (with_pet && c.ok) {
        /*
         * The pet as a save, without the habitats. What it ate among them
         * goes along as two numbers, for the Homebody or Roamer question.
         */
        s_work = *pet;
        pet_places_t *pl = &s_work.places;
        float total = pl->away_points + pl->carried_total, top = pl->carried_top;
        for (int i = 0; i < HABITAT_MAX; i++) {
            if (pl->habitat[i].prints) {
                total += pl->habitat[i].net_points;
                top = pl->habitat[i].net_points > top ? pl->habitat[i].net_points : top;
            }
        }
        places_init(pl);
        pl->carried_total = total;
        pl->carried_top = top;
        memset(&s_work.farm, 0, sizeof(s_work.farm));   /* the collection has it */
        s_work.new_badges = 0;
        s_work.left_id = 0;
        s_work.left_hops = 0;
        s_work.hops = hops;

        size_t n = pet_save(&s_work, c.p, (size_t)(c.end - c.p));
        if (n == 0) {
            return 0;
        }
        c.p += n;
        len = (uint32_t)n;
        memcpy(len_at, &len, 4);
    }
    if (!c.ok) {
        return 0;
    }
    put_u32(&c, pet_crc32(buf, (size_t)(c.p - buf)));
    return c.ok ? (size_t)(c.p - buf) : 0;
}

pet_import_t pet_import(pet_t *pet, pet_realm_t realm, const uint8_t *buf, size_t len,
                        uint32_t now, bool *pet_arrived)
{
    if (pet_arrived) {
        *pet_arrived = false;
    }
    if (len < 32) {
        return PET_IMPORT_DAMAGED;
    }
    uint32_t crc;
    memcpy(&crc, buf + len - 4, 4);
    if (pet_crc32(buf, len - 4) != crc) {
        return PET_IMPORT_DAMAGED;
    }
    cursor_t c = {.p = (uint8_t *)buf, .end = buf + len - 4, .ok = true};
    if (get_u32(&c) != PARCEL_MAGIC) {
        return PET_IMPORT_DAMAGED;
    }
    uint32_t format = get_u32(&c);
    if (format > PARCEL_FORMAT) {
        return PET_IMPORT_NEWER;
    }
    if (format < 1) {
        return PET_IMPORT_DAMAGED;
    }
    uint8_t their_realm = get_u8(&c), has_pet = get_u8(&c);
    get_u8(&c);
    get_u8(&c);
    get_u32(&c);    /* the pet's id and hops: the save says them again */
    get_u32(&c);
    get_u32(&c);    /* when it was made */
    if (!c.ok || has_pet > 1) {
        return PET_IMPORT_DAMAGED;
    }
    if (their_realm != (uint8_t)realm) {
        return PET_IMPORT_WRONG_REALM;
    }

    uint32_t part = get_u32(&c);
    if (!c.ok || part > (size_t)(c.end - c.p)) {
        return PET_IMPORT_DAMAGED;
    }
    cursor_t cc = {.p = c.p, .end = c.p + part, .ok = true};
    if (!get_collection(&cc, &s_col)) {
        return PET_IMPORT_DAMAGED;
    }
    c.p += part;

    part = get_u32(&c);
    if (!c.ok || part != (size_t)(c.end - c.p) || (part == 0) != (has_pet == 0)) {
        return PET_IMPORT_DAMAGED;
    }

    if (has_pet) {
        uint32_t version = 0;
        if (part >= 8) {
            memcpy(&version, c.p + 4, 4);
        }
        if (version > pet_save_version()) {
            return PET_IMPORT_NEWER;
        }
        /* Loaded at this device's clock: its timers are carried over the crossing. */
        if (!pet_load(&s_work, &pet->tune, c.p, part, now) || s_work.away) {
            return PET_IMPORT_DAMAGED;
        }
        if (!pet->away && pet->id == s_work.id) {
            return PET_IMPORT_SAME_PET;
        }
        if (!pet->away && pet->stage != STAGE_EGG) {
            return PET_IMPORT_OCCUPIED;
        }
    }

    /* Nothing can refuse it from here on. */
    if (has_pet) {
        float total = s_work.places.carried_total, top = s_work.places.carried_top;
        pet_pasture_clear(pet, 1, true);    /* an unhatched egg gives way */
        s_work.places = pet->places;        /* the habitats are this device's */
        s_work.places.carried_total = total;
        s_work.places.carried_top = top;
        s_work.farm = pet->farm;            /* and so is the collection, until the two are joined below */
        s_work.badges = pet->badges;
        s_work.new_badges = pet->new_badges;
        s_work.left_id = pet->left_id;
        s_work.left_hops = pet->left_hops;
        s_work.away = false;
        *pet = s_work;
        if (pet_arrived) {
            *pet_arrived = true;
        }
    }
    collection_merge(pet, &s_col);
    pet_badges_check(pet, false);   /* the joined farm may complete a badge neither had */
    return PET_IMPORT_OK;
}
