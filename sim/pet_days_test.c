/* Desktop checks for day records, the journal, badges and the newer save format. Exit code 0 = pass. */
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

static void meal(pet_t *p, food_type_t type, float value)
{
    pet_feed(p, type, value);
    pet_tick(p, p->now + 61, NULL);
}

/* ---- A version 2 save, written out by hand as the last firmware wrote it ---- */

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

static uint8_t *u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); return p + 4; }
static uint8_t *f32(uint8_t *p, float v) { memcpy(p, &v, 4); return p + 4; }

static size_t old_save(uint8_t *buf, uint32_t now)
{
    uint8_t *p = buf;
    p = u32(p, 0x54455056u);
    p = u32(p, 2);
    p = u32(p, FOOD_TYPE_COUNT);
    p = u32(p, now);
    p = u32(p, now - 3 * DAY);      /* born */
    p = u32(p, 12345);              /* rng */
    p = u32(p, (1u << TRAIT_LAZY) | (1u << TRAIT_PLAYFUL));
    p = f32(p, 16.0f);
    p = f32(p, 55.0f);
    p = u32(p, now / DAY);
    p = u32(p, 1);                  /* junk today */
    p = u32(p, 0);
    p = u32(p, 74);                 /* meals */
    p = u32(p, now - 100);
    p = u32(p, FOOD_TYPE_COUNT);    /* no craving */
    p = u32(p, 0);
    p = u32(p, now + HOUR);
    p = u32(p, 0);
    p = u32(p, 0);
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        bool had = i == FOOD_WIFI_SECURED || i == FOOD_BLE_PHONE || i == FOOD_WIFI_CAR;
        p = u32(p, i == FOOD_WIFI_SECURED ? 4 : 0);
        p = u32(p, had ? 30 : 0);
        p = f32(p, had ? 12.5f : 0);
    }
    p = u32(p, STAGE_HATCHLING);
    p = u32(p, FORM_NONE);
    p = u32(p, now - DAY);
    p = u32(p, 5);
    p = u32(p, 0);
    p = u32(p, crc32(buf, (size_t)(p - buf)));
    return (size_t)(p - buf);
}

int main(void)
{
    /* ---- A day's record ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 3);
        CHECK(p.today.day == T0 / DAY && p.today.foods == 0, "a new pet starts today's record");
        meal(&p, FOOD_WIFI_SECURED, 1);
        meal(&p, FOOD_BLE_SPEAKER, 3);
        pet_feed(&p, FOOD_BLE_PHONE, 1);
        pet_feed(&p, FOOD_BLE_PHONE, 1);
        pet_tick(&p, p.now + 61, NULL);
        CHECK(p.today.foods == 4 && p.today.meals == 3, "foods and meals are counted");
        CHECK(p.today.types == 3 && p.today.new_foods == 3, "kinds, and first tastes");
        CHECK(p.today.best == FOOD_BLE_SPEAKER, "the best find is remembered");
        CHECK(p.today.group_pts[GROUP_MUSIC] == 30 && p.today.group_pts[GROUP_NETWORKS] == 10,
              "points go to the right group");
        CHECK(p.today.group_pts[GROUP_GADGETS] == 15, "repeats count for less, as in the stomach");

        /* Midnight: the record joins the week. */
        pet_tick(&p, (T0 / DAY + 1) * DAY + 5, NULL);
        CHECK(p.history[0].day == T0 / DAY && p.history[0].foods == 4, "yesterday is kept");
        CHECK(p.today.day == T0 / DAY + 1 && p.today.foods == 0, "and today starts empty");

        /* Ten more days: only the last seven stay. */
        for (int d = 0; d < 10; d++) {
            meal(&p, FOOD_WIFI_SECURED, 1);
            pet_tick(&p, (p.now / DAY + 1) * DAY + 5, NULL);
        }
        CHECK(p.history[0].day == p.today.day - 1 && p.history[6].day == p.today.day - 7,
              "a week of days, newest first");
    }

    /* ---- Flags ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 3);
        for (int i = 0; i < 6; i++) {
            meal(&p, FOOD_WIFI_SECURED, 1);     /* hatch */
        }
        CHECK(p.today.flags & DAY_GREW, "hatching is noted");
        for (int i = 0; i < 7; i++) {
            pet_feed(&p, FOOD_WIFI_OPEN, 2);
        }
        pet_tick(&p, p.now + 61, NULL);
        CHECK(p.today.flags & DAY_QUEASY, "so is a queasy day");
        CHECK(!(p.today.flags & DAY_HUNGRY), "not hungry so far");
        pet_tick(&p, p.now + 12 * HOUR, NULL);
        CHECK((p.today.flags | p.history[0].flags) & DAY_HUNGRY, "and going hungry");
    }

    /* ---- Journal ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 3);
        char a[200], b[200];
        pet_journal_entry(&p, &p.today, true, a, sizeof(a));
        CHECK(strstr(a, "Nothing to eat yet") != NULL, "an empty today says so");

        pet_day_t d = {.day = 500, .foods = 40, .meals = 9, .types = 4, .best = FOOD_BLE_CAR};
        d.group_pts[GROUP_CARS] = 50;
        d.group_pts[GROUP_NETWORKS] = 100;
        pet_journal_entry(&p, &d, false, a, sizeof(a));
        pet_journal_entry(&p, &d, false, b, sizeof(b));
        CHECK(strlen(a) > 20 && !strcmp(a, b), "the same day always reads the same");
        CHECK(strstr(a, "drive") || strstr(a, "fast") || strstr(a, "ars") || strstr(a, "rushed"),
              "a day with a car mentions the drive");

        d.flags = DAY_QUEASY;
        pet_journal_entry(&p, &d, false, a, sizeof(a));
        CHECK(strstr(a, "junk") || strstr(a, "tummy") || strstr(a, "open networks"),
              "a queasy day says so");

        /* Every combination fits the buffer and ends cleanly. */
        bool ok = true;
        for (uint32_t day = 1; day < 400; day++) {
            pet_day_t e = {.day = day, .foods = (uint16_t)(day % 120), .meals = 5,
                           .best = (uint8_t)(day % (FOOD_TYPE_COUNT + 1)),
                           .new_foods = (uint8_t)(day % 3 == 0), .flags = (uint8_t)(day % 16)};
            e.group_pts[day % GROUP_COUNT] = 80;
            char small[96];
            memset(small, 'x', sizeof(small));
            pet_journal_entry(&p, &e, day % 2, small, sizeof(small));
            size_t n = strlen(small);
            ok = ok && n > 0 && n < sizeof(small);
        }
        CHECK(ok, "entries always fit and are never empty");
    }

    /* ---- Badges ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 3);
        CHECK(p.badges == 0, "a new pet has none");
        meal(&p, FOOD_WIFI_SECURED, 1);
        CHECK(pet_has_badge(&p, BADGE_FIRST_BITE), "first bite");
        CHECK(pet_take_new_badges(&p) == (1u << BADGE_FIRST_BITE), "and it is reported once");
        CHECK(pet_take_new_badges(&p) == 0, "only once");

        meal(&p, FOOD_BLE_CAR, 5);
        CHECK(pet_has_badge(&p, BADGE_FIRST_CAR), "first car");
        CHECK(!pet_has_badge(&p, BADGE_RARE_3), "one rare food is not three");
        meal(&p, FOOD_WIFI_CAR, 5);
        meal(&p, FOOD_BLE_SPEAKER, 3);
        CHECK(pet_has_badge(&p, BADGE_RARE_3), "three rare foods in a day");

        CHECK(!pet_has_badge(&p, BADGE_LUCKY_FIND), "a plain hidden network is not lucky");
        meal(&p, FOOD_WIFI_HIDDEN, 1);
        CHECK(!pet_has_badge(&p, BADGE_LUCKY_FIND), "still not");
        meal(&p, FOOD_WIFI_HIDDEN, 5);
        CHECK(pet_has_badge(&p, BADGE_LUCKY_FIND), "a hidden network worth 5 is");

        meal(&p, FOOD_BLE_PHONE, 1);
        CHECK(pet_has_badge(&p, BADGE_BALANCED_DAY), "five food groups in a day");
        CHECK(pet_has_badge(&p, BADGE_HATCHED), "hatched along the way");

        for (int i = 0; i < FOOD_FIRST_BLE; i++) {
            meal(&p, (food_type_t)i, 1);
        }
        CHECK(pet_has_badge(&p, BADGE_ALL_WIFI) && !pet_has_badge(&p, BADGE_FULL_DEX),
              "every Wi-Fi food");
        for (int i = FOOD_FIRST_BLE; i < FOOD_TYPE_COUNT; i++) {
            meal(&p, (food_type_t)i, 1);
        }
        CHECK(pet_has_badge(&p, BADGE_ALL_BLUETOOTH) && pet_has_badge(&p, BADGE_FULL_DEX),
              "and the whole food-dex");

        for (int i = 0; i < 40; i++) {
            pet_feed(&p, FOOD_BLE_UNKNOWN, 0.5f);
        }
        pet_tick(&p, p.now + 61, NULL);
        CHECK(pet_has_badge(&p, BADGE_FEAST), "fifty foods in a day");

        /* A week of steady meals, never hungry. */
        CHECK(!pet_has_badge(&p, BADGE_WELL_FED_WEEK), "no week yet");
        for (int d = 0; d < 8; d++) {
            uint32_t midnight = (p.now / DAY + 1) * DAY;
            while (p.now + 2 * HOUR < midnight) {
                /* A bit of everything: repeats of one food soon stop filling it. */
                for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
                    pet_feed(&p, (food_type_t)i, food_value((food_type_t)i, 7));
                }
                pet_tick(&p, p.now + 2 * HOUR, NULL);
            }
            pet_tick(&p, midnight + 5, NULL);
        }
        CHECK(pet_has_badge(&p, BADGE_WELL_FED_WEEK), "a week without going hungry");
        CHECK(pet_has_badge(&p, BADGE_MEALS_100) == (p.meals >= 100), "meal count badge matches");
        for (int b = 0; b < BADGE_COUNT; b++) {
            CHECK(pet_badge_name((pet_badge_t)b)[0] && pet_badge_how((pet_badge_t)b)[0],
                  "every badge has a name and a how-to");
        }
    }

    /* ---- Saving ---- */
    {
        pet_t p, q;
        pet_init(&p, NULL, T0, 3);
        for (int d = 0; d < 3; d++) {
            meal(&p, FOOD_WIFI_SECURED, 1);
            meal(&p, FOOD_BLE_SPEAKER, 3);
            pet_tick(&p, (p.now / DAY + 1) * DAY + 5, NULL);
        }
        meal(&p, FOOD_BLE_WATCH, 2);
        snprintf(p.name, sizeof(p.name), "Biscuit");

        uint8_t buf[PET_SAVE_MAX];
        size_t len = pet_save(&p, buf, sizeof(buf));
        CHECK(len > 0 && len <= PET_SAVE_MAX, "the bigger save still fits");
        CHECK(pet_load(&q, NULL, buf, len, p.now), "and loads");
        CHECK(!strcmp(q.name, "Biscuit"), "name kept");
        CHECK(q.badges == p.badges && q.new_badges == p.new_badges, "badges kept");
        CHECK(!memcmp(&q.today, &p.today, sizeof(p.today)), "today's record kept");
        CHECK(!memcmp(q.history, p.history, sizeof(p.history)), "the week kept");

        /* Off over two midnights: the saved "today" becomes history. */
        CHECK(pet_load(&q, NULL, buf, len, p.now + 2 * DAY), "loads two days later");
        CHECK(q.history[0].day == p.today.day && q.history[0].foods == p.today.foods,
              "the day it was switched off is filed");
        CHECK(q.today.day == p.today.day + 2 && q.today.foods == 0, "today starts fresh");
    }

    /* ---- The save from before these screens existed ---- */
    {
        uint8_t buf[PET_SAVE_MAX];
        size_t len = old_save(buf, T0);
        pet_t q;
        CHECK(pet_load(&q, NULL, buf, len, T0 + 500), "a version 2 save still loads");
        CHECK(q.meals == 74 && q.stage == STAGE_HATCHLING, "meals and stage kept");
        CHECK(pet_has_trait(&q, TRAIT_LAZY) && pet_has_trait(&q, TRAIT_PLAYFUL), "traits kept");
        CHECK(q.today.day == T0 / DAY && q.today.foods == 4, "today's count carried over");
        CHECK(q.name[0] == 0, "no name yet");
        CHECK(pet_has_badge(&q, BADGE_FIRST_BITE) && pet_has_badge(&q, BADGE_HATCHED) &&
                  pet_has_badge(&q, BADGE_FIRST_CAR),
              "badges already earned are credited");
        CHECK(q.new_badges == 0, "quietly: no fanfare for old news");
        CHECK(!pet_has_badge(&q, BADGE_MEALS_100), "and nothing it has not earned");
    }

    printf(failures ? "%d check(s) failed\n" : "all day, journal and badge checks passed\n", failures);
    return failures ? 1 : 0;
}
