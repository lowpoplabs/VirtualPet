/* Desktop checks for habitats, place badges, off the grid and their save format. Exit code 0 = pass. */
#include <stdio.h>
#include <string.h>

#include "pet_core.h"

static int failures;

/* What an empty farm adds to the end of a save: generation, moved on, forms, count. */
/* An empty farm, the byte for the favourite food, and the seven numbers and the secret, about pastures, that end a save. */
#define FARM_BYTES (14 + 28 + 16)

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

/* A scan that hears `n` fixed networks numbered from `first`, and some Bluetooth. */
static pet_scan_t scan_of(uint16_t first, int n)
{
    pet_scan_t s = {0};
    for (int i = 0; i < n; i++) {
        s.print[i] = (uint16_t)(first + i);
    }
    s.prints = (uint8_t)n;
    s.wifi_heard = (uint16_t)n;
    s.ble_heard = 3;
    return s;
}

/* Stays among the same networks for `seconds`, a scan every 30 s. Returns every event seen. */
static uint32_t stay(pet_places_t *p, uint32_t *now, uint16_t first, int n, uint32_t seconds)
{
    uint32_t ev = 0;
    pet_scan_t s = scan_of(first, n);
    for (uint32_t t = 0; t < seconds; t += 30) {
        *now += 30;
        ev |= places_scan(p, &s, *now, *now / DAY, NULL);
    }
    return ev;
}

int main(void)
{
    /* ---- Names ---- */
    {
        place_kind_t k = PLACE_KIND_COUNT;
        uint16_t b = 0;
        CHECK(place_match_name("Starbucks WiFi", &k, &b) && k == PLACE_COFFEE && b != 0,
              "a chain is recognised, whatever the capitals");
        uint16_t starbucks = b;
        CHECK(b == place_chain_id("starbucks") && b == place_chain_id("StarBucks"),
              "its number comes from its spelling");
        CHECK(b == place_legacy_chain(PLACE_COFFEE, 0), "and is what an old save's first coffee bit meant");
        CHECK(place_legacy_chain(PLACE_COFFEE, 31) == 0 && place_legacy_chain(PLACE_COFFEE, 40) == 0,
              "a bit no chain had means nothing");
        CHECK(place_match_name("DUNKIN Guest", &k, &b) && k == PLACE_COFFEE && b != starbucks,
              "another chain gets another number");
        CHECK(place_match_name("Joe's Coffee House", &k, &b) && k == PLACE_COFFEE && b == 0,
              "a general word has no number");
        CHECK(place_match_name("Starbucks Coffee", &k, &b) && b == starbucks,
              "a chain wins over a general word");
        CHECK(place_match_name("Hilton Honors", &k, &b) && k == PLACE_HOTEL, "hotel");
        CHECK(place_match_name("7Brew", &k, &b) && k == PLACE_COFFEE && b == place_chain_id("7brew"),
              "a drive-through coffee stand");
        CHECK(place_match_name("Taco Bell WiFi", &k, &b) && k == PLACE_RESTAURANT && b != 0, "a restaurant chain");
        uint16_t taco = b;
        CHECK(place_match_name("Five Guys", &k, &b) && k == PLACE_RESTAURANT && b != 0 && b != taco, "another");
        CHECK(place_match_name("Luigi's Pizza", &k, &b) && k == PLACE_RESTAURANT && b == 0,
              "a restaurant by a general word");
        CHECK(place_match_name("Elmwood Fire Dept Guest", &k, &b) && k == PLACE_SAFETY && b == 0,
              "a fire station");
        CHECK(place_match_name("OakCountySheriff", &k, &b) && k == PLACE_SAFETY, "a sheriff's office");
        CHECK(!place_match_name("Police Surveillance Van 4", &k, &b) && !place_match_name("Firefly", &k, &b),
              "but not a joke name, nor a word that only starts the same");
        CHECK(!place_match_name("Guys Win", &k, &b), "half a chain's name is not the chain");
        CHECK(place_match_name("eduroam", &k, &b) && k == PLACE_UNIVERSITY, "campus");
        CHECK(place_match_name("gogoinflight", &k, &b) && k == PLACE_INFLIGHT, "in the air");
        CHECK(place_match_name("City Library Public", &k, &b) && k == PLACE_LIBRARY, "library");
        CHECK(!place_match_name("NETGEAR-5G", &k, &b), "an ordinary router is nothing");
        CHECK(!place_match_name("", &k, &b) && !place_match_name(NULL, &k, &b), "nor is no name");
        {
            /* No two built-in chains of a kind may share a number. */
            const place_name_t *list;
            int n = place_names_builtin(&list), clash = 0;
            for (int i = 0; i < n; i++) {
                for (int j = 0; j < i; j++) {
                    if (list[i].kind == list[j].kind && list[i].chain && list[i].chain == list[j].chain) {
                        /* the same chain's spellings sit side by side; anything else is a clash */
                        bool run = true;
                        for (int m = j; m <= i; m++) {
                            run = run && list[m].chain == list[i].chain;
                        }
                        clash += !run;
                    }
                }
            }
            CHECK(n > 100 && clash == 0, "built-in chains all have their own number");
        }
        CHECK(place_tier(0) == 0 && place_tier(1) == 1 && place_tier(3) == 2 && place_tier(10) == 3,
              "tiers at 1, 3 and 10");
    }

    /* ---- Prints ---- */
    {
        const uint8_t a[6] = {1, 2, 3, 4, 5, 6}, b[6] = {1, 2, 3, 4, 5, 7};
        CHECK(place_print(7, a, 6) == place_print(7, a, 6), "the same network gives the same print");
        CHECK(place_print(7, a, 6) != place_print(7, b, 6), "a different one differs");
        CHECK(place_print(7, a, 6) != place_print(8, a, 6), "and another device sees it differently");
        CHECK(place_print(7, a, 6) != 0, "never zero");
    }

    /* ---- Settling in, leaving, coming back ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0;

        CHECK(stay(&p, &now, 100, 5, 300) == 0 && p.here < 0, "five minutes is only passing through");
        uint32_t ev = stay(&p, &now, 100, 5, 400);
        CHECK((ev & PLACE_EV_NEW) && p.here == 0, "ten minutes makes a habitat");
        CHECK(places_count(&p) == 1 && p.habitat[0].visits == 1, "one place, one visit");
        CHECK(places_home(&p) < 0, "not home yet");

        stay(&p, &now, 100, 5, 3 * HOUR);
        CHECK(places_home(&p) == 0, "after a few hours it is home");
        CHECK(p.habitat[0].minutes >= 170 && p.habitat[0].minutes <= 200, "time there is counted");
        char label[20];
        places_label(&p, 0, label, sizeof(label));
        CHECK(!strcmp(label, "Home"), "and is called Home until the owner says otherwise");

        /* One odd scan does not mean it left. */
        pet_scan_t odd = scan_of(900, 3);
        now += 30;
        CHECK(places_scan(&p, &odd, now, now / DAY, NULL) == 0 && p.here == 0, "a stray scan changes nothing");

        /* A walk past many different networks: leaves, and makes no habitats. */
        ev = 0;
        for (int i = 0; i < 40; i++) {
            pet_scan_t street = scan_of((uint16_t)(1000 + i * 10), 4);
            now += 30;
            ev |= places_scan(&p, &street, now, now / DAY, NULL);
        }
        CHECK((ev & PLACE_EV_LEFT) && p.here < 0, "walking away, it leaves");
        CHECK(!(ev & PLACE_EV_NEW) && places_count(&p) == 1, "a walk makes no habitat");

        /* Somewhere new for an hour. */
        ev = stay(&p, &now, 500, 6, HOUR);
        CHECK((ev & PLACE_EV_NEW) && p.here == 1, "a second place is found");
        places_label(&p, 1, label, sizeof(label));
        CHECK(!strcmp(label, "Place 2"), "unnamed places are numbered");
        snprintf(p.habitat[1].name, HABITAT_NAME_MAX, "Office");
        places_label(&p, 1, label, sizeof(label));
        CHECK(!strcmp(label, "Office"), "until the owner names them");

        /* Home again: recognised from only some of its networks, within a minute. */
        ev = stay(&p, &now, 3000, 3, 400);      /* the trip between */
        ev = stay(&p, &now, 102, 3, 60);
        CHECK((ev & PLACE_EV_ARRIVED) && p.here == 0, "home is recognised from part of its networks");
        CHECK(p.habitat[0].visits == 2, "and that is a second visit");
        CHECK(places_count(&p) == 2, "no extra habitat for the same place");
    }

    /* ---- Slow scans, as on battery and at night ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0, ev = 0;
        pet_scan_t home = scan_of(100, 5), odd = scan_of(900, 3);
        for (int i = 0; i < 6; i++) {           /* every 3 minutes */
            now += 180;
            ev |= places_scan(&p, &home, now, now / DAY, NULL);
        }
        CHECK((ev & PLACE_EV_NEW) && p.here == 0, "a habitat is still made from scans 3 minutes apart");
        for (int i = 0; i < 8; i++) {           /* every 15 minutes */
            now += 900;
            places_scan(&p, &home, now, now / DAY, NULL);
        }
        CHECK(p.habitat[0].minutes >= 120, "and the night's hours there are counted");
        now += 900;
        CHECK(places_scan(&p, &odd, now, now / DAY, NULL) == 0 && p.here == 0,
              "one odd scan in the night does not mean it left");
        now += 900;
        places_scan(&p, &home, now, now / DAY, NULL);
        CHECK(p.here == 0 && p.habitat[0].visits == 1, "still the same visit");
        now += 900;
        places_scan(&p, &odd, now, now / DAY, NULL);
        now += 900;
        CHECK((places_scan(&p, &odd, now, now / DAY, NULL) & PLACE_EV_LEFT), "two in a row does");
    }

    /* ---- A place with a single network ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0;
        CHECK((stay(&p, &now, 42, 1, 700) & PLACE_EV_NEW), "one network is enough to settle by");
        for (int i = 0; i < 30; i++) {
            pet_scan_t street = scan_of((uint16_t)(2000 + i * 10), 3);
            now += 30;
            places_scan(&p, &street, now, now / DAY, NULL);
        }
        CHECK(p.here < 0, "left");
        CHECK((stay(&p, &now, 42, 1, 90) & PLACE_EV_ARRIVED), "and found again");
    }

    /* ---- Full: the least lived-in unnamed place makes way; named ones and home stay ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0;
        for (int i = 0; i < HABITAT_MAX; i++) {
            stay(&p, &now, (uint16_t)(100 * (i + 1)), 4, i == 0 ? 4 * HOUR : (i == 3 ? 900 : HOUR));
            stay(&p, &now, (uint16_t)(20000 + i * 50), 2, 400);
        }
        CHECK(places_count(&p) == HABITAT_MAX, "every slot used");
        snprintf(p.habitat[3].name, HABITAT_NAME_MAX, "Gran's");   /* the briefest, but named */
        stay(&p, &now, 9000, 4, 2 * HOUR);
        CHECK(places_count(&p) == HABITAT_MAX, "still full");
        CHECK(!strcmp(p.habitat[3].name, "Gran's"), "a named place is kept");
        CHECK(places_home(&p) == 0 && p.habitat[0].minutes >= 230, "home is kept");
        CHECK(p.here >= 0 && p.habitat[p.here].print[0] == 9000, "the new place took a slot");
    }

    /* ---- Place badges ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0;
        place_kind_t kind = PLACE_KIND_COUNT;
        pet_scan_t s = scan_of(100, 3);
        s.match[0].kind = PLACE_COFFEE;
        s.match[0].chain = 101;
        s.matches = 1;
        CHECK((places_scan(&p, &s, now, 100, &kind) & PLACE_EV_BADGE) && kind == PLACE_COFFEE,
              "the first coffee shop is a badge");
        CHECK(place_tier(p.kind[PLACE_COFFEE].count) == 1, "bronze");
        CHECK(!(places_scan(&p, &s, now + 30, 100, &kind) & PLACE_EV_BADGE) &&
                  p.kind[PLACE_COFFEE].count == 1,
              "the same chain again counts for nothing");
        CHECK(!(places_scan(&p, &s, now + DAY, 101, &kind) & PLACE_EV_BADGE) &&
                  p.kind[PLACE_COFFEE].count == 1,
              "not even another day");
        s.match[0].chain = 102;
        places_scan(&p, &s, now + DAY + 30, 101, &kind);
        s.match[0].chain = 103;
        CHECK((places_scan(&p, &s, now + DAY + 60, 101, &kind) & PLACE_EV_BADGE) &&
                  place_tier(p.kind[PLACE_COFFEE].count) == 2,
              "three different chains is silver");

        /* General words: once a day. */
        s.match[0].kind = PLACE_LIBRARY;
        s.match[0].chain = 0;
        CHECK((places_scan(&p, &s, now, 200, &kind) & PLACE_EV_BADGE) && kind == PLACE_LIBRARY,
              "a library by its general name");
        places_scan(&p, &s, now + 30, 200, &kind);
        CHECK(p.kind[PLACE_LIBRARY].count == 1, "counts once a day");
        places_scan(&p, &s, now + DAY, 201, &kind);
        places_scan(&p, &s, now + 2 * DAY, 202, &kind);
        CHECK(place_tier(p.kind[PLACE_LIBRARY].count) == 2, "three days is silver");
        for (int d = 0; d < 300; d++) {
            places_scan(&p, &s, now + (3 + d) * DAY, 203 + d, &kind);
        }
        CHECK(p.kind[PLACE_LIBRARY].count == 255, "the count stops rather than wrap");

        /* More chains than can be remembered: the extra ones are not counted twice, or at all. */
        s.match[0].kind = PLACE_HOTEL;
        for (int round = 0; round < 2; round++) {
            for (int c = 0; c < PLACE_CHAINS_MAX + 8; c++) {
                s.match[0].chain = (uint16_t)(5000 + c);
                places_scan(&p, &s, now, 300, &kind);
            }
        }
        CHECK(p.kind[PLACE_HOTEL].count == PLACE_CHAINS_MAX && p.kind[PLACE_HOTEL].chains == PLACE_CHAINS_MAX,
              "the chain memory fills and stops");
    }

    /* ---- Off the grid ---- */
    {
        pet_places_t p;
        places_init(&p);
        uint32_t now = T0, ev = 0;
        pet_scan_t nothing = {0};
        for (int i = 0; i < 110; i++) {         /* 55 minutes */
            now += 30;
            ev |= places_scan(&p, &nothing, now, now / DAY, NULL);
        }
        CHECK(!(ev & PLACE_EV_OFFGRID), "55 quiet minutes is not enough");
        pet_scan_t one = {.ble_heard = 1};
        now += 30;
        places_scan(&p, &one, now, now / DAY, NULL);
        for (int i = 0; i < 110; i++) {
            now += 30;
            ev |= places_scan(&p, &nothing, now, now / DAY, NULL);
        }
        CHECK(!(ev & PLACE_EV_OFFGRID), "one Bluetooth crumb starts the hour again");
        for (int i = 0; i < 20; i++) {
            now += 30;
            ev |= places_scan(&p, &nothing, now, now / DAY, NULL);
        }
        CHECK((ev & PLACE_EV_OFFGRID) && p.offgrid_tier == 1, "a full silent hour is bronze");
        ev = 0;
        for (int i = 0; i < 8 * 120; i++) {
            now += 30;
            ev |= places_scan(&p, &nothing, now, now / DAY, NULL);
        }
        CHECK((ev & PLACE_EV_OFFGRID) && p.offgrid_tier == 3, "eight hours is gold");

        /* With a motion sensor: a silent drawer earns nothing, a silent walk does. */
        places_init(&p);
        pet_scan_t drawer = {.motion = 1}, walk = {.motion = 2};
        ev = 0;
        for (int i = 0; i < 4 * 120; i++) {
            now += 30;
            ev |= places_scan(&p, &drawer, now, now / DAY, NULL);
        }
        CHECK(!(ev & PLACE_EV_OFFGRID) && p.offgrid_tier == 0, "four silent hours lying still earn nothing");
        places_init(&p);
        for (int i = 0; i < 130; i++) {
            now += 30;
            /* walking, with a rest now and then */
            ev |= places_scan(&p, i % 4 == 3 ? &drawer : &walk, now, now / DAY, NULL);
        }
        CHECK((ev & PLACE_EV_OFFGRID) && p.offgrid_tier == 1, "a silent hour's walk, rests and all, is bronze");

        /* Scanning switched off for hours is not silence. */
        places_init(&p);
        now += 30;
        places_scan(&p, &nothing, now, now / DAY, NULL);
        now += 5 * HOUR;
        CHECK(!(places_scan(&p, &nothing, now, now / DAY, NULL) & PLACE_EV_OFFGRID) &&
                  p.offgrid_tier == 0,
              "a gap in scanning does not count as silence");
    }

    /* ---- In the pet: mood, the day's record, Homebody or Roamer ---- */
    {
        pet_t pet;
        pet_init(&pet, NULL, T0, 5);
        pet_scan_t home = scan_of(100, 5);
        float mood = pet.mood;
        uint32_t ev = 0;
        for (int i = 0; i < 25; i++) {
            pet_tick(&pet, pet.now + 30, NULL);
            ev |= pet_scan(&pet, &home, NULL);
        }
        CHECK((ev & PLACE_EV_NEW) && pet.mood > mood, "settling somewhere new lifts its mood");
        CHECK(pet.today.flags & DAY_NEW_PLACE, "and goes in the day's record");
        char entry[200];
        pet_journal_entry(&pet, &pet.today, true, entry, sizeof(entry));
        CHECK(strlen(entry) > 10, "the journal has something to say");

        /* All its network food at home: a Homebody. */
        pet.stage = STAGE_JUVENILE;
        pet.form = FORM_GRAZER;
        pet_feed(&pet, FOOD_WIFI_SECURED, 1);
        CHECK(pet.places.habitat[0].net_points > 0 && pet.places.away_points == 0,
              "network food is credited to where it was eaten");
        CHECK(places_top_share(&pet.places) < 0, "too little eaten to say where it comes from");
        places_fed(&pet.places, 40);
        CHECK(places_top_share(&pet.places) > 0.99f, "everything came from one place");
        CHECK(pet_form_leaning(&pet) == FORM_HOMEBODY, "which makes a Homebody");

        /* Then a great deal eaten out and about: a Roamer. */
        pet.places.here = -1;
        pet_feed(&pet, FOOD_WIFI_WPA3, 2);
        CHECK(pet.places.away_points > 0, "food eaten on the move is credited to nowhere");
        places_fed(&pet.places, 200);
        CHECK(places_top_share(&pet.places) < 0.6f, "most of it now came from elsewhere");
        CHECK(pet_form_leaning(&pet) == FORM_ROAMER, "which makes a Roamer");

        /* ---- Saving ---- */
        snprintf(pet.places.habitat[0].name, HABITAT_NAME_MAX, "Burrow");
        pet.places.kind[PLACE_HOTEL].count = 4;
        pet.places.kind[PLACE_HOTEL].chain[0] = 4321;
        pet.places.kind[PLACE_HOTEL].chain[1] = 77;
        pet.places.kind[PLACE_HOTEL].chains = 2;
        pet.places.offgrid_tier = 2;
        uint8_t buf[PET_SAVE_MAX];
        size_t len = pet_save(&pet, buf, sizeof(buf));
        pet_t q;
        CHECK(len > 0, "the save with places fits");
        CHECK(pet_load(&q, NULL, buf, len, pet.now), "and loads");
        CHECK(!memcmp(q.places.habitat, pet.places.habitat, sizeof(q.places.habitat)), "habitats kept");
        CHECK(!memcmp(q.places.kind, pet.places.kind, sizeof(q.places.kind)), "place badges kept");
        CHECK(q.places.offgrid_tier == 2 && q.places.away_points == pet.places.away_points,
              "off the grid and away points kept");
        CHECK(q.places.here == -1, "where it is right now is found again, not remembered");

        /* A full table still fits. */
        for (int i = 0; i < HABITAT_MAX; i++) {
            pet.places.habitat[i].prints = HABITAT_PRINTS;
        }
        CHECK(pet_save(&pet, buf, sizeof(buf)) > 0, "a full table of habitats fits too");

        /* ---- Version 4 kept a bit for each chain, by its place in the list of the day ---- */
        pet_init(&pet, NULL, T0, 9);
        snprintf(pet.name, sizeof(pet.name), "Bit");
        pet.places.offgrid_tier = 1;
        len = pet_save(&pet, buf, sizeof(buf));
        {
            /* The end of a save: for each kind count, chains (none: 1 byte), last day; then off the grid. */
            /* The growth speed and the farm are newer still. */
            size_t at = len - 4 - FARM_BYTES - 4 - 1 - PLACE_KIND_COUNT * 6;
            for (int k = 0; k < PLACE_KIND_COUNT; k++) {
                uint8_t count = k == PLACE_HOTEL ? 3 : 0;
                /* hotel bits 0 and 1 were Marriott and Hilton; bit 31 was never a chain */
                uint32_t bits = k == PLACE_HOTEL ? 0x80000003u : 0, last_day = 77;
                buf[at++] = count;
                memcpy(buf + at, &bits, 4);
                memcpy(buf + at + 4, &last_day, 4);
                at += 8;
            }
            buf[at++] = 1;
            uint32_t four = 4;
            memcpy(buf + 4, &four, 4);
            uint32_t crc4 = crc32(buf, at);
            memcpy(buf + at, &crc4, 4);
            CHECK(pet_load(&q, NULL, buf, at + 4, pet.now), "a version 4 save still loads");
            CHECK(!strcmp(q.name, "Bit") && q.places.offgrid_tier == 1, "with what it held");
            CHECK(q.places.kind[PLACE_HOTEL].count == 3 && q.places.kind[PLACE_HOTEL].last_day == 77,
                  "its badge counts are kept");
            CHECK(q.places.kind[PLACE_HOTEL].chains == 2 &&
                      q.places.kind[PLACE_HOTEL].chain[0] == place_chain_id("marriott") &&
                      q.places.kind[PLACE_HOTEL].chain[1] == place_chain_id("hilton"),
                  "and its chain bits become those chains' numbers");
            /* So the Hilton it had already visited does not count again. */
            pet_scan_t s = {0};
            place_kind_t kind = PLACE_KIND_COUNT;
            uint16_t chain = 0;
            place_match_name("Hilton Honors", &kind, &chain);
            s.match[0].kind = (uint8_t)kind;
            s.match[0].chain = chain;
            s.matches = 1;
            places_scan(&q.places, &s, T0, 500, NULL);
            CHECK(q.places.kind[PLACE_HOTEL].count == 3, "a chain counted before the change stays counted");
        }

        /* ---- Version 5 did not say what speed the pet was growing at ---- */
        pet_init(&pet, NULL, T0, 9);
        snprintf(pet.name, sizeof(pet.name), "Five");
        pet_tick(&pet, T0 + 3 * 86400, NULL);
        len = pet_save(&pet, buf, sizeof(buf));
        {
            size_t at = len - 4 - FARM_BYTES - 4;
            uint32_t five = 5;
            memcpy(buf + 4, &five, 4);
            uint32_t crc5 = crc32(buf, at);
            memcpy(buf + at, &crc5, 4);
            pet_tuning_t fast = pet_tuning_default();
            fast.growth_speed = 4;
            CHECK(pet_load(&q, &fast, buf, at + 4, pet.now), "a version 5 save still loads");
            CHECK(!strcmp(q.name, "Five") && q.born == pet.born && q.tune.growth_speed == 4,
                  "and is taken to have lived at the speed it wakes at");
        }

        /* ---- A faint place name counts only once it has been heard again later ---- */
        {
            pet_places_t pl;
            places_init(&pl);
            pet_scan_t s = {0};
            s.matches = 1;
            s.match[0].kind = PLACE_RESTAURANT;
            s.match[0].chain = 7001;
            s.match[0].faint = 1;
            uint32_t t = T0;
            places_scan(&pl, &s, t, 600, NULL);
            places_scan(&pl, &s, t + 30, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 0, "heard faintly twice in passing: not a visit");
            pet_scan_t none = {0};
            places_scan(&pl, &none, t + 60, 600, NULL);
            places_scan(&pl, &s, t + PLACE_STAY_S, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 1, "still there a few minutes on: a visit");
            places_scan(&pl, &s, t + PLACE_STAY_S + 30, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 1, "which counts once");

            /* Driving past another, then past it again much later: the wait starts over. */
            s.match[0].chain = 7002;
            places_scan(&pl, &s, t + 1000, 600, NULL);
            places_scan(&pl, &none, t + 1000 + PLACE_WAIT_FORGET_S + 60, 600, NULL);
            places_scan(&pl, &s, t + 1000 + PLACE_WAIT_FORGET_S + 90, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 1, "passed twice, far apart: not a visit");

            /* Heard strongly, it counts at once, as before. */
            s.match[0].chain = 7003;
            s.match[0].faint = 0;
            places_scan(&pl, &s, t + 5000, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 2, "a strong one counts on the spot");

            /* More faint names at once than it can wait on: none is lost for good. */
            s.match[0].faint = 1;
            for (int n = 0; n < PLACE_WAIT_MAX + 3; n++) {
                s.match[0].chain = (uint16_t)(8000 + n);
                places_scan(&pl, &s, t + 6000 + n, 600, NULL);
            }
            s.match[0].chain = (uint16_t)(8000 + PLACE_WAIT_MAX + 2);
            places_scan(&pl, &s, t + 6000 + PLACE_STAY_S + 20, 600, NULL);
            CHECK(pl.kind[PLACE_RESTAURANT].count == 3, "a crowd of faint names: the latest still gets its turn");
        }

        /* ---- A save from before the newest kind of place was added has one kind fewer ---- */
        pet_init(&pet, NULL, T0, 9);
        snprintf(pet.name, sizeof(pet.name), "Fewer");
        pet.places.kind[PLACE_COFFEE].count = 2;
        pet.places.offgrid_tier = 1;
        len = pet_save(&pet, buf, sizeof(buf));
        {
            /* The end of a save: ..., a kind (6 bytes with no chains) x count, off the grid, speed, checksum. */
            size_t tail = 1 + 4 + FARM_BYTES + 4;
            size_t kinds_at = len - tail - PLACE_KIND_COUNT * 6 - 4 -
                              HABITAT_MAX * (HABITAT_PRINTS * 3 + 1 + HABITAT_NAME_MAX + 2 + 12) - 4;
            uint32_t fewer = PLACE_KIND_COUNT - 1;
            memcpy(buf + kinds_at, &fewer, 4);
            memmove(buf + len - tail - 6, buf + len - tail, tail);      /* drop the last kind */
            size_t short_len = len - 6;
            uint32_t crc_short = crc32(buf, short_len - 4);
            memcpy(buf + short_len - 4, &crc_short, 4);
            CHECK(pet_load(&q, NULL, buf, short_len, pet.now), "a save with one kind of place fewer still loads");
            CHECK(!strcmp(q.name, "Fewer") && q.places.kind[PLACE_COFFEE].count == 2 &&
                      q.places.offgrid_tier == 1, "with what it held");
            CHECK(q.places.kind[PLACE_KIND_COUNT - 1].count == 0, "and the new kind starts empty");
        }

        /* ---- The save from before places: version 3 is a later one without the last part ---- */
        pet_init(&pet, NULL, T0, 9);
        for (int i = 0; i < 30; i++) {
            pet_feed(&pet, FOOD_BLE_PHONE, 2);
            pet_tick(&pet, pet.now + 61, NULL);
        }
        snprintf(pet.name, sizeof(pet.name), "Pip");
        len = pet_save(&pet, buf, sizeof(buf));
        size_t places_bytes = 8 + HABITAT_MAX * (HABITAT_PRINTS * 3 + 1 + HABITAT_NAME_MAX + 2 + 12) +
                              4 + PLACE_KIND_COUNT * 6 + 1 + 4 + FARM_BYTES;   /* + the growth speed and the farm */
        size_t old_len = len - places_bytes;
        uint32_t three = 3;
        memcpy(buf + 4, &three, 4);
        uint32_t crc = crc32(buf, old_len - 4);
        memcpy(buf + old_len - 4, &crc, 4);
        CHECK(pet_load(&q, NULL, buf, old_len, pet.now), "a version 3 save still loads");
        CHECK(q.meals == pet.meals && !strcmp(q.name, "Pip") && q.badges == pet.badges,
              "with its meals, name and badges");
        CHECK(places_count(&q.places) == 0 && q.places.here == -1, "and no habitats yet");
        CHECK(pet_form_leaning(&q) == pet_form_leaning(&pet), "and the same leaning as before");
    }

    printf(failures ? "%d check(s) failed\n" : "all place checks passed\n", failures);
    return failures ? 1 : 0;
}
