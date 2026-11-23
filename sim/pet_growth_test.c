/* Desktop checks for growth and evolution. Exit code 0 = pass. */
#include <math.h>
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

/* One eating moment of one food, then on to the next minute. */
static void meal(pet_t *p, food_type_t type, float value)
{
    pet_feed(p, type, value);
    pet_tick(p, p->now + 61, NULL);
}

/* Lives through `days` days eating the given foods `per_day` times each day. */
static void live(pet_t *p, int days, const food_type_t *foods, int n_foods, int per_day)
{
    for (int d = 0; d < days; d++) {
        uint32_t start = p->now;
        for (int i = 0; i < per_day; i++) {
            food_type_t t = foods[i % n_foods];
            meal(p, t, food_value(t, 7));
        }
        pet_tick(p, start + DAY, NULL);
    }
}

/* Raises a pet to adulthood on one diet and reports what it became. */
/* The same, for an owner who has picked a favourite food (0 for none). */
static pet_form_t raise_favouring(const food_type_t *foods, int n, int per_day, uint8_t favourite,
                                  pet_form_t *juvenile)
{
    pet_t p;
    pet_init(&p, NULL, T0, 99);
    p.favourite = favourite;
    live(&p, 8, foods, n, per_day);
    if (juvenile) {
        *juvenile = p.form;
    }
    live(&p, 15, foods, n, per_day);
    return p.stage == STAGE_ADULT ? p.form : FORM_NONE;
}

static pet_form_t raise(const food_type_t *foods, int n, int per_day, pet_form_t *juvenile)
{
    pet_t p;
    pet_init(&p, NULL, T0, 99);
    live(&p, 8, foods, n, per_day);
    if (juvenile) {
        *juvenile = p.form;
    }
    live(&p, 15, foods, n, per_day);
    return p.stage == STAGE_ADULT ? p.form : FORM_NONE;
}

int main(void)
{
    /* ---- Egg ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 1);
        CHECK(p.stage == STAGE_EGG && p.form == FORM_NONE, "starts as an egg");
        for (int i = 0; i < 4; i++) {
            meal(&p, FOOD_WIFI_SECURED, 1);
        }
        CHECK(p.stage == STAGE_EGG, "four meals: still an egg");
        pet_tick(&p, p.now + 10 * DAY, NULL);
        CHECK(p.stage == STAGE_EGG, "an egg does not hatch on time alone");
        meal(&p, FOOD_WIFI_SECURED, 1);
        CHECK(p.stage == STAGE_HATCHLING, "hatches on its fifth meal");
        CHECK(pet_take_evolved(&p), "hatching is reported");
        CHECK(!pet_take_evolved(&p), "and only once");
    }

    /* ---- Meals and time both count ---- */
    {
        const food_type_t wifi[] = {FOOD_WIFI_SECURED, FOOD_WIFI_WPA3, FOOD_WIFI_HIDDEN};

        pet_t p;
        pet_init(&p, NULL, T0, 2);
        live(&p, 1, wifi, 3, 5);
        CHECK(p.stage == STAGE_HATCHLING, "hatched on day one");

        /* Plenty of meals, but only three days old. */
        live(&p, 2, wifi, 3, 100);
        CHECK(p.stage == STAGE_HATCHLING, "meals alone do not end a stage");
        float time_done, meals_done;
        pet_growth_progress(&p, &time_done, &meals_done);
        CHECK(meals_done == 1 && time_done < 0.5f, "progress shows meals done, time not");

        /* A hatchling left in a drawer: time passes, nothing eaten. */
        pet_t q;
        pet_init(&q, NULL, T0, 3);
        live(&q, 1, wifi, 3, 5);
        pet_tick(&q, q.now + 30 * DAY, NULL);
        CHECK(q.stage == STAGE_HATCHLING, "time alone does not end a stage: it waits");
        pet_growth_progress(&q, &time_done, &meals_done);
        CHECK(time_done == 1 && meals_done < 0.1f, "progress shows time done, meals not");
        /* Then it is taken out again and fed: it moves on as soon as the meals are in. */
        live(&q, 1, wifi, 3, 70);
        CHECK(q.stage == STAGE_JUVENILE, "resumes once it has eaten enough");

        live(&p, 6, wifi, 3, 20);
        CHECK(p.stage == STAGE_JUVENILE, "a week and sixty meals make a juvenile");
        live(&p, 15, wifi, 3, 20);
        CHECK(p.stage == STAGE_ADULT, "two more weeks make an adult");
        CHECK(!p.retire_ready, "an adult has nine weeks ahead of it");
        live(&p, 64, wifi, 3, 12);
        CHECK(p.stage == STAGE_ADULT && p.retire_ready, "then it is ready to retire");
    }

    /* ---- The juvenile split ---- */
    {
        const food_type_t wifi[] = {FOOD_WIFI_SECURED, FOOD_WIFI_WPA3, FOOD_WIFI_PRINTER};
        const food_type_t ble[] = {FOOD_BLE_PHONE, FOOD_BLE_UNKNOWN, FOOD_BLE_TRACKER};
        pet_form_t juvenile;
        raise(wifi, 3, 12, &juvenile);
        CHECK(juvenile == FORM_GRAZER, "mostly Wi-Fi: Grazer");
        raise(ble, 3, 12, &juvenile);
        CHECK(juvenile == FORM_HUNTER, "mostly Bluetooth: Hunter");

        pet_t p;
        pet_init(&p, NULL, T0, 4);
        CHECK(pet_form_leaning(&p) == FORM_GRAZER, "with nothing eaten the tie goes to Grazer");
        meal(&p, FOOD_BLE_SPEAKER, 3);
        CHECK(pet_form_leaning(&p) == FORM_HUNTER, "the hint follows the diet");
    }

    /* ---- Adult forms ---- */
    {
        const food_type_t home[] = {FOOD_WIFI_SECURED, FOOD_WIFI_WPA3, FOOD_WIFI_PRINTER};
        CHECK(raise(home, 3, 12, NULL) == FORM_HOMEBODY, "a few networks a day: Homebody");
        CHECK(raise(home, 3, 120, NULL) == FORM_ROAMER, "dozens of networks a day: Roamer");

        const food_type_t scav[] = {FOOD_WIFI_OPEN, FOOD_WIFI_HOTSPOT, FOOD_WIFI_SECURED};
        CHECK(raise(scav, 3, 12, NULL) == FORM_SCAVENGER, "open networks and hotspots: Scavenger");

        const food_type_t music[] = {FOOD_BLE_SPEAKER, FOOD_BLE_EARBUDS, FOOD_BLE_UNKNOWN};
        CHECK(raise(music, 3, 12, NULL) == FORM_SINGER, "speakers and earbuds: Singer");

        const food_type_t cars[] = {FOOD_BLE_CAR, FOOD_BLE_UNKNOWN, FOOD_BLE_TRACKER};
        CHECK(raise(cars, 3, 12, NULL) == FORM_RACER, "cars: Racer");

        /* A car's Wi-Fi is car food too: it leads to the Hunter, and on to the Racer. */
        CHECK(pet_food_group(FOOD_WIFI_CAR) == GROUP_CARS, "a car hotspot is in the cars group");
        const food_type_t drive[] = {FOOD_WIFI_CAR, FOOD_BLE_UNKNOWN, FOOD_BLE_TRACKER};
        pet_form_t drive_branch;
        CHECK(raise(drive, 3, 12, &drive_branch) == FORM_RACER && drive_branch == FORM_HUNTER,
              "car hotspots alone: a Hunter, then a Racer");

        const food_type_t gadgets[] = {FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_UNKNOWN};
        CHECK(raise(gadgets, 3, 12, NULL) == FORM_GLOWBUG, "phones and watches: Glowbug");

        const food_type_t crumbs[] = {FOOD_BLE_UNKNOWN, FOOD_BLE_TRACKER, FOOD_BLE_TV};
        CHECK(raise(crumbs, 3, 12, NULL) == FORM_GLOWBUG, "a Hunter with no speciality: Glowbug");

        /* ---- The owner's favourite food counts several times over toward the form ---- */
        {
            /* Mostly Wi-Fi, with the owner's own earbuds heard now and then. */
            const food_type_t flat[] = {FOOD_WIFI_SECURED, FOOD_WIFI_SECURED, FOOD_WIFI_WPA3, FOOD_BLE_EARBUDS};
            pet_form_t branch;
            pet_form_t plain = raise_favouring(flat, 4, 12, 0, &branch);
            CHECK(branch == FORM_GRAZER && (plain == FORM_HOMEBODY || plain == FORM_ROAMER),
                  "no favourite: a Wi-Fi flat raises a Wi-Fi pet");
            CHECK(raise_favouring(flat, 4, 12, GROUP_MUSIC + 1, &branch) == FORM_SINGER && branch == FORM_HUNTER,
                  "music as its favourite: the same flat raises a Singer");

            /* Among Hunters, phones are everywhere and cars are few. */
            /* A second helping of a food in one day is worth half, so it is kinds that count, not crowds. */
            const food_type_t street[] = {FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_PHONE, FOOD_BLE_WATCH,
                                          FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_PHONE, FOOD_BLE_WATCH,
                                          FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_PHONE, FOOD_BLE_WATCH,
                                          FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_PHONE, FOOD_BLE_CAR};
            pet_form_t street_plain = raise_favouring(street, 16, 16, 0, NULL);
            pet_form_t street_cars = raise_favouring(street, 16, 16, GROUP_CARS + 1, NULL);
            if (street_plain != FORM_GLOWBUG || street_cars != FORM_RACER) {
                printf("  (street: plain %s, favouring cars %s)\n", pet_form_name(street_plain),
                       pet_form_name(street_cars));
            }
            CHECK(street_plain == FORM_GLOWBUG, "no favourite: phones win");
            CHECK(street_cars == FORM_RACER, "cars as its favourite: a Racer");

            /* It bends the form only: the pet's real diet, and Prism, are untouched. */
            pet_t p;
            pet_init(&p, NULL, T0, 5);
            meal(&p, FOOD_WIFI_SECURED, 1);
            meal(&p, FOOD_BLE_EARBUDS, 2);
            float before[GROUP_COUNT], after[GROUP_COUNT];
            pet_group_points(&p, before);
            p.favourite = GROUP_MUSIC + 1;
            pet_group_points(&p, after);
            CHECK(!memcmp(before, after, sizeof(before)), "the diet itself reads the same with a favourite");

            /* A favourite with no weight, or one out of range, changes nothing. */
            pet_tuning_t tune = pet_tuning_default();
            tune.favourite_weight = 1;
            pet_init(&p, &tune, T0, 99);
            p.favourite = GROUP_MUSIC + 1;
            live(&p, 8, flat, 4, 12);
            CHECK(p.form == FORM_GRAZER, "a weight of 1 is no favour at all");

            /* It is saved with the pet, and a new egg starts with none. */
            uint8_t buf[PET_SAVE_MAX];
            pet_t q;
            p.favourite = GROUP_CARS + 1;
            size_t len = pet_save(&p, buf, sizeof(buf));
            CHECK(len > 0 && pet_load(&q, NULL, buf, len, p.now) && q.favourite == GROUP_CARS + 1,
                  "the favourite is kept across a save");
        }

        /* A juvenile's branch holds: a Grazer that turns to Bluetooth late stays on the Wi-Fi side. */
        pet_t p;
        pet_init(&p, NULL, T0, 5);
        live(&p, 8, home, 3, 12);
        CHECK(p.form == FORM_GRAZER, "Grazer juvenile");
        live(&p, 15, music, 3, 12);
        CHECK(p.stage == STAGE_ADULT && (p.form == FORM_HOMEBODY || p.form == FORM_ROAMER ||
                                         p.form == FORM_SCAVENGER || p.form == FORM_PRISM),
              "a Grazer never becomes a Hunter's adult form");
    }

    /* ---- Prism ---- */
    {
        const food_type_t everything[] = {
            FOOD_WIFI_SECURED, FOOD_WIFI_OPEN, FOOD_WIFI_HOTSPOT, FOOD_BLE_SPEAKER,
            FOOD_BLE_EARBUDS,  FOOD_BLE_CAR,   FOOD_BLE_PHONE,    FOOD_BLE_WATCH,
            FOOD_BLE_TRACKER,  FOOD_BLE_TV,    FOOD_WIFI_PRINTER, FOOD_BLE_UNKNOWN,
        };
        CHECK(raise(everything, 12, 24, NULL) == FORM_PRISM, "a balanced, varied diet: Prism");

        /* Balanced but narrow: not enough different foods tasted. */
        const food_type_t narrow[] = {FOOD_WIFI_SECURED, FOOD_WIFI_OPEN, FOOD_BLE_SPEAKER,
                                      FOOD_BLE_CAR, FOOD_BLE_PHONE, FOOD_BLE_TV};
        CHECK(raise(narrow, 6, 12, NULL) != FORM_PRISM, "six foods is too narrow for Prism");
    }

    /* ---- Speed setting: a whole life in an hour ---- */
    {
        pet_tuning_t fast = pet_tuning_default();
        fast.growth_speed = 2016;
        fast.meal_gap_s = 1;
        pet_t p;
        pet_init(&p, &fast, T0, 6);
        int evolutions = 0;
        /* 84 days at x2016 is exactly an hour; the egg's handful of meals adds a moment more. */
        for (uint32_t s = 0; s < 3700 && !p.retire_ready; s += 5) {
            pet_feed(&p, (s / 5) % 2 ? FOOD_WIFI_SECURED : FOOD_WIFI_WPA3, 1);
            pet_tick(&p, T0 + s, NULL);
            evolutions += pet_take_evolved(&p);
        }
        CHECK(evolutions == 3, "hatches, grows and matures");
        CHECK(p.stage == STAGE_ADULT && p.retire_ready, "a full life takes about an hour at x2016");
    }

    /* ---- Changing the speed mid-life keeps the pet's age and its place in the stage ---- */
    {
        const food_type_t home[] = {FOOD_WIFI_SECURED, FOOD_WIFI_WPA3, FOOD_BLE_PHONE};
        pet_t p;
        pet_init(&p, NULL, T0, 8);
        live(&p, 10, home, 3, 12);
        CHECK(p.stage == STAGE_JUVENILE && pet_take_evolved(&p), "a juvenile, a few days into its stage");
        int d0, dn0, m0, mn0, d1, dn1, m1, mn1;
        float t0, f0, t1, f1;
        pet_growth_counts(&p, &d0, &dn0, &m0, &mn0);
        pet_growth_progress(&p, &t0, &f0);
        uint32_t age0 = p.now - p.born;

        pet_set_growth_speed(&p, 4);
        pet_growth_counts(&p, &d1, &dn1, &m1, &mn1);
        pet_growth_progress(&p, &t1, &f1);
        CHECK(d1 == d0 && dn1 == dn0, "slow to fast: the same days done, of the same days");
        {
            uint32_t left = pet_growth_seconds_left(&p);
            float want = (1 - t0) * p.tune.juvenile_days * DAY / 4;
            CHECK(fabsf((float)left - want) < 60, "but a quarter of the real time left in the stage");
        }
        CHECK(fabsf(t1 - t0) < 0.001f, "and the same share of the stage's time");
        CHECK(mn1 == (mn0 + 3) / 4 && fabsf(f1 - f0) <= 1.0f / (float)mn1,
              "a quarter of the meals to eat, with the same share eaten");
        int64_t drift = (int64_t)(p.now - p.born) * 4 - (int64_t)age0;
        CHECK(drift >= -4 && drift <= 4, "it is the age it was");
        CHECK(p.stage == STAGE_JUVENILE && !pet_take_evolved(&p), "and it did not jump a stage");
        pet_tick(&p, p.now + DAY, NULL);
        pet_growth_counts(&p, &d1, &dn1, &m1, &mn1);
        CHECK(d1 == d0 + 4, "from then on a day counts as four");

        pet_set_growth_speed(&p, 1);
        pet_growth_counts(&p, &d1, &dn1, &m1, &mn1);
        CHECK(d1 == d0 + 4 && mn1 == mn0, "fast to slow keeps those days too");
        pet_set_growth_speed(&p, 1);
        pet_growth_counts(&p, &d1, &dn1, &m1, &mn1);
        CHECK(d1 == d0 + 4, "the same speed again changes nothing");

        /* An egg has no days to keep and always needs its handful of meals. */
        pet_t egg;
        pet_init(&egg, NULL, T0, 9);
        meal(&egg, FOOD_WIFI_SECURED, 5);
        pet_set_growth_speed(&egg, 4);
        pet_growth_counts(&egg, &d1, &dn1, &m1, &mn1);
        CHECK(m1 == 1 && mn1 == egg.tune.egg_meals, "an egg keeps its meals");
        CHECK(pet_growth_seconds_left(&egg) == 0, "and has no time to wait out");

        /* The speed is in the save, so waking at another speed keeps the age as well. */
        pet_t q;
        uint8_t buf[PET_SAVE_MAX];
        pet_growth_progress(&p, &t0, &f0);
        pet_growth_counts(&p, &d1, &dn1, &m1, &mn1);
        size_t len = pet_save(&p, buf, sizeof(buf));
        pet_tuning_t medium = pet_tuning_default();
        medium.growth_speed = 2;
        CHECK(len > 0 && pet_load(&q, &medium, buf, len, p.now + 5 * DAY), "saved slow, woken on medium");
        pet_growth_progress(&q, &t1, &f1);
        pet_growth_counts(&q, &d0, &dn0, &m0, &mn0);
        CHECK(q.tune.growth_speed == 2 && fabsf(t1 - t0) < 0.001f && d0 == d1,
              "it is the age it was when saved");
        CHECK(pet_load(&q, NULL, buf, len, p.now), "woken at the speed it was saved at");
        CHECK(q.born == p.born && q.stage_started == p.stage_started &&
                  q.stage_meal_base == p.stage_meal_base, "nothing moves");
    }

    /* ---- Growth survives a save, and time off does not age the pet ---- */
    {
        const food_type_t ble[] = {FOOD_BLE_PHONE, FOOD_BLE_WATCH, FOOD_BLE_UNKNOWN};
        pet_t p, q;
        pet_init(&p, NULL, T0, 7);
        live(&p, 10, ble, 3, 12);
        CHECK(p.stage == STAGE_JUVENILE && p.form == FORM_HUNTER, "a Hunter juvenile to save");

        uint8_t buf[PET_SAVE_MAX];
        size_t len = pet_save(&p, buf, sizeof(buf));
        CHECK(len > 0, "saves");
        CHECK(pet_load(&q, NULL, buf, len, p.now + 40 * DAY), "loads after forty days off");
        CHECK(q.stage == STAGE_JUVENILE && q.form == FORM_HUNTER, "stage and form restored");
        float t1, t2;
        pet_growth_progress(&p, &t1, NULL);
        pet_growth_progress(&q, &t2, NULL);
        CHECK(fabsf(t1 - t2) < 0.001f, "the stage did not advance while the device was off");
        pet_tick(&q, q.now + HOUR, NULL);
        CHECK(q.stage == STAGE_JUVENILE, "and it does not jump ahead on waking");
    }

    /* ---- Retirement, the farm and the next generation ---- */
    {
        pet_tuning_t fast = pet_tuning_default();
        fast.growth_speed = 2016;       /* a life in an hour */
        fast.meal_gap_s = 1;
        pet_t p;
        pet_init(&p, &fast, T0, 21);
        CHECK(p.generation == 1 && p.farm.count == 0, "the first egg: generation 1, an empty farm");
        CHECK(!pet_can_retire(&p) && !pet_retire(&p, 5), "an egg cannot retire");
        uint32_t t = T0;
        for (; t < T0 + 3700 && !pet_can_retire(&p); t += 5) {
            pet_feed(&p, ((t - T0) / 5) % 2 ? FOOD_WIFI_SECURED : FOOD_WIFI_WPA3, 1);
            pet_tick(&p, t, NULL);
        }
        CHECK(pet_can_retire(&p), "an adult whose life is complete can retire");
        pet_tick(&p, t + 600, NULL);
        CHECK(pet_can_retire(&p) && p.stage == STAGE_ADULT, "and waits for the owner: it does not go by itself");

        snprintf(p.name, sizeof(p.name), "Elder");
        p.places.kind[PLACE_STORE].count = 3;
        p.places.habitat[0].prints = 2;
        const pet_t old = p;
        CHECK(pet_retire(&p, 99), "it retires");
        const pet_resident_t *r = &p.farm.resident[0];
        CHECK(p.farm.count == 1 && !strcmp(r->name, "Elder") && r->form == old.form &&
                  r->traits == old.traits && r->generation == 1 && r->meals == old.meals,
              "the farm keeps its name, form, traits, generation and meals");
        CHECK(r->days >= 84 && r->days <= 110 && r->retired_day == old.day, "and how long it lived");
        CHECK(r->favourite < GROUP_COUNT, "and what it liked best");
        CHECK(p.farm.forms == (1u << old.form), "the form is marked as raised");

        CHECK(p.stage == STAGE_EGG && p.meals == 0 && p.name[0] == 0 && p.generation == 2,
              "an unnamed egg of the second generation takes its place");
        int own = 0;
        for (int i = 0; i < TRAIT_COUNT; i++) {
            own += (p.traits >> i) & 1;
        }
        CHECK(own == PET_TRAITS_PER_PET && (p.traits & old.traits), "with two traits, one of them its parent's");
        CHECK((p.badges & old.badges) == old.badges && pet_has_badge(&p, BADGE_RETIRED),
              "the badges stay, with one more for the retirement");
        CHECK(p.places.kind[PLACE_STORE].count == 3 && p.places.habitat[0].prints == 2,
              "the habitats and place badges stay");
        int tasted = 0;
        for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
            tasted += p.tasted[i] > 0;
        }
        CHECK(tasted == 0 && p.tune.growth_speed == 2016, "its food-dex starts empty; the settings carry on");
        CHECK(!pet_can_retire(&p) && !pet_retire(&p, 5) && p.farm.count == 1, "the egg cannot retire");

        /* One of the parent's traits always comes down, whatever the egg drew. */
        bool always = true;
        for (uint32_t seed = 1; seed < 200; seed++) {
            pet_t q = old;
            pet_retire(&q, seed);
            int n = 0;
            for (int i = 0; i < TRAIT_COUNT; i++) {
                n += (q.traits >> i) & 1;
            }
            always = always && n == PET_TRAITS_PER_PET && (q.traits & old.traits);
        }
        CHECK(always, "for every egg");

        /* The farm in a save, and a save from before there was one. */
        uint8_t buf[PET_SAVE_MAX];
        pet_t q;
        size_t len = pet_save(&p, buf, sizeof(buf));
        CHECK(len > 0 && pet_load(&q, &fast, buf, len, p.now), "a pet with a farm saves and loads");
        CHECK(q.generation == 2 && q.farm.count == 1 && q.farm.forms == p.farm.forms &&
                  !memcmp(&q.farm.resident[0], &p.farm.resident[0], sizeof(pet_resident_t)),
              "with its generation and its resident");

        /* A full farm: the oldest moves on, and the rest keep their order. */
        pet_t f = old;
        f.farm.count = 0;
        for (int n = 0; n < FARM_MAX + 2; n++) {
            f.stage = STAGE_ADULT;
            f.form = FORM_HOMEBODY;
            f.retire_ready = true;
            snprintf(f.name, sizeof(f.name), "Pet%d", n);
            pet_retire(&f, (uint32_t)n + 1);
        }
        CHECK(f.farm.count == FARM_MAX && f.farm.moved_on == 2, "the farm holds twelve; two have moved on");
        CHECK(!strcmp(f.farm.resident[0].name, "Pet2") && !strcmp(f.farm.resident[FARM_MAX - 1].name, "Pet13"),
              "the oldest went first");
        CHECK(f.generation == FARM_MAX + 3 && pet_has_badge(&f, BADGE_GEN_5), "generations count on; the fifth earns a badge");
        len = pet_save(&f, buf, sizeof(buf));
        CHECK(len > 0 && len <= PET_SAVE_MAX && pet_load(&q, &fast, buf, len, f.now) && q.farm.count == FARM_MAX,
              "a full farm fits in a save");

        /* Every ordinary form retired once, in any order: a badge. A Prism is its own. */
        pet_t h = old;
        h.farm.count = 0;
        h.farm.forms = 0;
        h.badges = 0;
        const pet_form_t forms[] = {FORM_HOMEBODY, FORM_ROAMER, FORM_SCAVENGER, FORM_SINGER, FORM_RACER, FORM_GLOWBUG};
        for (int n = 0; n < 6; n++) {
            CHECK(!pet_has_badge(&h, BADGE_ALL_FORMS), "not before the last of the six");
            h.stage = STAGE_ADULT;
            h.form = forms[n];
            h.retire_ready = true;
            pet_retire(&h, (uint32_t)n + 7);
        }
        CHECK(pet_has_badge(&h, BADGE_ALL_FORMS) && !pet_has_badge(&h, BADGE_PRISM), "all six retired: a full house");
        h.stage = STAGE_ADULT;
        h.form = FORM_PRISM;
        h.meals = 10;
        pet_tick(&h, h.now + 61, NULL);
        pet_feed(&h, FOOD_WIFI_SECURED, 1);
        pet_tick(&h, h.now + 61, NULL);
        CHECK(pet_has_badge(&h, BADGE_PRISM), "a Prism earns its badge by growing into one");
    }

    printf(failures ? "%d check(s) failed\n" : "all growth checks passed\n", failures);
    return failures ? 1 : 0;
}
