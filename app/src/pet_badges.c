/*
 * Badges: small achievements worked out from the pet's own counts. They
 * are display only. Place badges (coffee shops, airports...) are tiered
 * and live with habitats in pet_places.c.
 */
#include "pet_core.h"

static const struct {
    const char *name;
    const char *how;
} BADGES[BADGE_COUNT] = {
    [BADGE_FIRST_BITE]    = {"First bite", "eat a first meal"},
    [BADGE_HATCHED]       = {"Out of the shell", "hatch from the egg"},
    [BADGE_GROWN_UP]      = {"All grown up", "reach an adult form"},
    [BADGE_FIRST_CAR]     = {"Road trip", "eat a car"},
    [BADGE_LUCKY_FIND]    = {"Lucky find", "find a hidden network worth 5"},
    [BADGE_RARE_3]        = {"Fine dining", "three rare foods in one day"},
    [BADGE_FEAST]         = {"Feast", "50 foods in one day"},
    [BADGE_BALANCED_DAY]  = {"Balanced day", "five food groups in one day"},
    [BADGE_WELL_FED_WEEK] = {"Well fed", "a week without going hungry"},
    [BADGE_PHONES_100]    = {"Chatterbox", "eat 100 phones"},
    [BADGE_MEALS_100]     = {"Regular", "100 meals"},
    [BADGE_MEALS_1000]    = {"Bottomless", "1,000 meals"},
    [BADGE_ALL_WIFI]      = {"Wi-Fi gourmet", "taste every Wi-Fi food"},
    [BADGE_ALL_BLUETOOTH] = {"Bluetooth gourmet", "taste every Bluetooth food"},
    [BADGE_FULL_DEX]      = {"Tried everything", "fill the food-dex"},
    [BADGE_RETIRED]       = {"Happy retirement", "raise a pet to retirement"},
    [BADGE_PRISM]         = {"Prism", "raise a pet into a Prism"},
    [BADGE_ALL_FORMS]     = {"Full house", "retire each of the six adult forms"},
    [BADGE_GEN_5]         = {"Dynasty", "reach the fifth generation"},
    [BADGE_MOVED]         = {"Moving day", "move a pet to another device"},
};

const char *pet_badge_name(pet_badge_t badge)
{
    return badge < BADGE_COUNT ? BADGES[badge].name : "?";
}

const char *pet_badge_how(pet_badge_t badge)
{
    return badge < BADGE_COUNT ? BADGES[badge].how : "";
}

void pet_badge_award(pet_t *pet, pet_badge_t badge)
{
    uint32_t bit = 1u << badge;
    if (!(pet->badges & bit)) {
        pet->badges |= bit;
        pet->new_badges |= bit;
    }
}

uint32_t pet_take_new_badges(pet_t *pet)
{
    uint32_t fresh = pet->new_badges;
    pet->new_badges = 0;
    return fresh;
}

static void award_if(pet_t *pet, bool earned, pet_badge_t badge)
{
    if (earned) {
        pet_badge_award(pet, badge);
    }
}

/*
 * Called after each eating moment, and with day_ended just before today's
 * record joins the week.
 */
void pet_badges_check(pet_t *pet, bool day_ended)
{
    const pet_day_t *d = &pet->today;

    award_if(pet, pet->meals >= 1, BADGE_FIRST_BITE);
    award_if(pet, pet->stage >= STAGE_HATCHLING, BADGE_HATCHED);
    award_if(pet, pet->stage >= STAGE_ADULT, BADGE_GROWN_UP);
    award_if(pet, pet->tasted[FOOD_WIFI_CAR] + pet->tasted[FOOD_BLE_CAR] > 0, BADGE_FIRST_CAR);
    award_if(pet, pet->rare_today >= 3, BADGE_RARE_3);
    award_if(pet, d->foods >= 50, BADGE_FEAST);
    award_if(pet, pet->tasted[FOOD_BLE_PHONE] >= 100, BADGE_PHONES_100);
    award_if(pet, pet->meals >= 100, BADGE_MEALS_100);
    award_if(pet, pet->meals >= 1000, BADGE_MEALS_1000);

    /* The line: these look at the generations, not only at this pet. */
    const uint16_t six = (1u << FORM_HOMEBODY) | (1u << FORM_ROAMER) | (1u << FORM_SCAVENGER) |
                         (1u << FORM_SINGER) | (1u << FORM_RACER) | (1u << FORM_GLOWBUG);
    award_if(pet, pet->stage == STAGE_ADULT && pet->form == FORM_PRISM, BADGE_PRISM);
    award_if(pet, (pet->farm.forms & six) == six, BADGE_ALL_FORMS);
    award_if(pet, pet->generation >= 5, BADGE_GEN_5);

    int groups = 0;
    for (int g = 0; g < GROUP_COUNT; g++) {
        groups += d->group_pts[g] > 0;
    }
    award_if(pet, groups >= 5, BADGE_BALANCED_DAY);

    bool wifi = true, ble = true;
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        if (pet->tasted[i] == 0) {
            if (food_is_ble((food_type_t)i)) {
                ble = false;
            } else {
                wifi = false;
            }
        }
    }
    award_if(pet, wifi, BADGE_ALL_WIFI);
    award_if(pet, ble, BADGE_ALL_BLUETOOTH);
    award_if(pet, wifi && ble, BADGE_FULL_DEX);

    /* Today plus the six days before it, with no gaps, fed and never hungry. */
    if (day_ended && pet->stage != STAGE_EGG) {
        bool week = d->foods > 0 && !(d->flags & DAY_HUNGRY);
        for (int i = 0; i < 6 && week; i++) {
            const pet_day_t *h = &pet->history[i];
            week = h->day == d->day - 1 - (uint32_t)i && h->foods > 0 && !(h->flags & DAY_HUNGRY);
        }
        award_if(pet, week, BADGE_WELL_FED_WEEK);
    }
}
