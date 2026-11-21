/* Desktop checks for the pet core. Exit code 0 = pass. */
#include <math.h>
#include <stdio.h>

#include "pet_core.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

#define NEAR(a, b) (fabsf((a) - (b)) < 0.01f)

#define DAY  86400u
#define HOUR 3600u
/* Start at 10:00 on some day, so "night" does not colour the mood checks. */
#define T0 (100 * DAY + 10 * HOUR)

static pet_t fresh(void)
{
    pet_t p;
    pet_init(&p, NULL, T0, 12345);
    p.next_craving = UINT32_MAX;   /* cravings off unless a test wants them */
    return p;
}

int main(void)
{
    pet_meal_t m;

    /* Diminishing returns: full, half, quarter within a day */
    {
        pet_t p = fresh();
        CHECK(NEAR(pet_feed(&p, FOOD_BLE_SPEAKER, 3), 3.0f), "first is full value");
        CHECK(NEAR(pet_feed(&p, FOOD_BLE_SPEAKER, 3), 1.5f), "second is half");
        CHECK(NEAR(pet_feed(&p, FOOD_BLE_SPEAKER, 3), 0.75f), "third is a quarter");
        CHECK(NEAR(pet_feed(&p, FOOD_BLE_PHONE, 1), 1.0f), "another type is unaffected");
        CHECK(NEAR(p.points[FOOD_BLE_SPEAKER], 5.25f), "points add up for evolution");
    }

    /* A new day resets the repeats */
    {
        pet_t p = fresh();
        pet_feed(&p, FOOD_WIFI_PRINTER, 2);
        pet_feed(&p, FOOD_WIFI_PRINTER, 2);
        pet_tick(&p, T0 + DAY, NULL);
        CHECK(NEAR(pet_feed(&p, FOOD_WIFI_PRINTER, 2), 2.0f), "full value again next day");
    }

    /* Batching: one eating moment for a burst, then a gap */
    {
        pet_t p = fresh();
        pet_feed(&p, FOOD_WIFI_SECURED, 1);
        pet_feed(&p, FOOD_WIFI_CAR, 5);
        pet_feed(&p, FOOD_BLE_UNKNOWN, 0.5f);
        CHECK(pet_tick(&p, T0 + 1, &m), "burst becomes one eating moment");
        CHECK(m.foods == 3 && NEAR(m.value, 6.5f), "batch holds all three");
        CHECK(m.best == FOOD_WIFI_CAR, "the best food is the one shown");
        CHECK(m.new_types_today == 3, "variety counted");
        CHECK(NEAR(p.stomach, 16.5f), "stomach filled");

        pet_feed(&p, FOOD_BLE_PHONE, 1);
        CHECK(!pet_tick(&p, T0 + 30, &m), "no second moment inside the gap");
        CHECK(pet_tick(&p, T0 + 61, &m) && m.foods == 1, "eaten once the gap has passed");
        CHECK(p.meals == 2, "meals counts eating moments");
    }

    /* Stomach: drains 1 per hour, caps at max, never below zero */
    {
        pet_t p = fresh();
        pet_tick(&p, T0 + 3 * HOUR, NULL);
        CHECK(NEAR(p.stomach, 7.0f), "drains one per hour");
        pet_feed(&p, FOOD_WIFI_CAR, 5);
        pet_feed(&p, FOOD_BLE_CAR, 5);
        pet_feed(&p, FOOD_WIFI_HOTSPOT, 3);
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_tick(&p, T0 + 3 * HOUR + 1, &m);
        CHECK(NEAR(p.stomach, 20.0f), "caps at max");
        CHECK(NEAR(m.wasted, 3.0f), "overflow is reported as wasted");
        pet_tick(&p, T0 + 5 * DAY, NULL);
        CHECK(NEAR(p.stomach, 0.0f), "never below zero");
    }

    /* Moods */
    {
        pet_t p = fresh();
        CHECK(pet_mood(&p) == PET_MOOD_CONTENT, "starts content");
        pet_feed(&p, FOOD_WIFI_CAR, 5);
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_feed(&p, FOOD_BLE_TV, 2);
        pet_feed(&p, FOOD_WIFI_PRINTER, 2);
        pet_tick(&p, T0 + 1, NULL);
        CHECK(pet_mood(&p) == PET_MOOD_HAPPY, "a varied meal makes it happy");
        pet_tick(&p, T0 + 8 * HOUR, NULL);   /* 18:00, stomach 12 */
        CHECK(pet_mood(&p) != PET_MOOD_HAPPY, "happiness wears off");
        pet_tick(&p, T0 + 13 * HOUR, NULL);  /* 23:00 */
        CHECK(pet_mood(&p) == PET_MOOD_SLEEPY, "sleepy at night");
        pet_tick(&p, T0 + 23 * HOUR, NULL);  /* 09:00 next day, stomach empty */
        CHECK(pet_mood(&p) == PET_MOOD_HUNGRY, "hungry when the stomach is low");
    }

    /* Junk food: too many open networks in a day */
    {
        pet_t p = fresh();
        bool queasy = false;
        for (int i = 0; i < 6; i++) {
            pet_feed(&p, FOOD_WIFI_OPEN, 2);
            if (pet_tick(&p, T0 + 100 * (i + 1), &m)) {
                queasy = queasy || m.became_queasy;
            }
            if (i == 4) {
                CHECK(pet_mood(&p) != PET_MOOD_QUEASY, "five is still fine");
            }
        }
        CHECK(queasy && pet_mood(&p) == PET_MOOD_QUEASY, "the sixth makes it queasy");
        pet_tick(&p, T0 + 3 * HOUR, NULL);
        CHECK(pet_mood(&p) != PET_MOOD_QUEASY, "queasy passes");
    }

    /* Cravings */
    {
        pet_t p = fresh();
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);          /* tasted, so it can be craved */
        pet_tick(&p, T0 + 1, NULL);
        pet_tick(&p, T0 + DAY, NULL);               /* new day: nothing eaten yet */
        p.next_craving = p.now;
        pet_tick(&p, T0 + DAY + 1, NULL);
        CHECK(p.craving != PET_NO_CRAVING, "a craving appears");
        CHECK(p.craving != FOOD_BLE_UNKNOWN, "never craves crumbs");
        CHECK(pet_craving_text(&p)[0] != '\0', "craving has text");

        p.craving = FOOD_BLE_SPEAKER;
        float mood_before = p.mood;
        CHECK(NEAR(pet_feed(&p, FOOD_BLE_SPEAKER, 3), 4.5f), "craved food is worth more");
        CHECK(pet_tick(&p, T0 + DAY + 100, &m) && m.craving_satisfied, "craving satisfied");
        CHECK(p.craving == PET_NO_CRAVING, "craving cleared");
        CHECK(p.mood > mood_before + 10, "and the pet is pleased");

        /* No new cravings while it sleeps */
        pet_t n = fresh();
        pet_tick(&n, T0 + 14 * HOUR, NULL);          /* midnight */
        n.next_craving = n.now;
        pet_tick(&n, T0 + 15 * HOUR, NULL);          /* 01:00 */
        CHECK(n.craving == PET_NO_CRAVING, "no craving starts at night");
        pet_tick(&n, T0 + 22 * HOUR, NULL);          /* 08:00 */
        CHECK(n.craving != PET_NO_CRAVING, "it starts in the morning instead");

        p.craving = FOOD_WIFI_CAR;
        p.craving_until = p.now + 10;
        pet_tick(&p, p.now + 11, NULL);
        CHECK(p.craving == PET_NO_CRAVING, "an unmet craving expires");
        CHECK(pet_craving_text(&p)[0] == '\0', "no text without a craving");
    }

    /* Static: near empty for two days; recovers with meals; never dies */
    {
        pet_t p = fresh();
        pet_tick(&p, T0 + 10 * HOUR, NULL);          /* stomach hits 0 */
        pet_tick(&p, T0 + 40 * HOUR, NULL);
        CHECK(!p.is_static, "not static after one hungry day");
        for (uint32_t h = 41; h <= 60; h++) {
            pet_tick(&p, T0 + h * HOUR, NULL);
        }
        CHECK(p.is_static, "static after two days near empty");
        pet_feed(&p, FOOD_WIFI_CAR, 5);
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_tick(&p, T0 + 60 * HOUR + 1, NULL);
        pet_tick(&p, T0 + 60 * HOUR + 60, NULL);
        CHECK(!p.is_static, "recovers once fed");

        pet_t q = fresh();
        pet_tick(&q, T0 + 9 * HOUR + 1800, NULL);    /* 0.5 left: near empty */
        pet_feed(&q, FOOD_WIFI_SECURED, 1);
        pet_tick(&q, T0 + 9 * HOUR + 1801, NULL);    /* back above "near empty" */
        pet_tick(&q, T0 + 9 * HOUR + 1900, NULL);
        CHECK(q.empty_since == 0, "a snack restarts the two-day count");
    }

    /* Robustness */
    {
        pet_t p = fresh();
        pet_tick(&p, T0 - 500, NULL);
        CHECK(p.now == T0, "a clock going backwards is ignored");
        CHECK(pet_feed(&p, FOOD_TYPE_COUNT, 3) == 0, "bad food type is ignored");
        float before = p.mood;
        pet_pat(&p);
        CHECK(p.mood > before, "a pat lifts the mood");
    }

    printf(failures ? "%d check(s) failed\n" : "all pet core checks passed\n", failures);
    return failures ? 1 : 0;
}
