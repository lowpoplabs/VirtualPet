/* Desktop checks for the action picker, traits and saving. Exit code 0 = pass. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pet_brain.h"
#include "pet_power.h"
#include "pet_sound.h"

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
#define T0   (100 * DAY + 10 * HOUR)
#define BIT(t) (1u << (t))

/* How often each action comes up over many picks. */
static void tally(const pet_senses_t *s, int count[ACT_COUNT])
{
    memset(count, 0, sizeof(int) * ACT_COUNT);
    uint32_t r = 12345;
    for (int i = 0; i < 20000; i++) {
        r = r * 1664525u + 1013904223u;
        count[pet_pick_default(s, r >> 8)]++;
    }
}

static pet_action_t always_play(const pet_senses_t *s, uint32_t random)
{
    (void)s;
    (void)random;
    return ACT_PLAY;
}

static pet_action_t broken_picker(const pet_senses_t *s, uint32_t random)
{
    (void)s;
    (void)random;
    return (pet_action_t)99;
}

/*
 * One scan for the riding rule: `heard` networks, of which `fresh` were not
 * in the scan before. The rest are carried over from that scan.
 */
static uint16_t s_ride_last[RIDE_PRINTS];
static int s_ride_last_n;
static uint16_t s_ride_next = 1;

static bool ride(pet_ride_t *r, bool carried, int heard, int fresh)
{
    uint16_t now[RIDE_PRINTS];
    if (heard > RIDE_PRINTS) {
        fresh = fresh * RIDE_PRINTS / heard;
        heard = RIDE_PRINTS;
    }
    int kept = heard - fresh < s_ride_last_n ? heard - fresh : s_ride_last_n;
    for (int i = 0; i < heard; i++) {
        now[i] = i < kept ? s_ride_last[i] : s_ride_next++;
    }
    memcpy(s_ride_last, now, sizeof(now));
    s_ride_last_n = heard;
    return pet_ride_scan(r, carried, now, heard, NULL);
}

int main(void)
{
    int n[ACT_COUNT], m[ACT_COUNT];
    const pet_senses_t calm = {.fullness = 0.6f, .mood = 0.5f, .busy = 0.2f, .previous = ACT_IDLE};

    /* ---- Picker ---- */
    {
        tally(&calm, n);
        for (int a = 0; a < ACT_GRUMBLE; a++) {
            CHECK(n[a] > 0, "a calm pet does a bit of everything");
        }
        CHECK(n[ACT_GRUMBLE] == 0, "a fed, well pet never grumbles");

        pet_senses_t s = calm;
        s.night = true;
        tally(&s, n);
        CHECK(n[ACT_NAP] > 10000, "mostly naps at night");

        s = calm;
        s.fullness = 0.05f;
        tally(&s, n);
        CHECK(n[ACT_GRUMBLE] > 4000, "a hungry pet grumbles");

        s = calm;
        s.busy = 1.0f;
        tally(&s, n);
        tally(&calm, m);
        CHECK(n[ACT_LOOK] > m[ACT_LOOK] && n[ACT_SNIFF] > m[ACT_SNIFF],
              "a busy place makes it look and sniff more");

        s = calm;
        s.mood = 0.95f;
        tally(&s, n);
        CHECK(n[ACT_PLAY] > 2 * m[ACT_PLAY], "a happy pet plays more");

        s = calm;
        s.previous = ACT_LOOK;
        tally(&s, n);
        CHECK(n[ACT_LOOK] < m[ACT_LOOK], "less likely to repeat itself");
    }

    /* ---- Riding: told from walking and from sitting ---- */
    {
        pet_ride_t r = {0};
        /* A drive: every scan hears networks that were not there a moment ago, or nothing. */
        CHECK(!ride(&r, true, 3, 3), "one scan of networks going by is not a ride yet");
        CHECK(pet_ride_passing(&r), "but it is worth looking again soon");
        CHECK(!ride(&r, true, 1, 1), "nor two");
        CHECK(ride(&r, true, 4, 3), "a third is");
        CHECK(ride(&r, true, 0, 0), "an empty stretch of road does not end it");
        CHECK(ride(&r, true, 2, 2), "and it goes on");
        CHECK(ride(&r, true, 14, 14), "pulling up somewhere, its networks are all just arrived");
        CHECK(!ride(&r, true, 14, 0), "but they stay, and that ends it");
        CHECK(!pet_ride_passing(&r), "nor is there any hurry now");
        CHECK(!ride(&r, true, 5, 5), "setting off again starts the count again");
        CHECK(!ride(&r, true, 1, 1), "two");
        CHECK(ride(&r, true, 2, 2), "riding once more");
        CHECK(!ride(&r, false, 0, 0), "put down, it is not riding");

        r = (pet_ride_t){0};
        for (int k = 0; k < 3; k++) {
            ride(&r, true, 2, 2);
        }
        bool lasted = true;
        for (int k = 0; k < 5; k++) {
            lasted = lasted && ride(&r, true, 0, 0);
        }
        CHECK(lasted, "a ride lasts through five empty scans");
        CHECK(!ride(&r, true, 0, 0), "but not for ever: parked in the country is not riding");
        CHECK(!ride(&r, true, 1, 1), "and one new network afterwards does not bring it back");

        /*
         * The road it drives every day. Every network on it is an old friend,
         * and it is a ride all the same: out along three streets, a stop, and
         * back along the same three.
         */
        {
            static const uint16_t street[3][3] = {{101, 102, 103}, {201, 202, 203}, {301, 302, 303}};
            static const uint16_t shop[4] = {901, 902, 903, 904};
            int same = -1;
            r = (pet_ride_t){0};
            pet_ride_scan(&r, true, street[0], 3, NULL);
            pet_ride_scan(&r, true, street[1], 3, NULL);
            CHECK(pet_ride_scan(&r, true, street[2], 3, &same) && same == 0, "the way out is a ride");
            bool stopped = true;
            for (int k = 0; k < 20; k++) {
                bool riding = pet_ride_scan(&r, true, shop, 4, &same);
                stopped = stopped && (k == 0 || (!riding && same == 4));
            }
            CHECK(stopped, "the stop is not");
            pet_ride_scan(&r, true, street[2], 3, &same);
            CHECK(same == 0, "the street outside is no longer in mind");
            pet_ride_scan(&r, true, street[1], 3, NULL);
            CHECK(pet_ride_scan(&r, true, street[0], 3, NULL), "and the way home, past networks it knows, is a ride");
        }

        /* One faint network that comes and goes all evening is not three networks going by. */
        {
            static const uint16_t faint[1] = {77};
            r = (pet_ride_t){0};
            bool rode = false;
            for (int k = 0; k < 40; k++) {
                rode = rode || pet_ride_scan(&r, true, faint, k % 3 == 0 ? 1 : 0, NULL);
            }
            CHECK(!rode, "a faint network coming and going is not a ride");
        }

        /* More networks than it can hold in mind: the nearest are what count. */
        {
            uint16_t crowd[40];
            for (int i = 0; i < 40; i++) {
                crowd[i] = (uint16_t)(500 + i);
            }
            int same = -1;
            r = (pet_ride_t){0};
            pet_ride_scan(&r, true, crowd, 40, NULL);
            pet_ride_scan(&r, true, crowd, 40, &same);
            CHECK(same == RIDE_PRINTS, "a crowd of networks is remembered by its nearest");
            CHECK(!pet_ride_scan(&r, true, NULL, 0, &same) && same == 0, "and a scan that heard nothing is taken as such");
        }

        /* Things that are not rides, however long they last. */
        static const struct {
            bool carried;
            int first_heard, first_new;     /* the first scan there */
            int heard, fresh;               /* every scan after it */
            const char *what;
        } NOT[] = {
            {true, 9, 9, 9, 1, "a walk down a street, the last houses still in earshot"},
            {true, 9, 9, 9, 4, "a brisk walk, nearly half the street new each time"},
            {false, 5, 5, 5, 0, "lying on a desk"},
            {true, 50, 50, 50, 0, "standing in a queue in a shop"},
            {true, 54, 19, 54, 4, "ambling round a big shop"},
            {true, 0, 0, 0, 0, "an evening by a fire with no network in earshot"},
            {true, 1, 0, 1, 0, "the same faint network coming and going"},
            {true, 8, 8, 8, 0, "walking into somewhere new and staying"},
        };
        for (size_t i = 0; i < sizeof(NOT) / sizeof(NOT[0]); i++) {
            pet_ride_t n = {0};
            bool rode = ride(&n, NOT[i].carried, NOT[i].first_heard, NOT[i].first_new);
            for (int k = 0; k < 30; k++) {
                rode = rode || ride(&n, NOT[i].carried, NOT[i].heard, NOT[i].fresh);
            }
            if (rode) {
                printf("FAIL: taken for a ride: %s\n", NOT[i].what);
                failures++;
            }
        }
    }

    /* ---- How strongly a trait pulls comes from the tuning numbers ---- */
    {
        pet_senses_t s = calm;
        float plain[ACT_COUNT], lazy[ACT_COUNT], tuned[ACT_COUNT];
        pet_action_scores(&s, plain);
        s.traits = BIT(TRAIT_LAZY);
        pet_action_scores(&s, lazy);
        CHECK(lazy[ACT_NAP] == plain[ACT_NAP] * 2.0f && lazy[ACT_PLAY] == plain[ACT_PLAY] * 0.5f,
              "with no tuning given, the built-in numbers");

        pet_tuning_t tune = pet_tuning_default();
        tune.lazy_nap = 1;
        tune.lazy_idle = 1;
        tune.lazy_play = 1;
        s.tune = &tune;
        pet_action_scores(&s, tuned);
        CHECK(!memcmp(tuned, plain, sizeof(plain)), "a trait tuned to 1 changes nothing");
        tune.lazy_nap = 5;
        pet_action_scores(&s, tuned);
        CHECK(tuned[ACT_NAP] == plain[ACT_NAP] * 5.0f, "and tuned up, it pulls harder");

        /* A greedy pet's grumbling starts where its number says. */
        s = calm;
        s.traits = BIT(TRAIT_GREEDY);
        s.fullness = 0.5f;
        s.tune = &tune;
        pet_action_scores(&s, tuned);
        CHECK(tuned[ACT_GRUMBLE] == 0, "half full: a greedy pet is not grumbling yet");
        tune.greedy_grumble_at = 0.8f;
        pet_action_scores(&s, tuned);
        CHECK(tuned[ACT_GRUMBLE] > 0, "unless it is told to start sooner");

        /* And a pet carries its own numbers to the picker. */
        pet_t pet;
        pet_init(&pet, &tune, 1000, 5);
        CHECK(pet_senses(&pet, ACT_IDLE).tune == &pet.tune, "a pet's senses carry its tuning");
    }

    /* ---- Traits change behaviour ---- */
    {
        pet_senses_t s = calm;
        tally(&calm, m);

        s.traits = BIT(TRAIT_LAZY);
        tally(&s, n);
        CHECK(n[ACT_NAP] > m[ACT_NAP] && n[ACT_PLAY] < m[ACT_PLAY], "lazy: more naps, less play");

        s.traits = BIT(TRAIT_PLAYFUL);
        tally(&s, n);
        CHECK(n[ACT_PLAY] > m[ACT_PLAY], "playful: more play");

        s.traits = BIT(TRAIT_CURIOUS);
        tally(&s, n);
        CHECK(n[ACT_LOOK] > m[ACT_LOOK], "curious: looks about more");

        /* Greedy complains at a stomach level that leaves others content. */
        pet_senses_t peckish = calm;
        peckish.fullness = 0.3f;
        tally(&peckish, m);
        peckish.traits = BIT(TRAIT_GREEDY);
        tally(&peckish, n);
        CHECK(m[ACT_GRUMBLE] == 0 && n[ACT_GRUMBLE] > 0, "greedy: grumbles before it is hungry");

        pet_senses_t hungry = calm;
        hungry.fullness = 0.05f;
        tally(&hungry, m);
        hungry.traits = BIT(TRAIT_CHEERFUL);
        tally(&hungry, n);
        CHECK(n[ACT_GRUMBLE] < m[ACT_GRUMBLE], "cheerful: grumbles less");

        pet_senses_t busy = calm;
        busy.busy = 1.0f;
        busy.mood = 0.9f;
        tally(&busy, m);
        busy.traits = BIT(TRAIT_SHY);
        tally(&busy, n);
        CHECK(n[ACT_PLAY] < m[ACT_PLAY], "shy: plays less in a crowd");
    }

    /* ---- Every egg gets two different traits, and eggs differ ---- */
    {
        int seen_masks = 0;
        uint8_t first = 0;
        for (uint32_t seed = 1; seed <= 40; seed++) {
            pet_t p;
            pet_init(&p, NULL, T0, seed * 7919);
            int bits = 0;
            for (int t = 0; t < TRAIT_COUNT; t++) {
                bits += pet_has_trait(&p, (pet_trait_t)t);
            }
            CHECK(bits == PET_TRAITS_PER_PET, "exactly two traits");
            if (seed == 1) {
                first = p.traits;
            } else if (p.traits != first) {
                seen_masks++;
            }
        }
        CHECK(seen_masks > 10, "different eggs get different traits");
    }

    /* ---- Brain: timing and the swappable picker ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 5);
        pet_brain_t b;
        pet_brain_init(&b, NULL, 77);
        CHECK(pet_brain_step(&b, &p, 1000), "first step picks an action");
        pet_action_t first = b.action;
        CHECK(!pet_brain_step(&b, &p, 1500) && b.action == first, "an action lasts a while");
        int changes = 0;
        for (uint32_t ms = 2000; ms < 300000; ms += 250) {
            changes += pet_brain_step(&b, &p, ms);
        }
        CHECK(changes >= 10, "it moves on to other things");

        pet_brain_init(&b, always_play, 1);
        pet_brain_step(&b, &p, 0);
        CHECK(b.action == ACT_PLAY, "a replacement picker is used");

        pet_brain_init(&b, broken_picker, 1);
        pet_brain_step(&b, &p, 0);
        CHECK(b.action == ACT_IDLE, "a misbehaving picker falls back to idle");

        /* The millisecond counter wrapping must not freeze the pet. */
        pet_brain_init(&b, NULL, 3);
        pet_brain_step(&b, &p, 0xFFFFFF00u);
        changes = 0;
        for (uint32_t i = 1; i <= 2000; i++) {
            changes += pet_brain_step(&b, &p, 0xFFFFFF00u + i * 250);
        }
        CHECK(changes >= 10, "keeps going across the counter wrap");
    }

    /* ---- Busy-ness fades ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 5);
        for (int i = 0; i < 8; i++) {
            pet_feed(&p, FOOD_BLE_UNKNOWN, 0.5f);
        }
        CHECK(NEAR(p.activity, 8.0f), "each food adds to busy-ness");
        pet_tick(&p, T0 + 600, NULL);
        CHECK(fabsf(p.activity - 4.0f) < 0.1f, "halves in ten minutes");
        pet_tick(&p, T0 + 3 * HOUR, NULL);
        CHECK(p.activity == 0, "and is gone after a few hours");
    }

    /* ---- Saving ---- */
    {
        pet_t p;
        pet_init(&p, NULL, T0, 4242);
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_feed(&p, FOOD_WIFI_CAR, 5);
        pet_feed(&p, FOOD_WIFI_OPEN, 2);
        pet_tick(&p, T0 + 5, NULL);
        pet_tick(&p, T0 + 2 * HOUR, NULL);
        p.craving = FOOD_BLE_TV;
        p.craving_until = p.now + 6 * HOUR;
        p.queasy_until = p.now + HOUR;

        uint8_t buf[PET_SAVE_MAX];
        size_t len = pet_save(&p, buf, sizeof(buf));
        CHECK(len > 0 && len <= PET_SAVE_MAX, "save fits the buffer");
        CHECK(pet_save(&p, buf, 40) == 0, "too small a buffer is refused");
        len = pet_save(&p, buf, sizeof(buf));

        /* Straight back in: an exact copy of everything that matters. */
        pet_t q;
        CHECK(pet_load(&q, NULL, buf, len, p.now), "loads");
        CHECK(NEAR(q.stomach, p.stomach) && NEAR(q.mood, p.mood), "stomach and mood restored");
        CHECK(q.traits == p.traits && q.born == p.born && q.meals == p.meals, "identity restored");
        CHECK(NEAR(q.points[FOOD_WIFI_CAR], 5) && q.tasted[FOOD_BLE_SPEAKER] == 1, "diet restored");
        CHECK(q.eaten_today[FOOD_WIFI_OPEN] == 1 && q.junk_today == 1, "today's counts restored");
        CHECK(q.craving == FOOD_BLE_TV, "craving restored");

        /* Off for three days: time stood still for the pet. */
        uint32_t later = p.now + 3 * DAY + 5 * HOUR;
        CHECK(pet_load(&q, NULL, buf, len, later), "loads after a long time off");
        CHECK(NEAR(q.stomach, p.stomach), "stomach did not drain while off");
        CHECK(q.now == later, "clock is current");
        CHECK(q.craving_until - q.now == 6 * HOUR, "craving timer carried forward");
        CHECK(q.queasy_until - q.now == HOUR && pet_mood(&q) == PET_MOOD_QUEASY, "still queasy");
        CHECK(q.now - q.born == p.now - p.born, "age did not advance while off");
        CHECK(q.eaten_today[FOOD_WIFI_OPEN] == 0 && q.junk_today == 0, "new day: daily counts reset");
        CHECK(NEAR(q.points[FOOD_WIFI_CAR], 5), "lifetime diet kept");
        pet_tick(&q, later + HOUR, NULL);
        CHECK(NEAR(q.stomach, p.stomach - 1), "and it carries on from there");

        /* A clock that went backwards (clock chip lost its time) must not break it. */
        CHECK(pet_load(&q, NULL, buf, len, p.now - 30 * DAY), "loads with an earlier clock");
        CHECK(NEAR(q.stomach, p.stomach) && q.craving_until - q.now == 6 * HOUR,
              "timers still relative to now");

        /* Damage and nonsense are refused, and leave the pet alone. */
        pet_t keep;
        pet_init(&keep, NULL, T0, 9);
        pet_t before = keep;
        uint8_t bad[PET_SAVE_MAX];
        memcpy(bad, buf, len);
        bad[len / 2] ^= 0x40;
        CHECK(!pet_load(&keep, NULL, bad, len, T0), "a flipped bit is refused");
        CHECK(!pet_load(&keep, NULL, buf, len - 7, T0), "a cut-off save is refused");
        CHECK(!pet_load(&keep, NULL, buf, 0, T0), "an empty save is refused");
        memset(bad, 0xFF, sizeof(bad));
        CHECK(!pet_load(&keep, NULL, bad, len, T0), "garbage is refused");
        CHECK(memcmp(&keep, &before, sizeof(keep)) == 0, "a refused load changes nothing");
    }

    /* ---- Sounds ---- */
    {
        pet_t p;
        pet_meal_t meal;
        pet_init(&p, NULL, T0, 5);

        pet_feed(&p, FOOD_WIFI_SECURED, 1);
        pet_tick(&p, T0 + 1, &meal);
        CHECK(meal.new_foods_ever == 1 && pet_meal_sound(&meal) == SOUND_FANFARE,
              "a first taste gets the fanfare");

        pet_feed(&p, FOOD_WIFI_SECURED, 1);
        pet_tick(&p, T0 + 100, &meal);
        CHECK(meal.new_foods_ever == 0 && pet_meal_sound(&meal) == SOUND_MUNCH,
              "the same food again is a plain munch");

        /* Taste a speaker, then on a later day it is rare but not new. */
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_tick(&p, T0 + 200, &meal);
        pet_feed(&p, FOOD_BLE_SPEAKER, 3);
        pet_feed(&p, FOOD_WIFI_SECURED, 1);
        pet_tick(&p, T0 + 300, &meal);
        CHECK(NEAR(meal.best_value, 3) && pet_meal_sound(&meal) == SOUND_JINGLE,
              "a rare food in the batch gets the jingle");

        p.craving = FOOD_WIFI_SECURED;
        pet_feed(&p, FOOD_WIFI_SECURED, 1);
        pet_tick(&p, T0 + 400, &meal);
        CHECK(pet_meal_sound(&meal) == SOUND_JINGLE, "a satisfied craving gets the jingle");

        const uint32_t never = UINT32_MAX, min = 60 * 1000;
        CHECK(pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_MUNCH, false, never), "munch plays");
        CHECK(!pet_sound_allowed(SOUND_LEVEL_OFF, SOUND_FANFARE, false, never), "off is off");
        CHECK(!pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_FANFARE, true, never), "quiet hours are silent");
        CHECK(!pet_sound_allowed(SOUND_LEVEL_RARE, SOUND_MUNCH, false, never), "rare only: no munch");
        CHECK(pet_sound_allowed(SOUND_LEVEL_RARE, SOUND_JINGLE, false, never), "rare only: jingle plays");
        CHECK(!pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_MUNCH, false, 2 * min),
              "munches are spaced out in a busy place");
        CHECK(pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_MUNCH, false, 4 * min), "and return after the gap");
        CHECK(pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_JINGLE, false, min), "rare sounds wait less");
        CHECK(!pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_JINGLE, false, 10 * 1000), "but still wait");
        CHECK(pet_sound_allowed(SOUND_LEVEL_ALL, SOUND_FANFARE, false, 10 * 1000), "a fanfare barely waits");

        for (int i = 0; i < SOUND_COUNT; i++) {
            const pet_sound_t *s = pet_sound((pet_sound_id_t)i);
            int total = 0;
            for (int k = 0; k < s->count; k++) {
                total += s->notes[k].ms;
            }
            CHECK(s->count > 0 && total <= 1200, "every sound is short");
        }
    }

    /* ---- Power plan ---- */
    {
        pet_power_in_t in = {.usb = true, .battery_pct = 80};
        pet_power_plan_t p = pet_power_plan(&in);
        CHECK(p.scan_period_s == 30 && !p.radio_off_between, "plugged in: scan every 30 s");
        CHECK(p.screen_stays_on && p.idle_pct > 0 && p.idle_pct < p.bright_pct,
              "plugged in: screen stays on, dimmed when idle");

        in.usb = false;
        p = pet_power_plan(&in);
        CHECK(p.scan_period_s >= 120 && p.radio_off_between, "battery: a burst every few minutes");
        CHECK(!p.screen_stays_on && p.idle_pct == 0 && p.screen_idle_ms == 15000,
              "battery: screen dark after 15 s");
        CHECK(!p.drowsy && !p.critical, "battery at 80% is fine");

        in.battery_pct = 15;
        pet_power_plan_t low = pet_power_plan(&in);
        CHECK(low.drowsy && !low.critical && low.scan_period_s > p.scan_period_s,
              "15%: drowsy and scanning less");
        in.battery_pct = 5;
        low = pet_power_plan(&in);
        CHECK(low.critical && low.scan_period_s == 0, "5%: stop scanning");
        in.usb = true;
        low = pet_power_plan(&in);
        CHECK(!low.drowsy && low.scan_period_s == 30, "plugged in at 5% is back to normal");

        /* The motion sensor sets the rhythm on battery, and only there. */
        in = (pet_power_in_t){.usb = false, .battery_pct = 80};
        CHECK(pet_power_plan(&in).scan_period_s == 180, "on battery, motion unknown: every 3 minutes");
        in.motion = PET_MOTION_MOVING;
        CHECK(pet_power_plan(&in).scan_period_s == 60, "carried: every minute");
        in.motion = PET_MOTION_PASSING;
        CHECK(pet_power_plan(&in).scan_period_s == 30, "networks going by: every half minute");
        in.battery_pct = 10;
        CHECK(pet_power_plan(&in).scan_period_s == 600, "but a low battery still comes first");
        in.battery_pct = 80;
        in.night = true;
        CHECK(pet_power_plan(&in).scan_period_s == 900, "and so do quiet hours");
        in.night = false;
        in.motion = PET_MOTION_STILL;
        CHECK(pet_power_plan(&in).scan_period_s == 300, "lying still: every 5 minutes");
        in.usb = true;
        CHECK(pet_power_plan(&in).scan_period_s == 30, "plugged in it makes no difference");

        /* ---- Gait: a walk, a drive and a desk look different ---- */
        {
            pet_gait_t g;
            int n, shake, steps;
            pet_gait_init(&g);
            uint32_t ms = 0;
            for (int i = 0; i < 330; i++, ms += 90) {       /* 30 s on a desk: sensor noise only */
                pet_gait_sample(&g, 5 + i % 3, -3, 1000 + i % 2 * 4, ms);
            }
            pet_gait_take(&g, &n, &shake, &steps);
            CHECK(n == 330 && shake < 10 && steps == 0, "on a desk: no shake, no steps");

            /* Walking: the pull swings well above and below 1 g about twice a second. */
            for (int i = 0; i < 330; i++, ms += 90) {
                int swing = (i * 90 % 520) < 260 ? 250 : -200;
                pet_gait_sample(&g, 100, 150, 1000 + swing, ms);
            }
            pet_gait_take(&g, &n, &shake, &steps);
            CHECK(steps >= 90 && steps <= 130, "walking: about 115 steps a minute");
            CHECK(shake > 60, "and a strong shake");
            int walk_shake = shake;

            /* In a car: a small, quick, irregular shake with no rhythm to it. */
            uint32_t r = 7;
            for (int i = 0; i < 330; i++, ms += 90) {
                r = r * 1103515245u + 12345u;
                pet_gait_sample(&g, 20, 30, 1000 + (int)(r >> 16) % 61 - 30, ms);
            }
            pet_gait_take(&g, &n, &shake, &steps);
            CHECK(steps == 0, "in a car: no steps");
            CHECK(shake >= 10 && shake < walk_shake, "but more shake than a desk and less than a walk");
            pet_gait_take(&g, &n, &shake, &steps);
            CHECK(n == 0 && shake == 0 && steps == 0, "taking it empties it");
        }

        /* ---- Motion itself ---- */
        {
            pet_motion_t m;
            pet_motion_init(&m);
            uint32_t ms = 1000;
            CHECK(pet_motion_state(&m, ms) == PET_MOTION_UNKNOWN, "no readings: unknown");
            bool raised = false;
            for (int i = 0; i < 50; i++, ms += 100) {      /* flat on a table */
                raised |= pet_motion_sample(&m, 10 + i % 3, -5, 1000, ms);
            }
            CHECK(!raised, "lying flat is not a raise");
            raised = pet_motion_sample(&m, 400, 0, 900, ms += 100);    /* one bump */
            for (int i = 0; i < 20; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 10, -5, 1000, ms);
            }
            CHECK(!raised && !pet_motion_take_moved(&m), "one bump of the table is nothing");
            CHECK(pet_motion_state(&m, ms) != PET_MOTION_MOVING, "and not being carried");

            /* Tipped face down and left: not a raise. */
            pet_motion_sample(&m, 600, 0, 0, ms += 100);
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 100, 0, -990, ms);
            }
            CHECK(!raised, "face down is not a raise");
            /* Stood up on edge and held: a raise. */
            pet_motion_sample(&m, 500, 300, -300, ms += 100);
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 990, 100, 20, ms);
            }
            CHECK(raised, "stood upright from another angle: a raise");
            raised = false;
            /* Hanging upright from a bag: shaken, then at rest at the same angle. */
            for (int i = 0; i < 8; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 990 - (i % 2) * 300, 100, 20 + (i % 2) * 250, ms);
            }
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 985, 120, 30, ms);
            }
            CHECK(!raised, "back to rest at the same upright angle: not a raise");
            /* Turned back to face up and held: a raise. */
            pet_motion_sample(&m, 500, 300, 700, ms += 100);
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 100, 300, 900, ms);
            }
            CHECK(raised, "turned to face up and held: a raise");
            raised = false;
            for (int i = 0; i < 30; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 100 + i % 3 * 20, 300, 900, ms);
            }
            CHECK(!raised, "holding it there, with a hand's tremor, is not another");
            /* Picked up off a desk and looked at: facing up throughout, but really moved. */
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 100 + (i % 2) * 250, 300, 900 - (i % 2) * 150, ms);
            }
            CHECK(!raised, "still moving: not yet");
            for (int i = 0; i < 6; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 250, 350, 850, ms);
            }
            CHECK(raised, "picked up and held facing up: a raise");
            raised = false;

            /* Walking: jolts every step. */
            for (int i = 0; i < 100; i++, ms += 100) {
                raised |= pet_motion_sample(&m, 200 + (i % 2) * 400, 100, 900 - (i % 2) * 300, ms);
            }
            CHECK(pet_motion_state(&m, ms) == PET_MOTION_MOVING, "walking reads as carried");
            CHECK(!raised, "and a swinging arm is not a raise");
            CHECK(pet_motion_take_moved(&m) && !pet_motion_take_moved(&m), "moved since last asked, once");
            for (int i = 0; i < 20; i++, ms += 100) {
                pet_motion_sample(&m, 0, 0, 1000, ms);
            }
            ms += 3 * 60 * 1000;
            pet_motion_sample(&m, 0, 0, 1000, ms);
            CHECK(pet_motion_state(&m, ms) == PET_MOTION_UNKNOWN, "three minutes later: no longer carried");
            ms += 8 * 60 * 1000;
            CHECK(pet_motion_state(&m, ms) == PET_MOTION_STILL, "ten minutes later: still");
        }

        /* Carry mode: a backpack with a power bank. */
        in = (pet_power_in_t){.usb = true, .battery_pct = 80, .carry = true};
        pet_power_plan_t carry = pet_power_plan(&in);
        CHECK(!carry.screen_stays_on && carry.idle_pct == 0, "carry on usb: screen sleeps when idle");
        CHECK(carry.scan_period_s == 30 && !carry.radio_off_between,
              "carry on usb: still scans every 30 s");
        in.usb = false;
        carry = pet_power_plan(&in);
        CHECK(!carry.screen_stays_on && carry.scan_period_s >= 120,
              "carry on battery is plain battery mode");

        in = (pet_power_in_t){.usb = false, .battery_pct = -1};
        p = pet_power_plan(&in);
        CHECK(!p.drowsy && p.scan_period_s > 0, "an unknown battery level is not treated as flat");

        in = (pet_power_in_t){.usb = true, .battery_pct = 80, .night = true};
        pet_power_plan_t night = pet_power_plan(&in);
        CHECK(night.scan_period_s >= 600, "night: scans rarely, even plugged in");
        in.usb = false;
        night = pet_power_plan(&in);
        CHECK(night.scan_period_s >= 600 && night.radio_off_between, "night on battery too");
        in.battery_pct = 3;
        night = pet_power_plan(&in);
        CHECK(night.scan_period_s == 0, "night does not restart scanning on a flat battery");
    }

    printf(failures ? "%d check(s) failed\n" : "all brain, save, sound and power checks passed\n",
           failures);
    return failures ? 1 : 0;
}
