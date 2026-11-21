/*
 * Journal: a short entry in the pet's voice for one day, put together from
 * fragments chosen by that day's numbers. Nothing is stored but the
 * numbers, so an entry is rebuilt each time it is shown, and the same day
 * always reads the same.
 *
 * The fragment lists will move to a file on the card with the other text.
 */
#include <stdio.h>

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
    "Tried %s for the first time.", "First ever taste of %s.", "Something new today: %s.",
};
static const char *const BEST[] = {
    "The best bit was %s.", "Found %s, which was a treat.", "Best find: %s.",
    "I keep thinking about %s.",
};
static const char *const MOSTLY[] = {
    "Mostly %s.", "It was nearly all %s.", "Lots of %s.",
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

#define PICK(list, r) ((list)[(r) % (uint32_t)COUNT(list)])

void pet_journal_entry(const pet_t *pet, const pet_day_t *d, bool in_progress,
                       char *out, size_t cap)
{
    if (cap == 0) {
        return;
    }
    out[0] = '\0';
    uint32_t r1 = mix(d->day * 3 + 1), r2 = mix(d->day * 3 + 2), r3 = mix(d->day * 3 + 3);
    char line[80];
    int len = 0;

    if (d->foods == 0) {
        add(out, cap, 0, in_progress ? "Nothing to eat yet today." : PICK(NOTHING, r1));
        return;
    }
    len = add(out, cap, len,
              d->foods < 15 ? PICK(QUIET, r1) : (d->foods > 80 ? PICK(BUSY, r1) : PICK(NORMAL, r1)));

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
        len = add(out, cap, len, PICK(GREW, r2));
    } else if (d->flags & DAY_NEW_PLACE) {
        len = add(out, cap, len, PICK(NEW_PLACE, r2));
    } else if (d->group_pts[GROUP_CARS] > 0 || d->best == FOOD_WIFI_CAR) {
        len = add(out, cap, len, PICK(DROVE, r2));
    } else if (d->new_foods > 0 && d->best < FOOD_TYPE_COUNT) {
        snprintf(line, sizeof(line), PICK(NEW_FOOD, r2), a_food((food_type_t)d->best));
        len = add(out, cap, len, line);
    } else if (d->best < FOOD_TYPE_COUNT && d->best != FOOD_BLE_UNKNOWN &&
               d->best != FOOD_WIFI_SECURED && (r2 & 0x100)) {
        snprintf(line, sizeof(line), PICK(BEST, r2), a_food((food_type_t)d->best));
        len = add(out, cap, len, line);
    } else if ((d->flags & DAY_OUT) && (r2 & 0x200)) {
        len = add(out, cap, len, PICK(WENT_OUT, r2));
    } else if (total > 0 && d->group_pts[top] * 2 > total) {
        snprintf(line, sizeof(line), PICK(MOSTLY, r2), pet_group_name((pet_group_t)top));
        len = add(out, cap, len, line);
    }

    if (d->flags & DAY_QUEASY) {
        len = add(out, cap, len, PICK(QUEASY, r3));
    } else if (d->flags & DAY_HUNGRY) {
        len = add(out, cap, len, PICK(HUNGRY, r3));
    } else if (d->flags & DAY_CRAVING) {
        len = add(out, cap, len, PICK(CRAVING, r3));
    } else if (!in_progress) {
        /* An ordinary day ends on a line from one of the pet's traits. */
        int which = (int)((r3 >> 8) % PET_TRAITS_PER_PET), seen = 0;
        for (int t = 0; t < TRAIT_COUNT; t++) {
            if (!pet_has_trait(pet, (pet_trait_t)t) || seen++ != which) {
                continue;
            }
            switch (t) {
            case TRAIT_LAZY:     len = add(out, cap, len, PICK(CLOSE_LAZY, r3)); break;
            case TRAIT_GREEDY:   len = add(out, cap, len, PICK(CLOSE_GREEDY, r3)); break;
            case TRAIT_CURIOUS:  len = add(out, cap, len, PICK(CLOSE_CURIOUS, r3)); break;
            case TRAIT_PLAYFUL:  len = add(out, cap, len, PICK(CLOSE_PLAYFUL, r3)); break;
            case TRAIT_SHY:      len = add(out, cap, len, PICK(CLOSE_SHY, r3)); break;
            default:             len = add(out, cap, len, PICK(CLOSE_CHEERFUL, r3)); break;
            }
        }
    }
    (void)len;
}
