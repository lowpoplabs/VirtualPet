/* Settings files: see pet_config.h. */
#include "pet_config.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { K_FLOAT, K_INT, K_SECONDS } kind_t;

/* One line a file may hold: where its number goes and what it may be. */
typedef struct {
    const char *name;
    kind_t kind;
    size_t offset;
    float scale;        /* K_SECONDS: seconds per unit written in the file */
    float min, max;     /* in the file's units */
    const char *help;
} entry_t;

#define TUNE(field) offsetof(pet_tuning_t, field)

static const entry_t TUNING[] = {
    {"stomach_max", K_FLOAT, TUNE(stomach_max), 1, 5, 100, "how much the stomach holds"},
    {"drain_per_hour", K_FLOAT, TUNE(drain_per_hour), 1, 0.05f, 20, "how fast it empties"},
    {"start_stomach", K_FLOAT, TUNE(start_stomach), 1, 0, 100, "where a new pet begins"},
    {"repeat_factor", K_FLOAT, TUNE(repeat_factor), 1, 0, 1,
     "each repeat of a food type in a day is worth this share of the last"},
    {"meal_gap_seconds", K_SECONDS, TUNE(meal_gap_s), 1, 5, 3600, "least time between eating moments"},
    {"junk_limit", K_INT, TUNE(junk_limit), 1, 1, 100, "open networks in a day before it feels queasy"},
    {"queasy_hours", K_SECONDS, TUNE(queasy_s), 3600, 0.1f, 48, "how long queasy lasts"},
    {"mood_per_new_type", K_FLOAT, TUNE(mood_per_new_type), 1, 0, 50,
     "mood for the first of each food type each day"},
    {"mood_per_value", K_FLOAT, TUNE(mood_per_value), 1, 0, 20, "mood per point of food"},
    {"mood_drift_per_hour", K_FLOAT, TUNE(mood_drift_per_hour), 1, 0, 100,
     "how fast mood settles back to its resting level"},
    {"craving_value_factor", K_FLOAT, TUNE(craving_value_factor), 1, 1, 5,
     "a craved food is worth this many times more"},
    {"craving_mood", K_FLOAT, TUNE(craving_mood), 1, 0, 50, "mood for satisfying a craving"},
    {"craving_every_hours", K_SECONDS, TUNE(craving_every_s), 3600, 0.1f, 168, "wait between cravings"},
    {"craving_lasts_hours", K_SECONDS, TUNE(craving_lasts_s), 3600, 0.1f, 168, "how long a craving lasts"},
    {"hungry_below", K_FLOAT, TUNE(hungry_below), 1, 0, 100, "stomach level that reads as hungry"},
    {"empty_below", K_FLOAT, TUNE(empty_below), 1, 0, 100, "stomach level that counts as near empty"},
    {"static_after_hours", K_SECONDS, TUNE(static_after_s), 3600, 1, 720,
     "near empty for this long turns it to Static"},
    {"static_recover_at", K_FLOAT, TUNE(static_recover_at), 1, 0, 100, "stomach level that ends Static"},

    {"growth_speed", K_FLOAT, TUNE(growth_speed), 1, 0.1f, 10000,
     "1 = real life, about 12 weeks; 2016 = a whole life in an hour"},
    {"egg_meals", K_INT, TUNE(egg_meals), 1, 1, 1000, "an egg hatches on meals alone"},
    {"hatchling_days", K_FLOAT, TUNE(hatchling_days), 1, 0.01f, 365,
     "a stage ends when both its days and its meals are done"},
    {"hatchling_meals", K_INT, TUNE(hatchling_meals), 1, 1, 100000, ""},
    {"juvenile_days", K_FLOAT, TUNE(juvenile_days), 1, 0.01f, 365, ""},
    {"juvenile_meals", K_INT, TUNE(juvenile_meals), 1, 1, 100000, ""},
    {"adult_days", K_FLOAT, TUNE(adult_days), 1, 0.01f, 3650, ""},
    {"adult_meals", K_INT, TUNE(adult_meals), 1, 1, 1000000, ""},

    {"roamer_max_home_share", K_FLOAT, TUNE(roamer_max_home_share), 1, 0.1f, 1,
     "Roamer: less than this share of its network food from any one place"},
    {"roamer_finds_per_day", K_FLOAT, TUNE(roamer_finds_per_day), 1, 1, 10000,
     "Roamer before it knows a place: plain networks found per day"},
    {"new_place_mood", K_FLOAT, TUNE(new_place_mood), 1, 0, 50, "mood for settling somewhere new"},
    {"visit_mood", K_FLOAT, TUNE(visit_mood), 1, 0, 50, "mood for arriving somewhere that is not home"},
    {"prism_max_share", K_FLOAT, TUNE(prism_max_share), 1, 0.05f, 1,
     "Prism: no food group above this share of the diet"},
    {"prism_min_types", K_INT, TUNE(prism_min_types), 1, 1, FOOD_TYPE_COUNT,
     "Prism: at least this many food types tasted"},

    /* Personality. The pull toward an action is multiplied by these: 1 changes nothing. */
    {"greedy_grumble_at", K_FLOAT, TUNE(greedy_grumble_at), 1, 0.05f, 1,
     "greedy: grumbles below this share of a full stomach (others at 0.2)"},
    {"lazy_nap", K_FLOAT, TUNE(lazy_nap), 1, 0, 10, "lazy: napping, times this"},
    {"lazy_idle", K_FLOAT, TUNE(lazy_idle), 1, 0, 10, "lazy: standing about"},
    {"lazy_play", K_FLOAT, TUNE(lazy_play), 1, 0, 10, "lazy: playing"},
    {"greedy_grumble", K_FLOAT, TUNE(greedy_grumble), 1, 0, 10, "greedy: grumbling"},
    {"greedy_sniff", K_FLOAT, TUNE(greedy_sniff), 1, 0, 10, "greedy: sniffing for food"},
    {"curious_look", K_FLOAT, TUNE(curious_look), 1, 0, 10, "curious: looking around"},
    {"curious_sniff", K_FLOAT, TUNE(curious_sniff), 1, 0, 10, "curious: sniffing"},
    {"playful_play", K_FLOAT, TUNE(playful_play), 1, 0, 10, "playful: playing"},
    {"shy_play_less", K_FLOAT, TUNE(shy_play_less), 1, 0, 1,
     "shy: share of its play it gives up where it is busiest"},
    {"shy_idle_more", K_FLOAT, TUNE(shy_idle_more), 1, 0, 5,
     "shy: how much more it stands still there (1 = twice as much)"},
    {"cheerful_play", K_FLOAT, TUNE(cheerful_play), 1, 0, 10, "cheerful: playing"},
    {"cheerful_grumble", K_FLOAT, TUNE(cheerful_grumble), 1, 0, 10, "cheerful: grumbling"},
};
#define TUNING_COUNT ((int)(sizeof(TUNING) / sizeof(TUNING[0])))

/* The foods file has one line per food type, plus these two. */
#define FOODS_EXTRA 2
#define FOODS_COUNT (FOOD_TYPE_COUNT + FOODS_EXTRA)

static void fail(pet_config_error_t *err, int line, const char *fmt, ...)
{
    if (!err || err->why[0]) {
        return;     /* the first mistake is the one reported */
    }
    err->line = line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err->why, sizeof(err->why), fmt, ap);
    va_end(ap);
}

/* A food's name in the file: its label with underscores for spaces. */
static void food_key(food_type_t type, char *out, size_t cap)
{
    snprintf(out, cap, "%s", food_label(type));
    for (char *c = out; *c; c++) {
        if (*c == ' ') {
            *c = '_';
        }
    }
}

static entry_t food_entry(int i, char *name, size_t cap)
{
    if (i < FOOD_TYPE_COUNT) {
        food_key((food_type_t)i, name, cap);
        return (entry_t){name, K_FLOAT, offsetof(food_values_t, value) + (size_t)i * sizeof(float),
                         1, 0, 50, i == FOOD_WIFI_OPEN ? "junk food" : ""};
    }
    if (i == FOOD_TYPE_COUNT) {
        snprintf(name, cap, "hidden_treat");
        return (entry_t){name, K_FLOAT, offsetof(food_values_t, hidden_treat), 1, 0, 50,
                         "now and then a hidden network is worth this instead"};
    }
    snprintf(name, cap, "hidden_treat_one_in");
    return (entry_t){name, K_INT, offsetof(food_values_t, hidden_treat_one_in), 1, 1, 10000,
                     "...one time in this many"};
}

static void put(void *base, const entry_t *e, double v)
{
    void *at = (char *)base + e->offset;
    switch (e->kind) {
    case K_FLOAT:   *(float *)at = (float)v; break;
    case K_INT:     *(int *)at = (int)lround(v); break;
    case K_SECONDS: *(uint32_t *)at = (uint32_t)lround(v * e->scale); break;
    }
}

static double get(const void *base, const entry_t *e)
{
    const void *at = (const char *)base + e->offset;
    switch (e->kind) {
    case K_FLOAT:   return *(const float *)at;
    case K_INT:     return *(const int *)at;
    default:        return *(const uint32_t *)at / (double)e->scale;
    }
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/*
 * Walks the file's lines, handing each "name = number" to the table.
 * foods: the entries are made on the fly rather than read from TUNING.
 */
static bool parse(const char *text, void *out, bool foods, pet_config_error_t *err)
{
    const int count = foods ? FOODS_COUNT : TUNING_COUNT;
    bool seen[FOODS_COUNT > TUNING_COUNT ? FOODS_COUNT : TUNING_COUNT] = {false};
    int line_no = 0;
    /* A file saved by some editors starts with three marker bytes. */
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
    }
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t len = end ? (size_t)(end - text) : strlen(text);
        char line[160];
        line_no++;
        if (len >= sizeof(line)) {
            fail(err, line_no, "line too long");
            return false;
        }
        memcpy(line, text, len);
        line[len] = '\0';
        text += len + (end ? 1 : 0);

        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        char *name = line;
        while (is_space(*name)) {
            name++;
        }
        if (!*name) {
            continue;
        }
        char *eq = strchr(name, '=');
        if (!eq) {
            fail(err, line_no, "expected name = number");
            return false;
        }
        char *tail = eq;
        while (tail > name && is_space(tail[-1])) {
            tail--;
        }
        *tail = '\0';
        char *num = eq + 1;
        while (is_space(*num)) {
            num++;
        }
        char *stop = num;
        double v = strtod(num, &stop);
        while (is_space(*stop)) {
            stop++;
        }
        if (stop == num || *stop || !isfinite(v)) {
            fail(err, line_no, "%.24s: not a number", name);
            return false;
        }

        int found = -1;
        entry_t e = {0};
        char key[32];
        for (int i = 0; i < count && found < 0; i++) {
            e = foods ? food_entry(i, key, sizeof(key)) : TUNING[i];
            if (strcmp(name, e.name) == 0) {
                found = i;
            }
        }
        if (found < 0) {
            fail(err, line_no, "%.24s: no such setting", name);
            return false;
        }
        if (seen[found]) {
            fail(err, line_no, "%.24s: given twice", name);
            return false;
        }
        seen[found] = true;
        if (v < e.min || v > e.max) {
            fail(err, line_no, "%.24s: must be %g to %g", name, (double)e.min, (double)e.max);
            return false;
        }
        if (e.kind == K_INT && v != floor(v)) {
            fail(err, line_no, "%.24s: must be a whole number", name);
            return false;
        }
        put(out, &e, v);
    }
    return true;
}

bool pet_config_gives(const char *text, const char *name)
{
    size_t n = strlen(name);
    if (text && strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
    }
    while (text && *text) {
        const char *p = text;
        while (*p != '\n' && is_space(*p)) {
            p++;
        }
        if (strncmp(p, name, n) == 0) {
            p += n;
            while (*p != '\n' && is_space(*p)) {
                p++;
            }
            if (*p == '=') {
                return true;
            }
        }
        text = strchr(text, '\n');
        text = text ? text + 1 : NULL;
    }
    return false;
}

bool pet_config_tuning(const char *text, pet_tuning_t *out, pet_config_error_t *err)
{
    pet_config_error_t local = {0};
    if (err) {
        *err = local;
    } else {
        err = &local;
    }
    *out = pet_tuning_default();
    if (!text || !*text) {
        return true;
    }
    /* The owner's quiet hours live in the settings screen, not here; they are carried through. */
    bool ok = parse(text, out, false, err);
    /* Numbers that are each fine but make no sense together. */
    if (ok && !(out->empty_below < out->hungry_below && out->hungry_below < out->stomach_max)) {
        fail(err, 0, "need empty_below < hungry_below < stomach_max");
        ok = false;
    }
    if (ok && out->start_stomach > out->stomach_max) {
        fail(err, 0, "start_stomach is above stomach_max");
        ok = false;
    }
    if (ok && !(out->static_recover_at > out->empty_below && out->static_recover_at <= out->stomach_max)) {
        fail(err, 0, "static_recover_at must be above empty_below, within the stomach");
        ok = false;
    }
    if (!ok) {
        *out = pet_tuning_default();
    }
    return ok;
}

bool pet_config_foods(const char *text, food_values_t *out, pet_config_error_t *err)
{
    pet_config_error_t local = {0};
    if (err) {
        *err = local;
    } else {
        err = &local;
    }
    *out = food_values_default();
    if (!text || !*text) {
        return true;
    }
    if (!parse(text, out, true, err)) {
        *out = food_values_default();
        return false;
    }
    return true;
}

static size_t write_text(char *buf, size_t cap, const char *title, const void *values, bool foods)
{
    const int count = foods ? FOODS_COUNT : TUNING_COUNT;
    int n = snprintf(buf, cap,
                     "# %s\n"
                     "# These are the built-in values. To change some, copy this file to\n"
                     "# %s in the same folder and edit the copy; lines you leave out keep\n"
                     "# their built-in value. If the copy has a mistake the pet ignores all of\n"
                     "# it and says which line.\n\n",
                     title, foods ? PET_CONFIG_FOODS_FILE : PET_CONFIG_TUNING_FILE);
    for (int i = 0; i < count; i++) {
        if (n < 0 || (size_t)n >= cap) {
            return 0;
        }
        char key[32];
        const entry_t e = foods ? food_entry(i, key, sizeof(key)) : TUNING[i];
        char left[64];
        snprintf(left, sizeof(left), "%s = %g", e.name, get(values, &e));
        if (e.help[0]) {
            n += snprintf(buf + n, cap - (size_t)n, "%-32s # %s (%g to %g)\n", left, e.help,
                          (double)e.min, (double)e.max);
        } else {
            n += snprintf(buf + n, cap - (size_t)n, "%-32s # (%g to %g)\n", left, (double)e.min,
                          (double)e.max);
        }
    }
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t pet_config_tuning_text(char *buf, size_t cap)
{
    const pet_tuning_t t = pet_tuning_default();
    return write_text(buf, cap, "The pet's numbers.", &t, false);
}

size_t pet_config_foods_text(char *buf, size_t cap)
{
    const food_values_t f = food_values_default();
    return write_text(buf, cap, "What each food is worth.", &f, true);
}

/* ---- Name lists ----------------------------------------------------------
 * "key = name, name, name". Read twice: once to check the file and count
 * what it holds, once to keep it.
 */

#define PIECE_MAX 31        /* letters in one name */
#define LIST_KEYS_MAX 24
#define WRAP_AT 76

typedef struct {
    uint8_t key;
    bool first;             /* the first name on its line */
    uint16_t line;
    char *piece;
} item_t;

typedef struct {
    item_t *item;
    int items;
    char *pool;
    size_t used;
    bool keep;
    bool mentioned[LIST_KEYS_MAX];
} gather_t;

typedef const char *(*key_name_fn)(int key, char *scratch, size_t cap);

static void gather(gather_t *g, int key, bool first, int line, const char *s, size_t len, bool lower)
{
    if (g->keep) {
        char *at = g->pool + g->used;
        for (size_t i = 0; i < len; i++) {
            at[i] = lower ? (char)tolower((unsigned char)s[i]) : s[i];
        }
        at[len] = '\0';
        g->item[g->items] = (item_t){(uint8_t)key, first, (uint16_t)line, at};
    }
    g->items++;
    g->used += len + 1;
}

static bool parse_lists(const char *text, key_name_fn key_name, int keys, size_t min_len, bool lower,
                        gather_t *g, pet_config_error_t *err)
{
    int line_no = 0;
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
    }
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t len = end ? (size_t)(end - text) : strlen(text);
        char line[200];
        line_no++;
        if (len >= sizeof(line)) {
            fail(err, line_no, "line too long");
            return false;
        }
        memcpy(line, text, len);
        line[len] = '\0';
        text += len + (end ? 1 : 0);

        /* A comment starts at a # that is not inside quotes. */
        bool quoted = false;
        for (char *c = line; *c; c++) {
            if (*c == '"') {
                quoted = !quoted;
            } else if (*c == '#' && !quoted) {
                *c = '\0';
                break;
            }
        }
        char *name = line;
        while (is_space(*name)) {
            name++;
        }
        if (!*name) {
            continue;
        }
        char *eq = strchr(name, '=');
        if (!eq) {
            fail(err, line_no, "expected name = list");
            return false;
        }
        char *tail = eq;
        while (tail > name && is_space(tail[-1])) {
            tail--;
        }
        *tail = '\0';
        int key = -1;
        for (int i = 0; i < keys && key < 0; i++) {
            char scratch[32];
            if (strcmp(name, key_name(i, scratch, sizeof(scratch))) == 0) {
                key = i;
            }
        }
        if (key < 0) {
            fail(err, line_no, "%.24s: no such list", name);
            return false;
        }
        g->mentioned[key] = true;

        char *p = eq + 1;
        bool first = true;
        for (;;) {
            while (is_space(*p)) {
                p++;
            }
            if (!*p) {
                break;      /* an empty list, or a comma left at the end: both fine */
            }
            char *start, *stop;
            if (*p == '"') {
                start = ++p;
                stop = strchr(p, '"');
                if (!stop) {
                    fail(err, line_no, "a quote is not closed");
                    return false;
                }
                p = stop + 1;
                while (is_space(*p)) {
                    p++;
                }
                if (*p && *p != ',') {
                    fail(err, line_no, "expected a comma after the closing quote");
                    return false;
                }
            } else {
                start = p;
                while (*p && *p != ',') {
                    p++;
                }
                stop = p;
                while (stop > start && is_space(stop[-1])) {
                    stop--;
                }
            }
            size_t n = (size_t)(stop - start);
            if (*p == ',') {
                p++;
            }
            if (n == 0) {
                fail(err, line_no, "%.24s: a name is missing between commas", name);
                return false;
            }
            if (n < min_len) {
                fail(err, line_no, "%.*s: too short, needs %d letters or more", (int)n, start, (int)min_len);
                return false;
            }
            if (n > PIECE_MAX) {
                fail(err, line_no, "%.16s...: too long, %d letters at most", start, PIECE_MAX);
                return false;
            }
            gather(g, key, first, line_no, start, n, lower);
            first = false;
        }
    }
    return true;
}

static void gather_free(gather_t *g)
{
    free(g->item);
    free(g->pool);
    g->item = NULL;
    g->pool = NULL;
}

static bool gather_file(const char *text, key_name_fn key_name, int keys, size_t min_len, bool lower,
                        gather_t *g, pet_config_error_t *err)
{
    memset(g, 0, sizeof(*g));
    if (!parse_lists(text, key_name, keys, min_len, lower, g, err)) {
        return false;
    }
    g->item = malloc(((size_t)g->items + 1) * sizeof(item_t));
    g->pool = malloc(g->used + 1);
    if (!g->item || !g->pool) {
        gather_free(g);
        fail(err, 0, "not enough memory");
        return false;
    }
    g->items = 0;
    g->used = 0;
    g->keep = true;
    return parse_lists(text, key_name, keys, min_len, lower, g, err);
}

/* ---- Writing a list file ---- */

typedef struct {
    char *buf;
    size_t cap, n;
    size_t col;         /* letters on the line being written; 0 = none yet */
    bool full;
} out_t;

static void add(out_t *o, const char *fmt, ...)
{
    if (o->full) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(o->buf + o->n, o->cap - o->n, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= o->cap - o->n) {
        o->full = true;
    } else {
        o->n += (size_t)k;
        o->col += (size_t)k;
    }
}

static void end_line(out_t *o)
{
    if (o->col) {
        add(o, "\n");
    }
    o->col = 0;
}

/* Adds one name to the "key = ..." line being written, starting the line if need be. */
static void add_name(out_t *o, const char *key, const char *piece, bool wrap)
{
    size_t len = strlen(piece);
    bool quote = len && (is_space(piece[0]) || is_space(piece[len - 1]) || strpbrk(piece, ",#"));
    if (wrap && o->col && o->col + len + 4 > WRAP_AT) {
        end_line(o);
    }
    if (o->col) {
        add(o, ", ");
    } else {
        add(o, "%s = ", key);
    }
    add(o, quote ? "\"%s\"" : "%s", piece);
}

/* ---- Food names ---- */

static const food_type_t NAMED_FOODS[] = {
    FOOD_WIFI_CAR, FOOD_WIFI_HOTSPOT, FOOD_WIFI_PRINTER, FOOD_BLE_CAR,
    FOOD_BLE_EARBUDS, FOOD_BLE_SPEAKER, FOOD_BLE_WATCH, FOOD_BLE_TV,
};
#define NAMED_FOODS_COUNT ((int)(sizeof(NAMED_FOODS) / sizeof(NAMED_FOODS[0])))

static const char *food_list_key(int key, char *scratch, size_t cap)
{
    food_key(NAMED_FOODS[key], scratch, cap);
    return scratch;
}

static char *s_food_pool;
static const char **s_food_lists;

bool pet_config_food_names(const char *text, pet_config_error_t *err)
{
    pet_config_error_t local = {0};
    if (err) {
        *err = local;
    } else {
        err = &local;
    }
    for (int k = 0; k < NAMED_FOODS_COUNT; k++) {
        food_names_set(NAMED_FOODS[k], NULL);
    }
    free(s_food_pool);
    free((void *)s_food_lists);
    s_food_pool = NULL;
    s_food_lists = NULL;
    if (!text || !*text) {
        return true;
    }
    gather_t g;
    /* Two letters at least: one would match nearly every name there is. */
    if (!gather_file(text, food_list_key, NAMED_FOODS_COUNT, 2, false, &g, err)) {
        return false;
    }
    const char **lists = malloc(((size_t)g.items + NAMED_FOODS_COUNT) * sizeof(*lists));
    if (!lists) {
        gather_free(&g);
        fail(err, 0, "not enough memory");
        return false;
    }
    int n = 0;
    for (int k = 0; k < NAMED_FOODS_COUNT; k++) {
        if (!g.mentioned[k]) {
            continue;       /* left out: keeps its built-in list */
        }
        const char **list = lists + n;
        for (int i = 0; i < g.items; i++) {
            if (g.item[i].key == k) {
                lists[n++] = g.item[i].piece;
            }
        }
        lists[n++] = NULL;
        food_names_set(NAMED_FOODS[k], list);
    }
    free(g.item);
    s_food_pool = g.pool;
    s_food_lists = lists;
    return true;
}

size_t pet_config_food_names_text(char *buf, size_t cap)
{
    out_t o = {.buf = buf, .cap = cap};
    add(&o,
        "# The names that tell the pet what a network or device is.\n"
        "# A network or device whose name contains one of these, whatever the\n"
        "# capitals, is that food. Cars are looked for first, so \"Ford SYNC\" is a\n"
        "# car even if another list has a piece of its name.\n"
        "#\n"
        "# These are the built-in lists. To change some, copy this file to\n"
        "# %s in the same folder and edit the copy. A food may have\n"
        "# several lines. A food you leave out keeps its built-in list; one you\n"
        "# give replaces it. Put a name in \"quotes\" to keep a space at its end.\n"
        "# If the copy has a mistake the pet ignores all of it and says which line.\n"
        "#\n"
        "# car_hotspot, hotspot and printer are Wi-Fi network names; the rest are\n"
        "# Bluetooth device names.\n",
        PET_CONFIG_FOOD_NAMES_FILE);
    for (int k = 0; k < NAMED_FOODS_COUNT; k++) {
        char key[32];
        food_key(NAMED_FOODS[k], key, sizeof(key));
        add(&o, "\n");
        o.col = 0;
        for (const char *const *piece = food_names_default(NAMED_FOODS[k]); *piece; piece++) {
            add_name(&o, key, *piece, true);
        }
        end_line(&o);
    }
    return o.full ? 0 : o.n;
}

/* ---- Place names ---- */

/* Two lists for each kind of place: its chains, then its general words. */
static const char *place_list_key(int key, char *scratch, size_t cap)
{
    snprintf(scratch, cap, key & 1 ? "%s_word" : "%s", place_kind_key((place_kind_t)(key / 2)));
    return scratch;
}

static char *s_place_pool;
static place_name_t *s_place_list;

bool pet_config_place_names(const char *text, pet_config_error_t *err)
{
    pet_config_error_t local = {0};
    if (err) {
        *err = local;
    } else {
        err = &local;
    }
    place_names_set(NULL, 0);
    free(s_place_pool);
    free(s_place_list);
    s_place_pool = NULL;
    s_place_list = NULL;
    if (!text || !*text) {
        return true;
    }
    gather_t g;
    /* Three letters at least, as shorter network names are not looked at either. */
    if (!gather_file(text, place_list_key, 2 * PLACE_KIND_COUNT, 3, true, &g, err)) {
        return false;
    }
    const place_name_t *builtin;
    int builtins = place_names_builtin(&builtin);
    place_name_t *list = malloc(((size_t)g.items + (size_t)builtins + 1) * sizeof(*list));
    if (!list) {
        gather_free(&g);
        fail(err, 0, "not enough memory");
        return false;
    }
    int n = 0;
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        if (!g.mentioned[2 * k] && !g.mentioned[2 * k + 1]) {
            /* Left out: this kind keeps its built-in list. */
            for (int i = 0; i < builtins; i++) {
                if (builtin[i].kind == k) {
                    list[n++] = builtin[i];
                }
            }
            continue;
        }
        uint16_t chain = 0;
        for (int i = 0; i < g.items; i++) {
            const item_t *it = &g.item[i];
            if (it->key / 2 != k) {
                continue;
            }
            bool word = it->key & 1;
            if (!word && it->first) {
                /* A new line is a new chain, known by its first spelling. */
                chain = place_chain_id(it->piece);
                for (int j = 0; j < n; j++) {
                    if (list[j].kind == k && list[j].chain == chain) {
                        fail(err, it->line, "%.24s: counts as the same chain as %.16s", it->piece,
                             list[j].piece);
                        free(list);
                        gather_free(&g);
                        return false;
                    }
                }
            }
            list[n++] = (place_name_t){(uint8_t)k, word ? 0 : chain, it->piece};
        }
    }
    free(g.item);
    s_place_pool = g.pool;
    s_place_list = list;
    place_names_set(list, n);
    return true;
}

size_t pet_config_place_names_text(char *buf, size_t cap)
{
    out_t o = {.buf = buf, .cap = cap};
    add(&o,
        "# Network names that mark a kind of place, for the place badges.\n"
        "# A network heard close by whose name contains one of these, whatever\n"
        "# the capitals, counts toward that badge.\n"
        "#\n"
        "#   coffee = peets, peet's     one chain and its spellings, a line per chain.\n"
        "#                              A chain counts once, ever. It is remembered\n"
        "#                              by its first spelling, so leave that as it is.\n"
        "#   coffee_word = cafe, ...    general words, which count once a day.\n"
        "#\n"
        "# These are the built-in lists. To change some, copy this file to\n"
        "# %s in the same folder and edit the copy. A kind of place\n"
        "# you leave out keeps its built-in list; one you give is replaced by what\n"
        "# you give. Names need three letters or more. If the copy has a mistake\n"
        "# the pet ignores all of it and says which line.\n",
        PET_CONFIG_PLACE_NAMES_FILE);
    const place_name_t *builtin;
    int builtins = place_names_builtin(&builtin);
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        char key[32];
        add(&o, "\n# %s\n", place_kind_name((place_kind_t)k));
        o.col = 0;
        place_list_key(2 * k, key, sizeof(key));
        uint16_t chain = 0;
        for (int i = 0; i < builtins; i++) {
            if (builtin[i].kind != k || !builtin[i].chain) {
                continue;
            }
            if (builtin[i].chain != chain) {
                end_line(&o);
                chain = builtin[i].chain;
            }
            add_name(&o, key, builtin[i].piece, false);
        }
        end_line(&o);
        place_list_key(2 * k + 1, key, sizeof(key));
        for (int i = 0; i < builtins; i++) {
            if (builtin[i].kind == k && !builtin[i].chain) {
                add_name(&o, key, builtin[i].piece, true);
            }
        }
        end_line(&o);
    }
    return o.full ? 0 : o.n;
}

/* ---- Journal sentences ----------------------------------------------------
 * "list = one sentence", a sentence to a line: sentences have commas in
 * them, so they cannot share a line the way names do. Read twice, like the
 * name lists: once to check and count, once to keep.
 */

static char *s_journal_pool;
static const char **s_journal_lines;

/*
 * One pass over the text. With pool and lines NULL it only checks and
 * counts; otherwise it copies each sentence into pool and points lines at
 * them, grouped by list in the order of first[].
 */
static bool journal_pass(const char *text, int count[JOURNAL_LIST_COUNT], size_t *bytes, char *pool,
                         const char **lines, const int first[JOURNAL_LIST_COUNT],
                         pet_config_error_t *err)
{
    int line_no = 0, placed[JOURNAL_LIST_COUNT] = {0};
    size_t used = 0;
    if (strncmp(text, "\xEF\xBB\xBF", 3) == 0) {
        text += 3;
    }
    while (*text) {
        const char *end = strchr(text, '\n');
        size_t len = end ? (size_t)(end - text) : strlen(text);
        char line[200];
        line_no++;
        if (len >= sizeof(line)) {
            fail(err, line_no, "line too long");
            return false;
        }
        memcpy(line, text, len);
        line[len] = '\0';
        text += len + (end ? 1 : 0);

        char *name = line;
        while (is_space(*name)) {
            name++;
        }
        if (!*name || *name == '#') {
            continue;       /* a sentence may hold a #, so only a whole line is a comment */
        }
        char *eq = strchr(name, '=');
        if (!eq) {
            fail(err, line_no, "expected list = sentence");
            return false;
        }
        char *tail = eq;
        while (tail > name && is_space(tail[-1])) {
            tail--;
        }
        *tail = '\0';
        int list = -1;
        for (int i = 0; i < JOURNAL_LIST_COUNT && list < 0; i++) {
            if (strcmp(name, pet_journal_list_key((pet_journal_list_t)i)) == 0) {
                list = i;
            }
        }
        if (list < 0) {
            fail(err, line_no, "%.24s: no such list", name);
            return false;
        }

        char *text_start = eq + 1;
        while (is_space(*text_start)) {
            text_start++;
        }
        size_t n = strlen(text_start);
        while (n > 0 && is_space(text_start[n - 1])) {
            n--;
        }
        text_start[n] = '\0';
        if (n == 0) {
            fail(err, line_no, "%.24s: the sentence is missing", name);
            return false;
        }
        if (n > JOURNAL_SENTENCE_MAX) {
            fail(err, line_no, "%.24s: too long, %d letters at most", name, JOURNAL_SENTENCE_MAX);
            return false;
        }
        for (size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)text_start[i];
            if (c < 0x20 || c > 0x7E) {
                fail(err, line_no, "%.24s: a letter the screen cannot show", name);
                return false;
            }
        }
        /* The blank is where the food goes: once where one is named, never elsewhere. */
        int blanks = 0;
        for (const char *b = strstr(text_start, JOURNAL_BLANK); b; b = strstr(b + 1, JOURNAL_BLANK)) {
            blanks++;
        }
        bool wants = pet_journal_list_names_food((pet_journal_list_t)list);
        if (wants && blanks != 1) {
            fail(err, line_no, "%.24s: needs %s in it, once", name, JOURNAL_BLANK);
            return false;
        }
        if (!wants && blanks != 0) {
            fail(err, line_no, "%.24s: %s does not belong in this list", name, JOURNAL_BLANK);
            return false;
        }

        if (pool) {
            memcpy(pool + used, text_start, n + 1);
            lines[first[list] + placed[list]++] = pool + used;
        } else {
            count[list]++;
        }
        used += n + 1;
    }
    if (bytes) {
        *bytes = used;
    }
    return true;
}

bool pet_config_journal(const char *text, pet_config_error_t *err)
{
    pet_config_error_t local = {0};
    if (err) {
        *err = local;
    } else {
        err = &local;
    }
    for (int i = 0; i < JOURNAL_LIST_COUNT; i++) {
        pet_journal_set((pet_journal_list_t)i, NULL, 0);
    }
    free(s_journal_pool);
    free((void *)s_journal_lines);
    s_journal_pool = NULL;
    s_journal_lines = NULL;
    if (!text || !*text) {
        return true;
    }

    int count[JOURNAL_LIST_COUNT] = {0}, first[JOURNAL_LIST_COUNT], total = 0;
    size_t bytes = 0;
    if (!journal_pass(text, count, &bytes, NULL, NULL, NULL, err)) {
        return false;
    }
    for (int i = 0; i < JOURNAL_LIST_COUNT; i++) {
        first[i] = total;
        total += count[i];
    }
    if (total == 0) {
        return true;        /* nothing but comments: the built-in sentences stay */
    }
    char *pool = malloc(bytes + 1);
    const char **lines = malloc((size_t)total * sizeof(*lines));
    if (!pool || !lines || !journal_pass(text, NULL, NULL, pool, lines, first, err)) {
        free(pool);
        free((void *)lines);
        if (!err->why[0]) {
            fail(err, 0, "not enough memory");
        }
        return false;
    }
    for (int i = 0; i < JOURNAL_LIST_COUNT; i++) {
        if (count[i]) {
            pet_journal_set((pet_journal_list_t)i, lines + first[i], count[i]);
        }
    }
    s_journal_pool = pool;
    s_journal_lines = lines;
    return true;
}

size_t pet_config_journal_text(char *buf, size_t cap)
{
    out_t o = {.buf = buf, .cap = cap};
    add(&o,
        "# What the pet writes in its journal.\n"
        "# Each day's entry is put together from these sentences: one for how the\n"
        "# day went, one for what stood out, and one for how it felt.\n"
        "#\n"
        "# These are the built-in sentences. To change some, copy this file to\n"
        "# %s in the same folder and edit the copy. Each line is\n"
        "#     list = one sentence\n"
        "# and a list has as many lines as it has sentences. A list you leave out\n"
        "# keeps its built-in sentences; one you give is replaced. Sentences can be\n"
        "# up to %d letters, in plain letters only: the screen cannot show accents.\n"
        "# In new_food, best and mostly, write %s where the food goes, once.\n"
        "# A line that starts with # is a note. If the copy has a mistake the pet\n"
        "# ignores all of it and says which line.\n",
        PET_CONFIG_JOURNAL_FILE, JOURNAL_SENTENCE_MAX, JOURNAL_BLANK);
    for (int i = 0; i < JOURNAL_LIST_COUNT; i++) {
        const char *const *lines;
        int n = pet_journal_builtin((pet_journal_list_t)i, &lines);
        add(&o, "\n# %s\n", pet_journal_list_when((pet_journal_list_t)i));
        for (int k = 0; k < n; k++) {
            add(&o, "%s = %s\n", pet_journal_list_key((pet_journal_list_t)i), lines[k]);
        }
    }
    return o.full ? 0 : o.n;
}
