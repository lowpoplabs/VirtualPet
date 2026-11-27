/* Desktop checks for parcels: a pet moving between devices, and farms joined. Exit code 0 = pass. */
#include <stdio.h>
#include <string.h>

#include "pet_core.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

#define DAY  86400u
#define HOUR 3600u
#define T0   (100 * DAY + 10 * HOUR)

static uint8_t s_parcel[PET_PARCEL_MAX];
static uint8_t s_save_a[PET_SAVE_MAX], s_save_b[PET_SAVE_MAX];

static void meal(pet_t *p, food_type_t type, float value)
{
    pet_feed(p, type, value);
    pet_tick(p, p->now + 61, NULL);
}

/* A device with a hatched pet that has eaten a little. */
static void device(pet_t *p, uint32_t seed, const char *name)
{
    pet_init(p, NULL, T0, seed);
    for (int i = 0; i < 8; i++) {
        meal(p, i % 2 ? FOOD_WIFI_SECURED : FOOD_BLE_PHONE, 2);
    }
    snprintf(p->name, sizeof(p->name), "%s", name);
}

/* Makes the pet an adult at the end of its life and retires it. */
static void retire(pet_t *p, pet_form_t form, uint32_t seed)
{
    p->stage = STAGE_ADULT;
    p->form = form;
    p->retire_ready = true;
    pet_tick(p, p->now + DAY, NULL);    /* each retires on a day of its own */
    CHECK(pet_retire(p, seed), "an adult at the end of its life retires");
}

static bool same_save(const pet_t *a, const pet_t *b)
{
    size_t na = pet_save(a, s_save_a, sizeof(s_save_a));
    size_t nb = pet_save(b, s_save_b, sizeof(s_save_b));
    return na > 0 && na == nb && memcmp(s_save_a, s_save_b, na) == 0;
}

static bool same_farm(const pet_t *a, const pet_t *b)
{
    if (a->farm.count != b->farm.count || a->farm.moved_on != b->farm.moved_on ||
        a->farm.forms != b->farm.forms || a->badges != b->badges ||
        a->places.offgrid_tier != b->places.offgrid_tier) {
        return false;
    }
    for (int i = 0; i < a->farm.count; i++) {
        const pet_resident_t *x = &a->farm.resident[i], *y = &b->farm.resident[i];
        if (x->id != y->id || strcmp(x->name, y->name) != 0 || x->form != y->form ||
            x->meals != y->meals || x->retired_day != y->retired_day) {
            return false;
        }
    }
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        if (a->places.kind[k].count != b->places.kind[k].count ||
            a->places.kind[k].chains != b->places.kind[k].chains ||
            memcmp(a->places.kind[k].chain, b->places.kind[k].chain,
                   a->places.kind[k].chains * sizeof(uint16_t)) != 0) {
            return false;
        }
    }
    return true;
}

/* Sends from one device to another, as a file or a link would. */
static pet_import_t send(pet_t *from, pet_t *to, bool with_pet, bool *arrived)
{
    size_t n = pet_export(from, PET_REALM_TEST, with_pet, s_parcel, sizeof(s_parcel));
    if (n == 0) {
        return PET_IMPORT_DAMAGED;
    }
    pet_import_t r = pet_import(to, PET_REALM_TEST, s_parcel, n, to->now, arrived);
    if (r == PET_IMPORT_OK && with_pet) {
        pet_leave(from);
    }
    return r;
}

static void test_there_and_back(void)
{
    static pet_t a, b, before;
    device(&a, 11, "Mossy");
    a.favourite = 2;
    a.walked_m = (uint32_t)(20 * PET_MILE_M);   /* enough for bronze, not yet noticed */
    before = a;

    pet_init(&b, NULL, T0 + 5 * HOUR, 22);      /* another clock, and an unhatched egg */
    pet_leave(&b);
    CHECK(b.away, "a pasture can be empty");
    CHECK(pet_feed(&b, FOOD_WIFI_SECURED, 2) == 0, "an empty pasture eats nothing");
    CHECK(!pet_can_retire(&b), "and has nothing to retire");

    bool arrived = false;
    size_t n = pet_export(&a, PET_REALM_TEST, true, s_parcel, sizeof(s_parcel));
    CHECK(n > 0 && n <= PET_PARCEL_MAX, "a pet fits in a parcel");
    printf("a parcel with a pet is %u bytes\n", (unsigned)n);
    CHECK(!a.away && same_save(&a, &before), "exporting changes nothing");
    CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, n, b.now, &arrived) == PET_IMPORT_OK && arrived,
          "the pet arrives in an empty pasture");
    pet_leave(&a);
    CHECK(a.away && a.left_id == before.id && a.left_hops == 1, "the sender is left empty and remembers who went");
    CHECK(pet_has_badge(&a, BADGE_MOVED) && pet_has_badge(&b, BADGE_MOVED), "a move earns its badge at both ends");
    CHECK(b.walked_m == before.walked_m && b.farm.walk_tier == 1 && a.farm.walk_tier == 0,
          "the pet's miles go with it, and earn their badge where it arrives");
    CHECK(pet_export(&a, PET_REALM_TEST, true, s_parcel, sizeof(s_parcel)) == 0, "an empty pasture has no pet to send");

    CHECK(!b.away && b.id == before.id && b.hops == 1, "it is the same pet, one hop on");
    CHECK(strcmp(b.name, "Mossy") == 0 && b.meals == before.meals && b.traits == before.traits &&
          b.stage == before.stage && b.favourite == 2 && b.generation == before.generation,
          "with its name, meals, traits, stage and favourite");
    CHECK(b.stomach == before.stomach && b.mood == before.mood, "neither hungrier nor fuller for the crossing");
    CHECK(b.now - b.born == before.now - before.born, "and no older, though the clocks differ by hours");
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        CHECK(b.tasted[i] == before.tasted[i] && b.points[i] == before.points[i], "its food-dex comes with it");
    }

    /* And home again. */
    CHECK(send(&b, &a, true, &arrived) == PET_IMPORT_OK && arrived, "it can go back where it came from");
    CHECK(b.away && !a.away && a.id == before.id && a.hops == 2, "two hops, and the other pasture is empty");
    CHECK(a.meals == before.meals && strcmp(a.name, "Mossy") == 0, "still itself");
}

static void test_habitats_stay(void)
{
    static pet_t a, b;
    device(&a, 31, "Fern");
    /* A habitat on device a, with food eaten there and some eaten away. */
    a.places.habitat[0].prints = 2;
    a.places.habitat[0].print[0] = 0x1234;
    a.places.habitat[0].print[1] = 0x5678;
    a.places.habitat[0].net_points = 60;
    snprintf(a.places.habitat[0].name, sizeof(a.places.habitat[0].name), "den");
    a.places.away_points = 20;
    float share = places_top_share(&a.places);

    pet_init(&b, NULL, T0, 32);
    b.places.habitat[3].prints = 1;
    b.places.habitat[3].print[0] = 0x4242;
    snprintf(b.places.habitat[3].name, sizeof(b.places.habitat[3].name), "shed");

    size_t n = pet_export(&a, PET_REALM_TEST, true, s_parcel, sizeof(s_parcel));
    bool found = false;
    for (size_t i = 0; i + 3 <= n; i++) {
        found = found || memcmp(&s_parcel[i], "den", 3) == 0;
    }
    CHECK(n > 0 && !found, "a habitat's name is not in the parcel");
    CHECK(send(&a, &b, true, NULL) == PET_IMPORT_OK, "the egg gives way");
    CHECK(strcmp(b.places.habitat[3].name, "shed") == 0 && b.places.habitat[0].prints == 0,
          "the receiving device keeps its own habitats and gains none");
    CHECK(strcmp(a.places.habitat[0].name, "den") == 0 && a.places.habitat[0].prints == 2,
          "the sending device keeps its habitats");
    CHECK(a.places.habitat[0].net_points == 0 && a.places.away_points == 0, "but not the food the pet ate there");
    float there = places_top_share(&b.places);
    CHECK(there > share - 0.001f && there < share + 0.001f, "the pet is as much a homebody as it was");
}

static void test_refusals(void)
{
    static pet_t a, b, before;
    device(&a, 41, "Bramble");
    device(&b, 42, "Thistle");
    before = b;

    size_t n = pet_export(&a, PET_REALM_TEST, true, s_parcel, sizeof(s_parcel));
    CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, n, b.now, NULL) == PET_IMPORT_OCCUPIED,
          "a hatched pet does not give way");
    CHECK(same_save(&b, &before), "and nothing changed");

    CHECK(pet_import(&b, PET_REALM_RELEASE, s_parcel, n, b.now, NULL) == PET_IMPORT_WRONG_REALM,
          "a release build refuses a test parcel");
    size_t r = pet_export(&a, PET_REALM_RELEASE, false, s_parcel, sizeof(s_parcel));
    CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, r, b.now, NULL) == PET_IMPORT_WRONG_REALM,
          "a test build refuses a real farm");
    CHECK(same_save(&b, &before), "and nothing changed");

    n = pet_export(&a, PET_REALM_TEST, true, s_parcel, sizeof(s_parcel));
    CHECK(pet_import(&a, PET_REALM_TEST, s_parcel, n, a.now, NULL) == PET_IMPORT_SAME_PET,
          "a pet does not arrive where it already is");

    for (size_t at = 0; at < n; at += 7) {
        s_parcel[at] ^= 0x10;
        CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, n, b.now, NULL) == PET_IMPORT_DAMAGED,
              "a changed byte is damage");
        s_parcel[at] ^= 0x10;
    }
    for (size_t cut = 0; cut < n; cut += 13) {
        CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, cut, b.now, NULL) == PET_IMPORT_DAMAGED,
              "a parcel cut short is damage");
    }
    CHECK(same_save(&b, &before), "damage changes nothing");

    /* A later format: the number is the second word, and the checksum the last. */
    uint32_t format = 99, crc = 0xFFFFFFFFu;
    memcpy(&s_parcel[4], &format, 4);
    for (size_t i = 0; i < n - 4; i++) {
        crc ^= s_parcel[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    crc = ~crc;
    memcpy(&s_parcel[n - 4], &crc, 4);
    CHECK(pet_import(&b, PET_REALM_TEST, s_parcel, n, b.now, NULL) == PET_IMPORT_NEWER,
          "a parcel in a later format is refused as newer");
    CHECK(same_save(&b, &before), "and nothing changed");

    CHECK(pet_export(&a, PET_REALM_TEST, true, s_parcel, 100) == 0, "too small a buffer gives nothing");
    CHECK(!pet_new_egg(&b, 5), "no new egg where a pet lives");
}

static void test_new_egg(void)
{
    static pet_t a;
    device(&a, 51, "Clover");
    retire(&a, FORM_ROAMER, 52);
    uint32_t badges = a.badges;
    pet_leave(&a);
    CHECK(a.farm.count == 1 && a.badges == (badges | (1u << BADGE_MOVED)),
          "an empty pasture keeps the farm and the badges, with one more for the move");
    pet_tick(&a, a.now + 3 * DAY, NULL);
    CHECK(a.away && a.meals == 0, "and stays empty as the days go by");
    CHECK(pet_new_egg(&a, 53), "a new egg can be started there");
    CHECK(!a.away && a.stage == STAGE_EGG && a.generation == 1 && a.farm.count == 1 && a.id != 0,
          "a first-generation egg, with the farm as it was");
    meal(&a, FOOD_WIFI_SECURED, 2);
    CHECK(a.meals == 1, "and it eats");
}

static void test_farm_merge(void)
{
    static pet_t a, b, c, ab, ba, twice;
    device(&a, 61, "Ash");
    device(&b, 62, "Birch");
    device(&c, 63, "Cedar");
    retire(&a, FORM_HOMEBODY, 64);
    retire(&a, FORM_SINGER, 65);
    retire(&b, FORM_RACER, 66);
    snprintf(a.name, sizeof(a.name), "Alder");
    retire(&c, FORM_PRISM, 67);
    a.places.kind[PLACE_COFFEE].count = 3;
    a.places.kind[PLACE_COFFEE].chains = 2;
    a.places.kind[PLACE_COFFEE].chain[0] = 7;
    a.places.kind[PLACE_COFFEE].chain[1] = 3;
    b.places.kind[PLACE_COFFEE].count = 2;
    b.places.kind[PLACE_COFFEE].chains = 2;
    b.places.kind[PLACE_COFFEE].chain[0] = 3;
    b.places.kind[PLACE_COFFEE].chain[1] = 9;
    b.places.offgrid_tier = 2;

    /* a into b, and b into a: the same farm. */
    ab = b;
    ba = a;
    CHECK(send(&a, &ab, false, NULL) == PET_IMPORT_OK, "a farm can be sent without the pet");
    CHECK(send(&b, &ba, false, NULL) == PET_IMPORT_OK, "and the other way");
    CHECK(!a.away && !b.away && ab.id == b.id && ab.id != a.id, "no pet moved");
    CHECK(same_farm(&ab, &ba), "either order gives the same farm");
    CHECK(ab.farm.count == 3 && ab.farm.moved_on == 0, "three residents, none twice");
    CHECK(ab.places.kind[PLACE_COFFEE].chains == 3 && ab.places.kind[PLACE_COFFEE].count == 4,
          "three coffee chains between them, and the one general day");
    CHECK(ab.places.offgrid_tier == 2, "the higher off the grid tier");
    CHECK((ab.badges & a.badges) == a.badges && (ab.badges & b.badges) == b.badges, "the badges of both");
    CHECK((ab.farm.forms >> FORM_RACER & 1) && (ab.farm.forms >> FORM_SINGER & 1), "the forms of both");

    /* The same parcel again changes nothing. */
    twice = ab;
    CHECK(send(&a, &twice, false, NULL) == PET_IMPORT_OK && same_farm(&twice, &ab), "joining twice is joining once");
    CHECK(send(&ba, &twice, false, NULL) == PET_IMPORT_OK && same_farm(&twice, &ab), "and so is joining what is already joined");

    /* Three devices, visited in different orders, end the same. */
    static pet_t x, y, z;
    x = a; y = b; z = c;
    send(&x, &y, false, NULL); send(&y, &x, false, NULL);
    send(&y, &z, false, NULL); send(&z, &y, false, NULL);
    send(&y, &x, false, NULL);
    CHECK(same_farm(&x, &y) && same_farm(&y, &z) && x.farm.count == 4, "three devices come to one farm of four");
    static pet_t p, q, r;
    p = a; q = b; r = c;
    send(&r, &p, false, NULL); send(&p, &r, false, NULL);
    send(&q, &r, false, NULL); send(&r, &q, false, NULL);
    send(&r, &p, false, NULL);
    CHECK(same_farm(&p, &x) && same_farm(&q, &x) && same_farm(&r, &x), "by another round of visits, the same one");

    /* More than the farm holds: the same twelve on both, the newest. */
    static pet_t big, other;
    device(&big, 71, "Oak");
    device(&other, 72, "Elm");
    for (int i = 0; i < 9; i++) {
        retire(&big, FORM_ROAMER, 80 + (uint32_t)i);
        retire(&other, FORM_GLOWBUG, 100 + (uint32_t)i);
    }
    static pet_t big2, other2;
    big2 = big;
    other2 = other;
    send(&big, &other2, false, NULL);
    send(&other, &big2, false, NULL);
    CHECK(same_farm(&big2, &other2), "an overfull farm is the same on both");
    CHECK(big2.farm.count == FARM_MAX && big2.farm.moved_on == 6, "twelve shown and six moved on");
    for (int i = 1; i < big2.farm.count; i++) {
        CHECK(big2.farm.resident[i - 1].retired_day <= big2.farm.resident[i].retired_day, "oldest first");
    }
}

static void test_pet_and_farm_together(void)
{
    static pet_t a, b;
    device(&a, 91, "Rowan");
    retire(&a, FORM_SCAVENGER, 92);
    device(&a, 93, "Rowan II");         /* a fresh device would lose the farm: put it back */
    static pet_t keep;
    device(&keep, 91, "Rowan");
    retire(&keep, FORM_SCAVENGER, 92);
    a.farm = keep.farm;
    a.badges |= keep.badges;

    pet_init(&b, NULL, T0, 94);
    retire(&b, FORM_HOMEBODY, 95);      /* b has a resident of its own, and now an egg */
    CHECK(send(&a, &b, true, NULL) == PET_IMPORT_OK, "a pet and a farm arrive together");
    CHECK(strcmp(b.name, "Rowan II") == 0 && b.farm.count == 2, "the pet moved in and the farms are joined");
    CHECK(a.away && a.farm.count == 1, "the sender keeps its own farm until it is sent the other");
    CHECK(send(&b, &a, false, NULL) == PET_IMPORT_OK && a.away && a.farm.count == 2,
          "a farm can be sent to an empty pasture, which stays empty");

    /* A save keeps all of it. */
    static pet_t back;
    size_t n = pet_save(&a, s_save_a, sizeof(s_save_a));
    CHECK(n > 0 && pet_load(&back, NULL, s_save_a, n, a.now), "an empty pasture saves and loads");
    CHECK(back.away && back.left_id == a.left_id && back.left_hops == a.left_hops && same_farm(&back, &a),
          "as an empty pasture, with its farm and who left");
    n = pet_save(&b, s_save_a, sizeof(s_save_a));
    CHECK(n > 0 && pet_load(&back, NULL, s_save_a, n, b.now), "a pet that has moved saves and loads");
    CHECK(!back.away && back.id == b.id && back.hops == 1 && back.farm.resident[0].id == b.farm.resident[0].id,
          "with its id, its hops and its residents' ids");
    printf("a save is %u bytes\n", (unsigned)n);
}

int main(void)
{
    test_there_and_back();
    test_habitats_stay();
    test_refusals();
    test_new_egg();
    test_farm_merge();
    test_pet_and_farm_together();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all exchange checks passed\n");
    return 0;
}
