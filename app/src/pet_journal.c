/*
 * Journal: a short entry in the pet's voice for one day, put together from
 * sentences chosen by that day's numbers. Nothing is stored but the
 * numbers, so an entry is rebuilt each time it is shown, and the same day
 * always reads the same.
 *
 * The sentences are in lists, one for each thing a day can be. The lists
 * built in here can be replaced, a list at a time, from journal.txt on the
 * card (pet_config.h).
 */
#include <stdio.h>
#include <string.h>

#include "pet_core.h"

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* How the day went for the stomach. */
static const char *const NOTHING_YET[] = {"Nothing to eat yet today."};
static const char *const NOTHING[] = {
    "Nothing to eat all day.", "Not a crumb today.", "A very quiet day. Too quiet.",
    "Listened all day and heard nothing.",
};
static const char *const QUIET[] = {
    "A slow day.", "Slim pickings today.", "Just a few nibbles.", "Not much about today.",
    "A quiet one.",
};
static const char *const NORMAL[] = {
    "A decent day of snacking.", "Ate well enough.", "Plenty to nibble on.",
    "A steady sort of day.", "Kept my belly busy.",
};
static const char *const BUSY[] = {
    "So much food!", "What a feast.", "Busy, busy, busy.", "There was food everywhere.",
    "I could hardly keep up.",
};

/* What stood out. */
static const char *const GREW[] = {
    "I feel different. Bigger!", "Something changed in me today.", "I grew!",
};
static const char *const DROVE[] = {
    "Went for a drive today.", "We went somewhere fast.", "Cars! I love cars.",
    "The world rushed past.",
};
static const char *const NEW_PLACE[] = {
    "We went somewhere new!", "A whole new place today.", "New smells. New everything.",
    "I have never been there before.",
};
static const char *const WENT_OUT[] = {
    "We went out today.", "Got out of the house.", "Nice to be out and about.",
};
static const char *const NEW_FOOD[] = {
    "Tried {food} for the first time.", "First ever taste of {food}.", "Something new today: {food}.",
};
static const char *const BEST[] = {
    "The best bit was {food}.", "Found {food}, which was a treat.", "Best find: {food}.",
    "I keep thinking about {food}.",
};
static const char *const MOSTLY[] = {
    "Mostly {food}.", "It was nearly all {food}.", "Lots of {food}.",
};

/* How it felt. */
static const char *const QUEASY[] = {
    "Too much junk food. Ugh.", "My tummy hurts.", "No more open networks, please.",
};
static const char *const HUNGRY[] = {
    "My belly rumbled for a while.", "I was hungry for a bit.", "Could have eaten more.",
};
static const char *const CRAVING[] = {
    "And I got just what I wanted.", "Got my craving, too.", "Exactly what I fancied.",
};
static const char *const CLOSE_LAZY[] = {"Good day for a nap.", "Then I dozed."};
static const char *const CLOSE_GREEDY[] = {"More tomorrow, I hope.", "I could eat it all again."};
static const char *const CLOSE_CURIOUS[] = {"I wonder what is out there.", "Let's go somewhere new."};
static const char *const CLOSE_PLAYFUL[] = {"I bounced a lot.", "Fun day."};
static const char *const CLOSE_SHY[] = {"Glad to be home.", "It was a lot of noise."};
static const char *const CLOSE_CHEERFUL[] = {"A good day!", "I like it here."};

#define LIST(key, blank, when, lines) {key, blank, when, lines, COUNT(lines)}

static const struct {
    const char *key;            /* its name in journal.txt */
    bool blank;                 /* each sentence names a food: it holds {food} once */
    const char *when;           /* for the reference file */
    const char *const *lines;
    int count;
} LISTS[JOURNAL_LIST_COUNT] = {
    [JOURNAL_NOTHING_YET] = LIST("nothing_yet", false, "today so far, with nothing eaten", NOTHING_YET),
    [JOURNAL_NOTHING]     = LIST("nothing", false, "a day with nothing to eat", NOTHING),
    [JOURNAL_QUIET]       = LIST("quiet", false, "a day of fewer than 15 foods", QUIET),
    [JOURNAL_NORMAL]      = LIST("normal", false, "an ordinary day", NORMAL),
    [JOURNAL_BUSY]        = LIST("busy", false, "a day of more than 80 foods", BUSY),
    [JOURNAL_GREW]        = LIST("grew", false, "the day it hatched, grew or changed form", GREW),
    [JOURNAL_NEW_PLACE]   = LIST("new_place", false, "it settled somewhere new", NEW_PLACE),
    [JOURNAL_DROVE]       = LIST("drove", false, "it ate a car", DROVE),
    [JOURNAL_NEW_FOOD]    = LIST("new_food", true, "it tasted a food for the first time", NEW_FOOD),
    [JOURNAL_BEST]        = LIST("best", true, "the best thing it found", BEST),
    [JOURNAL_WENT_OUT]    = LIST("went_out", false, "it left home", WENT_OUT),
    [JOURNAL_MOSTLY]      = LIST("mostly", true, "one kind of food was most of the day", MOSTLY),
    [JOURNAL_QUEASY]      = LIST("queasy", false, "it ate too much junk", QUEASY),
    [JOURNAL_HUNGRY]      = LIST("hungry", false, "it went hungry for a while", HUNGRY),
    [JOURNAL_CRAVING]     = LIST("craving", false, "it got what it was craving", CRAVING),
    [JOURNAL_LAZY]        = LIST("lazy", false, "the last line of an ordinary day, for a lazy pet", CLOSE_LAZY),
    [JOURNAL_GREEDY]      = LIST("greedy", false, "...for a greedy pet", CLOSE_GREEDY),
    [JOURNAL_CURIOUS]     = LIST("curious", false, "...for a curious pet", CLOSE_CURIOUS),
    [JOURNAL_PLAYFUL]     = LIST("playful", false, "...for a playful pet", CLOSE_PLAYFUL),
    [JOURNAL_SHY]         = LIST("shy", false, "...for a shy pet", CLOSE_SHY),
    [JOURNAL_CHEERFUL]    = LIST("cheerful", false, "...for a cheerful pet", CLOSE_CHEERFUL),
};

/* The lists in use: the owner's where the card gave one, otherwise NULL for the built-in. */
static const char *const *s_lines[JOURNAL_LIST_COUNT];
static int s_count[JOURNAL_LIST_COUNT];

const char *pet_journal_list_key(pet_journal_list_t list)
{
    return list < JOURNAL_LIST_COUNT ? LISTS[list].key : "";
}

const char *pet_journal_list_when(pet_journal_list_t list)
{
    return list < JOURNAL_LIST_COUNT ? LISTS[list].when : "";
}

bool pet_journal_list_names_food(pet_journal_list_t list)
{
    return list < JOURNAL_LIST_COUNT && LISTS[list].blank;
}

int pet_journal_builtin(pet_journal_list_t list, const char *const **lines)
{
    if (list >= JOURNAL_LIST_COUNT) {
        return 0;
    }
    *lines = LISTS[list].lines;
    return LISTS[list].count;
}

void pet_journal_set(pet_journal_list_t list, const char *const *lines, int count)
{
    if (list < JOURNAL_LIST_COUNT) {
        s_lines[list] = lines && count > 0 ? lines : NULL;
        s_count[list] = lines && count > 0 ? count : 0;
    }
}

/* One sentence from a list, chosen by r. */
static const char *pick(pet_journal_list_t list, uint32_t r)
{
    if (s_lines[list]) {
        return s_lines[list][r % (uint32_t)s_count[list]];
    }
    return LISTS[list].lines[r % (uint32_t)LISTS[list].count];
}

static const char *a_food(food_type_t type)
{
    switch (type) {
    case FOOD_WIFI_SECURED: return "a plain network";
    case FOOD_WIFI_WPA3:    return "a crunchy new network";
    case FOOD_WIFI_OPEN:    return "an open network";
    case FOOD_WIFI_PRINTER: return "a printer";
    case FOOD_WIFI_HOTSPOT: return "a phone hotspot";
    case FOOD_WIFI_HIDDEN:  return "a hidden network";
    case FOOD_WIFI_CAR:     return "a car's Wi-Fi";
    case FOOD_BLE_UNKNOWN:  return "some crumbs";
    case FOOD_BLE_PHONE:    return "a phone";
    case FOOD_BLE_WATCH:    return "a watch";
    case FOOD_BLE_EARBUDS:  return "some earbuds";
    case FOOD_BLE_TRACKER:  return "a tracker";
    case FOOD_BLE_TV:       return "a TV";
    case FOOD_BLE_SPEAKER:  return "a speaker";
    case FOOD_BLE_CAR:      return "a car";
    default:                return "something";
    }
}

static int add(char *out, size_t cap, int len, const char *text)
{
    if (len >= (int)cap) {
        return len;
    }
    return len + snprintf(out + len, cap - (size_t)len, "%s%s", len ? " " : "", text);
}

/* Adds a sentence with its {food} filled in. One without the blank is added as it is. */
static int add_about(char *out, size_t cap, int len, const char *sentence, const char *food)
{
    char line[JOURNAL_SENTENCE_MAX + 40];
    const char *blank = strstr(sentence, JOURNAL_BLANK);
    if (!blank) {
        return add(out, cap, len, sentence);
    }
    snprintf(line, sizeof(line), "%.*s%s%s", (int)(blank - sentence), sentence, food,
             blank + strlen(JOURNAL_BLANK));
    return add(out, cap, len, line);
}

void pet_journal_entry(const pet_t *pet, const pet_day_t *d, bool in_progress,
                       char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    uint32_t r1 = mix(d->day * 3 + 1), r2 = mix(d->day * 3 + 2), r3 = mix(d->day * 3 + 3);
    int len = 0;

    if (d->foods == 0) {
        add(out, cap, 0, pick(in_progress ? JOURNAL_NOTHING_YET : JOURNAL_NOTHING, r1));
        return;
    }
    len = add(out, cap, len,
              pick(d->foods < 15 ? JOURNAL_QUIET : (d->foods > 80 ? JOURNAL_BUSY : JOURNAL_NORMAL), r1));

    /* The one thing worth telling, most notable first. */
    uint32_t total = 0;
    int top = 0;
    for (int g = 0; g < GROUP_COUNT; g++) {
        total += d->group_pts[g];
        if (d->group_pts[g] > d->group_pts[top]) {
            top = g;
        }
    }
    if (d->flags & DAY_GREW) {
        len = add(out, cap, len, pick(JOURNAL_GREW, r2));
    } else if (d->flags & DAY_NEW_PLACE) {
        len = add(out, cap, len, pick(JOURNAL_NEW_PLACE, r2));
    } else if (d->group_pts[GROUP_CARS] > 0 || d->best == FOOD_WIFI_CAR) {
        len = add(out, cap, len, pick(JOURNAL_DROVE, r2));
    } else if (d->new_foods > 0 && d->best < FOOD_TYPE_COUNT) {
        len = add_about(out, cap, len, pick(JOURNAL_NEW_FOOD, r2), a_food((food_type_t)d->best));
    } else if (d->best < FOOD_TYPE_COUNT && d->best != FOOD_BLE_UNKNOWN &&
               d->best != FOOD_WIFI_SECURED && (r2 & 0x100)) {
        len = add_about(out, cap, len, pick(JOURNAL_BEST, r2), a_food((food_type_t)d->best));
    } else if ((d->flags & DAY_OUT) && (r2 & 0x200)) {
        len = add(out, cap, len, pick(JOURNAL_WENT_OUT, r2));
    } else if (total > 0 && d->group_pts[top] * 2 > total) {
        len = add_about(out, cap, len, pick(JOURNAL_MOSTLY, r2), pet_group_name((pet_group_t)top));
    }

    if (d->flags & DAY_QUEASY) {
        len = add(out, cap, len, pick(JOURNAL_QUEASY, r3));
    } else if (d->flags & DAY_HUNGRY) {
        len = add(out, cap, len, pick(JOURNAL_HUNGRY, r3));
    } else if (d->flags & DAY_CRAVING) {
        len = add(out, cap, len, pick(JOURNAL_CRAVING, r3));
    } else if (!in_progress) {
        /* An ordinary day ends on a line from one of the pet's traits. */
        int which = (int)((r3 >> 8) % PET_TRAITS_PER_PET), seen = 0;
        for (int t = 0; t < TRAIT_COUNT; t++) {
            if (!pet_has_trait(pet, (pet_trait_t)t) || seen++ != which) {
                continue;
            }
            len = add(out, cap, len, pick((pet_journal_list_t)(JOURNAL_LAZY + t), r3));
        }
    }
    (void)len;
}
