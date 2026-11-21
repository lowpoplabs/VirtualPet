/* Checks for the settings files (pet_config.h). No window needed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_config.h"

static int s_failed;

#define CHECK(cond, what)                              \
    do {                                               \
        if (!(cond)) {                                 \
            printf("FAILED: %s (line %d)\n", what, __LINE__); \
            s_failed++;                                \
        }                                              \
    } while (0)

static bool same_tuning(const pet_tuning_t *a, const pet_tuning_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

int main(void)
{
    const pet_tuning_t def = pet_tuning_default();
    pet_tuning_t t;
    pet_config_error_t err;

    /* ---- No file, empty file, comments only ---- */
    CHECK(pet_config_tuning(NULL, &t, &err) && same_tuning(&t, &def), "no file: built-in values");
    CHECK(pet_config_tuning("", &t, &err) && same_tuning(&t, &def), "empty file: built-in values");
    CHECK(pet_config_tuning("# nothing here\n\n   \r\n", &t, &err) && same_tuning(&t, &def),
          "comments and blank lines only");
    CHECK(err.why[0] == '\0', "and no complaint");

    /* ---- Personality numbers are tuning numbers like any other ---- */
    CHECK(pet_config_tuning("lazy_nap = 3\nshy_play_less = 0.25\n", &t, &err) && t.lazy_nap == 3 &&
              t.shy_play_less == 0.25f && t.curious_look == def.curious_look,
          "trait numbers land, and the others keep their built-in value");
    CHECK(!pet_config_tuning("shy_play_less = 1.5\n", &t, &err) && err.line == 1 && same_tuning(&t, &def),
          "one out of range spoils the file, as any number does");

    /* ---- The journal's sentences ---- */
    {
        pet_t pet;
        pet_init(&pet, NULL, 100 * 86400, 3);
        pet_day_t day = {.day = 500, .foods = 5, .types = 2, .best = FOOD_TYPE_COUNT};
        char entry[160];
        const char *const *lines;

        CHECK(pet_config_journal(NULL, &err), "no journal file: fine");
        pet_journal_entry(&pet, &day, true, entry, sizeof(entry));
        bool builtin = false;
        for (int i = 0, n = pet_journal_builtin(JOURNAL_QUIET, &lines); i < n; i++) {
            builtin = builtin || !strcmp(entry, lines[i]);
        }
        CHECK(builtin, "and a slow day reads as one of the built-in sentences");

        const char *mine = "# my own words\r\n"
                           "quiet = Hardly a bite, all day.\r\n"
                           "  quiet =   Thin times.  \n"
                           "best = Oh, {food}! Lovely.\n";
        CHECK(pet_config_journal(mine, &err), "a good journal file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        pet_journal_entry(&pet, &day, true, entry, sizeof(entry));
        CHECK(!strcmp(entry, "Hardly a bite, all day.") || !strcmp(entry, "Thin times."),
              "a list it gives is replaced: commas stay, spaces at the ends go");
        bool both = false, first = false, second = false;
        for (uint32_t d = 1; d < 60; d++) {
            day.day = d;
            pet_journal_entry(&pet, &day, true, entry, sizeof(entry));
            first = first || !strcmp(entry, "Hardly a bite, all day.");
            second = second || !strcmp(entry, "Thin times.");
        }
        both = first && second;
        CHECK(both, "and each of its sentences gets used");

        /* A list it leaves out keeps its built-in sentences. */
        day.foods = 0;
        pet_journal_entry(&pet, &day, false, entry, sizeof(entry));
        builtin = false;
        for (int i = 0, n = pet_journal_builtin(JOURNAL_NOTHING, &lines); i < n; i++) {
            builtin = builtin || !strcmp(entry, lines[i]);
        }
        CHECK(builtin, "a list left out keeps its built-in sentences");

        /* The blank is filled with the food. */
        bool filled = false;
        for (uint32_t d = 1; d < 200 && !filled; d++) {
            pet_day_t b = {.day = d, .foods = 20, .types = 3, .best = FOOD_BLE_SPEAKER};
            pet_journal_entry(&pet, &b, true, entry, sizeof(entry));
            filled = strstr(entry, "Oh, a speaker! Lovely.") != NULL;
            CHECK(!strstr(entry, "{food}"), "no blank is ever left showing");
        }
        CHECK(filled, "a sentence that names a food has it filled in");

        static const struct {
            const char *text;
            int line;
            const char *says;
        } BAD[] = {
            {"quite = A slow day.\n", 1, "no such list"},
            {"quiet = Fine.\nquiet\n", 2, "expected list = sentence"},
            {"quiet = \n", 1, "sentence is missing"},
            {"quiet = This sentence goes on and on and on, far past what one line of the journal can hold.\n",
             1, "too long"},
            {"best = The best bit was lovely.\n", 1, "needs {food}"},
            {"best = {food} and {food}.\n", 1, "needs {food}"},
            {"quiet = Just {food} today.\n", 1, "does not belong"},
            {"quiet = Caf\xC3\xA9 day.\n", 1, "cannot show"},
        };
        for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
            bool ok = pet_config_journal(BAD[i].text, &err);
            day.day = 500;
            day.foods = 5;
            pet_journal_entry(&pet, &day, true, entry, sizeof(entry));
            builtin = false;
            for (int k = 0, n = pet_journal_builtin(JOURNAL_QUIET, &lines); k < n; k++) {
                builtin = builtin || !strcmp(entry, lines[k]);
            }
            if (ok || err.line != BAD[i].line || !strstr(err.why, BAD[i].says) || !builtin) {
                printf("FAILED: bad journal %d: ok=%d line=%d why=\"%s\" builtin=%d\n", (int)i, ok, err.line,
                       err.why, builtin);
                s_failed++;
            }
        }

        /* The built-in copy is a valid file, and reads back as the built-in sentences. */
        char *buf = malloc(PET_CONFIG_TEXT_MAX);
        size_t n = pet_config_journal_text(buf, PET_CONFIG_TEXT_MAX);
        CHECK(n > 0 && n < PET_CONFIG_TEXT_MAX - 1, "the journal reference fits");
        printf("  (journal reference: %d of %d bytes)\n", (int)n, PET_CONFIG_TEXT_MAX);
        char before[160];
        pet_config_journal(NULL, &err);
        pet_day_t b = {.day = 77, .foods = 30, .types = 4, .best = FOOD_BLE_TV, .new_foods = 1};
        pet_journal_entry(&pet, &b, false, before, sizeof(before));
        CHECK(pet_config_journal(buf, &err), "and is a valid file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        pet_journal_entry(&pet, &b, false, entry, sizeof(entry));
        CHECK(!strcmp(entry, before), "that changes nothing");
        free(buf);
        pet_config_journal(NULL, &err);
    }

    /* ---- A good file ---- */
    CHECK(pet_config_tuning("\xEF\xBB\xBF# speed run\r\n"
                            "growth_speed = 2016   # a life in an hour\r\n"
                            "  hatchling_meals=20\r\n"
                            "queasy_hours = 0.5\r\n"
                            "stomach_max\t=\t30",
                            &t, &err),
          "a good file is taken");
    CHECK(t.growth_speed == 2016 && t.hatchling_meals == 20, "its numbers land");
    CHECK(pet_config_gives("# growth_speed = 3\n  growth_speed=1\n", "growth_speed"),
          "a file that gives the growth speed is noticed, even at its built-in value");
    CHECK(!pet_config_gives("# growth_speed = 3\nhatchling_meals = 20\n", "growth_speed") &&
              !pet_config_gives("growth_speed_x = 3\n", "growth_speed") &&
              !pet_config_gives("", "growth_speed") && !pet_config_gives(NULL, "growth_speed"),
          "and one that does not is not");
    CHECK(t.queasy_s == 1800, "hours become seconds");
    CHECK(t.stomach_max == 30 && t.drain_per_hour == def.drain_per_hour, "lines left out keep their value");

    /* ---- Mistakes: the whole file is ignored, and the line is named ---- */
    static const struct {
        const char *text;
        int line;
        const char *says;
    } BAD[] = {
        {"growth_speed = 2016\ngrowth_sped = 3\n", 2, "no such setting"},
        {"growth_speed = fast\n", 1, "not a number"},
        {"growth_speed = 12abc\n", 1, "not a number"},
        {"growth_speed\n", 1, "expected name = number"},
        {"\n\nstomach_max = 500\n", 3, "must be"},
        {"stomach_max = -1\n", 1, "must be"},
        {"egg_meals = 2.5\n", 1, "whole number"},
        {"egg_meals = 3\negg_meals = 4\n", 2, "given twice"},
        {"growth_speed = nan\n", 1, "not a number"},
        {"growth_speed = 1e999\n", 1, "not a number"},
        {"hungry_below = 25\n", 0, "hungry_below"},        /* above stomach_max 20 */
        {"start_stomach = 50\n", 0, "start_stomach"},
        {"static_recover_at = 0.5\n", 0, "static_recover_at"},
    };
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        bool ok = pet_config_tuning(BAD[i].text, &t, &err);
        if (ok || !same_tuning(&t, &def) || err.line != BAD[i].line || !strstr(err.why, BAD[i].says)) {
            printf("FAILED: bad file %d: ok=%d line=%d why=\"%s\"\n", (int)i, ok, err.line, err.why);
            s_failed++;
        }
    }
    {
        char longline[400];
        memset(longline, 'x', sizeof(longline) - 1);
        longline[sizeof(longline) - 1] = '\0';
        CHECK(!pet_config_tuning(longline, &t, &err) && strstr(err.why, "too long"), "a very long line");
    }

    /* ---- The built-in copy reads back as itself ---- */
    {
        char *buf = malloc(PET_CONFIG_TEXT_MAX);
        size_t n = pet_config_tuning_text(buf, PET_CONFIG_TEXT_MAX);
        CHECK(n > 0 && n < PET_CONFIG_TEXT_MAX - 1, "the tuning reference fits");
        CHECK(pet_config_tuning(buf, &t, &err), "and is a valid file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        CHECK(same_tuning(&t, &def), "that gives the built-in values");
        CHECK(pet_config_tuning_text(buf, 100) == 0, "too small a buffer is refused, not overrun");

        food_values_t f;
        const food_values_t fdef = food_values_default();
        n = pet_config_foods_text(buf, PET_CONFIG_TEXT_MAX);
        CHECK(n > 0 && n < PET_CONFIG_TEXT_MAX - 1, "the foods reference fits");
        CHECK(pet_config_foods(buf, &f, &err), "and is a valid file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        CHECK(memcmp(&f, &fdef, sizeof(f)) == 0, "that gives the built-in values");
        free(buf);
    }

    /* ---- Foods ---- */
    {
        food_values_t f;
        CHECK(pet_config_foods("car_hotspot = 8\ncrumbs = 0.25\nhidden_treat_one_in = 1\nhidden_treat = 9\n",
                               &f, &err),
              "a good foods file");
        CHECK(f.value[FOOD_WIFI_CAR] == 8 && f.value[FOOD_BLE_UNKNOWN] == 0.25f, "its values land");
        CHECK(f.value[FOOD_BLE_CAR] == 5, "car and car_hotspot are different foods");
        food_values_set(&f);
        CHECK(food_value(FOOD_WIFI_CAR, 0) == 8, "and are what a food is then worth");
        CHECK(food_value(FOOD_WIFI_HIDDEN, 12345) == 9, "one in one: every hidden network is the treat");
        CHECK(!pet_config_foods("pizza = 3\n", &f, &err) && err.line == 1, "an unknown food is refused");
        CHECK(!pet_config_foods("phone = 99\n", &f, &err), "a silly value is refused");
        const food_values_t fdef = food_values_default();
        CHECK(memcmp(&f, &fdef, sizeof(f)) == 0, "and leaves the built-in values");
        food_values_set(&fdef);
    }

    /* ---- Food names ---- */
    {
        wifi_sighting_t w = {.ssid = "Skoda Connect 12", .sec = WIFI_SEC_SECURED};
        ble_sighting_t b = {.name = "Nothing Ear"};
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_SECURED && food_classify_ble(&b) == FOOD_BLE_UNKNOWN,
              "names the built-in lists do not know");
        CHECK(pet_config_food_names("\xEF\xBB\xBF# mine\r\n"
                                    "car_hotspot = Skoda, \"My Seat \"   # two makes\r\n"
                                    "earbuds = Nothing Ear,\r\n"
                                    "earbuds = AirPods\r\n"
                                    "printer =\r\n",
                                    &err),
              "a good food names file");
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_CAR, "its names are used");
        CHECK(food_classify_ble(&b) == FOOD_BLE_EARBUDS, "for Bluetooth too");
        b.name = "AirPods Pro";
        CHECK(food_classify_ble(&b) == FOOD_BLE_EARBUDS, "a food may have several lines");
        b.name = "Galaxy Buds";
        CHECK(food_classify_ble(&b) == FOOD_BLE_UNKNOWN, "a list given replaces the built-in one");
        b.name = "JBL Flip 6";
        CHECK(food_classify_ble(&b) == FOOD_BLE_SPEAKER, "a list left out is kept");
        w.ssid = "Tesla Model 3";
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_SECURED, "the old car names are gone");
        w.ssid = "my seat ibiza";
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_CAR, "quotes keep the space at the end");
        w.ssid = "My Seatbelt";
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_SECURED, "which then has to be there");
        w.ssid = "EPSON-1234";
        CHECK(food_classify_wifi(&w) == FOOD_WIFI_SECURED, "an empty list matches nothing");

        static const struct {
            const char *text;
            int line;
            const char *says;
        } BAD[] = {
            {"earbuds = AirPods\nearbud = Buds\n", 2, "no such list"},
            {"phone = iPhone\n", 1, "no such list"},          /* phones are not told by name */
            {"earbuds AirPods\n", 1, "expected name = list"},
            {"earbuds = AirPods,, Buds\n", 1, "missing between commas"},
            {"\ntv = x\n", 2, "too short"},
            {"tv = a name that goes on and on and on and on\n", 1, "too long"},
            {"tv = \"Fire\n", 1, "quote is not closed"},
            {"tv = \"Fire\" Stick\n", 1, "comma after"},
        };
        for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
            bool ok = pet_config_food_names(BAD[i].text, &err);
            w.ssid = "Tesla Model 3";
            if (ok || err.line != BAD[i].line || !strstr(err.why, BAD[i].says) ||
                food_classify_wifi(&w) != FOOD_WIFI_CAR) {
                printf("FAILED: bad food names %d: ok=%d line=%d why=\"%s\"\n", (int)i, ok, err.line, err.why);
                s_failed++;
            }
        }

        char *buf = malloc(PET_CONFIG_TEXT_MAX);
        size_t n = pet_config_food_names_text(buf, PET_CONFIG_TEXT_MAX);
        CHECK(n > 0 && n < PET_CONFIG_TEXT_MAX - 1, "the food names reference fits");
        CHECK(pet_config_food_names(buf, &err), "and is a valid file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        static const food_type_t NAMED[] = {FOOD_WIFI_CAR, FOOD_WIFI_HOTSPOT, FOOD_WIFI_PRINTER, FOOD_BLE_CAR,
                                            FOOD_BLE_EARBUDS, FOOD_BLE_SPEAKER, FOOD_BLE_WATCH, FOOD_BLE_TV};
        bool same = true;
        for (size_t k = 0; k < sizeof(NAMED) / sizeof(NAMED[0]); k++) {
            const char *const *a = food_names(NAMED[k]), *const *d = food_names_default(NAMED[k]);
            same = same && a != d;      /* read from the file, not left alone */
            for (; *a && *d; a++, d++) {
                same = same && strcmp(*a, *d) == 0;
            }
            same = same && !*a && !*d;
        }
        CHECK(same, "that gives the built-in lists, spaces and all");
        CHECK(food_names(FOOD_BLE_PHONE) == NULL, "a food told some other way has no list");
        CHECK(pet_config_food_names(NULL, &err) && food_names(FOOD_BLE_TV) == food_names_default(FOOD_BLE_TV),
              "no file: back to the built-in lists");
        CHECK(pet_config_food_names_text(buf, 200) == 0, "too small a buffer is refused, not overrun");
        free(buf);
    }

    /* ---- Place names ---- */
    {
        place_kind_t kind = PLACE_KIND_COUNT;
        uint16_t chain = 0, costa = 0;
        CHECK(place_match_name("Costa Coffee Guest", &kind, &chain) && chain != 0, "a built-in chain");
        costa = chain;
        CHECK(!place_match_name("Greggs_WiFi", &kind, &chain), "a name the built-in lists do not know");
        CHECK(pet_config_place_names("# a British high street\n"
                                     "coffee = Greggs\n"
                                     "coffee = Costa Coffee, costa_wifi\n"
                                     "coffee = Caffe Nero, caffenero, \"nero \"\n"
                                     "coffee_word = Tea Room\n"
                                     "train_word =\n",
                                     &err),
              "a good place names file");
        CHECK(place_match_name("Greggs_WiFi", &kind, &chain) && kind == PLACE_COFFEE &&
                  chain == place_chain_id("greggs"),
              "its chains are found, known by their spelling");
        CHECK(place_match_name("COSTA COFFEE", &kind, &chain) && chain == costa,
              "a chain that was built in keeps its number, wherever it now sits in the list");
        uint16_t nero = 0;
        CHECK(place_match_name("CaffeNero Free", &kind, &nero) && place_match_name("Nero Cloud", &kind, &chain) &&
                  chain == nero && nero == place_chain_id("caffe nero"),
              "spellings on one line are one chain");
        CHECK(place_match_name("The Old Tea Room", &kind, &chain) && kind == PLACE_COFFEE && chain == 0,
              "general words have no number");
        CHECK(!place_match_name("Starbucks WiFi", &kind, &chain), "a kind given is replaced");
        CHECK(!place_match_name("Amtrak_WiFi", &kind, &chain), "and can be emptied");
        CHECK(place_match_name("Hilton Honors", &kind, &chain) && kind == PLACE_HOTEL &&
                  chain == place_legacy_chain(PLACE_HOTEL, 1),
              "a kind left out is kept");

        static const struct {
            const char *text;
            int line;
            const char *says;
        } BAD[] = {
            {"cofee = greggs\n", 1, "no such list"},
            {"coffee = greggs\ncoffee = ab\n", 2, "too short"},
            {"coffee = greggs\nhotel = ibis\ncoffee = Greggs, gregs\n", 3, "same chain"},
        };
        for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
            bool ok = pet_config_place_names(BAD[i].text, &err);
            if (ok || err.line != BAD[i].line || !strstr(err.why, BAD[i].says) ||
                !place_match_name("Starbucks WiFi", &kind, &chain)) {
                printf("FAILED: bad place names %d: ok=%d line=%d why=\"%s\"\n", (int)i, ok, err.line, err.why);
                s_failed++;
            }
        }

        /* The built-in copy reads back as the built-in list, entry for entry. */
        char *buf = malloc(PET_CONFIG_TEXT_MAX);
        size_t n = pet_config_place_names_text(buf, PET_CONFIG_TEXT_MAX);
        CHECK(n > 0 && n < PET_CONFIG_TEXT_MAX - 1, "the place names reference fits");
        printf("  (place names reference: %d of %d bytes)\n", (int)n, PET_CONFIG_TEXT_MAX);
        CHECK(pet_config_place_names(buf, &err), "and is a valid file");
        if (err.why[0]) {
            printf("  line %d: %s\n", err.line, err.why);
        }
        const place_name_t *list;
        int count = place_names_builtin(&list), wrong = 0;
        for (int i = 0; i < count; i++) {
            /* each built-in piece, as a network name, must come out as the same kind and chain */
            if (!place_match_name(list[i].piece, &kind, &chain)) {
                wrong++;
            }
        }
        CHECK(wrong == 0, "every built-in name is still found");
        CHECK(place_match_name("Hilton Honors", &kind, &chain) && chain == place_legacy_chain(PLACE_HOTEL, 1) &&
                  place_match_name("hhonors", &kind, &nero) && nero == chain,
              "with the same numbers");
        CHECK(place_match_name("City Library", &kind, &chain) && kind == PLACE_LIBRARY && chain == 0,
              "and the same general words");
        CHECK(pet_config_place_names("", &err) && place_match_name("Starbucks WiFi", &kind, &chain),
              "no file: back to the built-in lists");
        free(buf);
    }

    if (s_failed) {
        printf("%d config checks FAILED\n", s_failed);
        return 1;
    }
    printf("all config checks passed\n");
    return 0;
}
