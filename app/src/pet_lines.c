/*
 * The words the pages work out from the pet's numbers, in one place: the
 * watch's pages (pet_pages.c) set them in LVGL labels, and a platform that
 * draws its own screens reads the very same ones through pet_ui.h.
 */
#include "pet_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pet_pages.h"

/* Adds to the end of what is in buf, and stops quietly when it is full. */
static void add(char *buf, size_t cap, const char *fmt, ...)
{
    size_t len = strlen(buf);
    if (len + 1 >= cap) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + len, cap - len, fmt, args);
    va_end(args);
}

const pet_day_t *pet_app_day(uint32_t day)
{
    const pet_t *pet = pet_app_pet();
    if (pet->today.day == day) {
        return &pet->today;
    }
    for (int i = 0; i < PET_HISTORY_DAYS; i++) {
        if (pet->history[i].day == day) {
            return &pet->history[i];
        }
    }
    return NULL;
}

/* Counted the way the Badges screen counts them, place badges included. */
static int badges_earned(const pet_t *pet)
{
    int n = 0;
    for (int b = 0; b < BADGE_COUNT; b++) {
        n += pet_has_badge(pet, (pet_badge_t)b);
    }
    for (int k = 0; k < PLACE_KIND_COUNT; k++) {
        n += pet->places.kind[k].count > 0;
    }
    return n + (pet->places.offgrid_tier > 0) + (pet->farm.walk_tier > 0) + (pet->farm.ride_tier > 0);
}

/* The plain badges, a place badge for each kind, off the grid, and the two for distance. */
#define BADGES_IN_ALL (BADGE_COUNT + PLACE_KIND_COUNT + 1 + 2)

static void stage_text(const pet_t *pet, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s%s%s", pet_stage_name(pet->stage), pet->form != FORM_NONE ? " " : "",
             pet_form_name(pet->form));
}

static void growth_days_text(const pet_t *pet, char *buf, size_t cap)
{
    int days_done, days_needed, meals_done, meals_needed;
    pet_growth_counts(pet, &days_done, &days_needed, &meals_done, &meals_needed);
    if (pet->stage == STAGE_ADULT && pet->retire_ready) {
        snprintf(buf, cap, "A full life lived.");
        return;
    }
    if (days_needed <= 0) {
        return;
    }
    snprintf(buf, cap, "day %d of %d", days_done, days_needed);
    /*
     * These are the pet's days. At any speed but the slowest they are
     * shorter than ours, so say how long it really is.
     */
    uint32_t left = pet_growth_seconds_left(pet);
    if (pet->tune.growth_speed > 1 && left > 0) {
        if (left >= 36 * 3600) {
            add(buf, cap, ", about %d real days left", (int)((left + 43200) / 86400));
        } else if (left > 3600) {
            add(buf, cap, ", about %d hours left", (int)((left + 1800) / 3600));
        } else {
            add(buf, cap, ", under an hour left");
        }
    }
}

/* What has been missing from the week: the point of the diet chart. */
static void missing_text(const pet_t *pet, char *buf, size_t cap)
{
    uint32_t week[GROUP_COUNT] = {0};
    for (int i = 0; i < 7; i++) {
        const pet_day_t *d = pet_app_day(pet->today.day - 6 + (uint32_t)i);
        for (int g = 0; d && g < GROUP_COUNT; g++) {
            week[g] += d->group_pts[g];
        }
    }
    for (int g = 0; g < GROUP_OTHER; g++) {
        if (week[g] == 0) {
            add(buf, cap, "%s%s", buf[0] ? ", " : "none this week: ", pet_group_name((pet_group_t)g));
        }
    }
}

static void retiring_text(const pet_t *pet, char *buf, size_t cap)
{
    if (pet_can_retire(pet)) {
        snprintf(buf, cap, "Retire %s...", pet->name[0] ? pet->name : "this pet");
    } else if (pet->stage == STAGE_ADULT) {
        int days, days_needed, meals, meals_needed;
        pet_growth_counts(pet, &days, &days_needed, &meals, &meals_needed);
        snprintf(buf, cap, "%s can retire after a full adult life: day %d of %d, meal %d of %d.",
                 pet->name[0] ? pet->name : "This pet", days, days_needed, meals, meals_needed);
    } else {
        snprintf(buf, cap, "Generation %d is still growing up.", pet->generation);
    }
}

bool pet_app_line(pet_line_t which, char *buf, size_t cap)
{
    const pet_t *pet = pet_app_pet();
    const pet_settings_t *set = pet_app_settings();
    bool notice = false;
    int n = 0;
    if (!buf || cap == 0) {
        return false;
    }
    buf[0] = '\0';
    switch (which) {
    case PET_LINE_MOOD:
        snprintf(buf, cap, "%s", pet_app_mood_text());
        break;
    case PET_LINE_BELL: {
        const char *craving = pet_craving_text(pet);
        snprintf(buf, cap, "%s", craving[0] ? craving : (pet_can_retire(pet) ? "ready to retire" : ""));
        notice = buf[0] != '\0';
        break;
    }
    case PET_LINE_STAGE:
        stage_text(pet, buf, cap);
        break;
    case PET_LINE_GROWTH_DAYS:
        growth_days_text(pet, buf, cap);
        break;
    case PET_LINE_GROWTH_MEALS:
        if (!(pet->stage == STAGE_ADULT && pet->retire_ready)) {
            int days_done, days_needed, meals_done, meals_needed;
            pet_growth_counts(pet, &days_done, &days_needed, &meals_done, &meals_needed);
            snprintf(buf, cap, "meals %d of %d", meals_done, meals_needed);
        }
        break;
    case PET_LINE_LEANING:
        if (pet->stage < STAGE_ADULT) {
            snprintf(buf, cap, "drifting toward %s", pet_form_name(pet_form_leaning(pet)));
        }
        break;
    case PET_LINE_MISSING:
        missing_text(pet, buf, cap);
        notice = buf[0] != '\0';
        break;
    case PET_LINE_AGE: {
        float speed = pet->tune.growth_speed > 0 ? pet->tune.growth_speed : 1;
        int days = (int)((pet->now - pet->born) * speed / 86400.0f);
        snprintf(buf, cap, days == 1 ? "%d day" : "%d days", days);
        break;
    }
    case PET_LINE_TRAITS:
        for (int t = 0; t < TRAIT_COUNT; t++) {
            if (pet_has_trait(pet, (pet_trait_t)t)) {
                add(buf, cap, "%s%s", buf[0] ? ", " : "", pet_trait_name((pet_trait_t)t));
            }
        }
        break;
    case PET_LINE_EATS_MOSTLY: {
        float g[GROUP_COUNT];
        int top = 0;
        pet_group_points(pet, g);
        for (int i = 1; i < GROUP_COUNT; i++) {
            if (g[i] > g[top]) {
                top = i;
            }
        }
        snprintf(buf, cap, "%s", g[top] > 0 ? pet_group_name((pet_group_t)top) : "nothing yet");
        break;
    }
    case PET_LINE_BADGES:
        snprintf(buf, cap, "%d of %d", badges_earned(pet), BADGES_IN_ALL);
        break;
    case PET_LINE_FAVOURITE: {
        /* Short names, so the watch's button fits at either text size. */
        static const char *const SHORT[PET_FAVOURITE_MAX + 1] = {
            "none", "networks", "hotspots", "music", "cars", "phones",
        };
        uint8_t f = pet->favourite;
        snprintf(buf, cap, "Favourite: %s", SHORT[f <= PET_FAVOURITE_MAX ? f : 0]);
        break;
    }
    case PET_LINE_HERE: {
        const pet_places_t *pl = &pet->places;
        if (pl->here >= 0) {
            char label[HABITAT_NAME_MAX + 4];
            places_label(pl, pl->here, label, sizeof(label));
            snprintf(buf, cap, "Here now: %s", label);
        } else if (pl->cand_prints) {
            snprintf(buf, cap, "Somewhere it does not know. Stay a while and it will remember.");
            notice = true;
        } else {
            snprintf(buf, cap, "On the move.");
        }
        break;
    }
    case PET_LINE_RETIRING:
        retiring_text(pet, buf, cap);
        notice = pet_can_retire(pet);
        break;
    case PET_LINE_TILE_DEX:
        for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
            n += pet->tasted[i] > 0;
        }
        snprintf(buf, cap, "%d of %d tasted", n, FOOD_TYPE_COUNT);
        break;
    case PET_LINE_TILE_BADGES:
        snprintf(buf, cap, "%d of %d earned", badges_earned(pet), BADGES_IN_ALL);
        notice = pet->new_badges != 0;      /* one it has not been shown yet */
        break;
    case PET_LINE_TILE_PLACES:
        n = places_count(&pet->places);
        snprintf(buf, cap, n == 1 ? "%d habitat" : "%d habitats", n);
        break;
    case PET_LINE_TILE_FARM:
        n = pet->farm.count + pet->farm.moved_on;
        if (pet_can_retire(pet)) {
            snprintf(buf, cap, "ready to retire");
            notice = true;
        } else if (n == 0) {
            snprintf(buf, cap, "empty");
        } else {
            snprintf(buf, cap, "%d retired", n);
        }
        break;
    case PET_LINE_TILE_PET:
        snprintf(buf, cap, "%s", pet->name[0] ? pet->name
                                 : (pet->form != FORM_NONE ? pet_form_name(pet->form)
                                                           : pet_stage_name(pet->stage)));
        break;
    case PET_LINE_TILE_SETTINGS:
        if (set->dev_log) {
            snprintf(buf, cap, "dev log on");
            notice = true;
        } else if (set->paused) {
            snprintf(buf, cap, "pet paused");
            notice = true;
        }
        break;
    default:
        break;
    }
    return notice;
}

/* ---- Settings ------------------------------------------------------------ */

const char *pet_app_setting_label(pet_setting_id_t id)
{
    static const char *const LABEL[SET_COUNT] = {
        [SET_BRIGHTNESS] = "Brightness", [SET_TEXT] = "Text size",
        [SET_SOUND] = "Sound", [SET_VOLUME] = "Volume",
        [SET_CARD] = "Card", [SET_DEV_LOG] = "Dev log",
        [SET_QUIET_START] = "Quiet from", [SET_QUIET_END] = "Quiet until",
        [SET_HOUR] = "Clock hour", [SET_MINUTE] = "Clock minute",
        [SET_CARRY] = "Screen", [SET_PAUSE] = "Pet", [SET_LIFE_SPEED] = "Life speed",
        [SET_RAISE] = "Wrist raise", [SET_TOUCH] = "Touch",
    };
    return (unsigned)id < SET_COUNT ? LABEL[id] : "";
}

void pet_app_setting_text(pet_setting_id_t id, char *buf, size_t cap)
{
    const pet_settings_t *s = pet_app_settings();
    const pet_hal_t *hal = pet_app_hal();
    uint32_t now = hal ? hal->now_s() : 0;
    if (!buf || cap == 0) {
        return;
    }
    switch (id) {
    case SET_BRIGHTNESS:  snprintf(buf, cap, "%d%%", s->brightness); break;
    case SET_TEXT:        snprintf(buf, cap, "%s", s->text_large ? "large" : "normal"); break;
    case SET_SOUND:       snprintf(buf, cap, "%s", s->sound == 0 ? "off" : (s->sound == 1 ? "rare" : "all")); break;
    case SET_VOLUME:      snprintf(buf, cap, "%d of 5", s->volume); break;
    case SET_CARD:        snprintf(buf, cap, "%s", s->card_shared ? "computer" : "pet"); break;
    case SET_DEV_LOG:
        snprintf(buf, cap, "%s", !s->dev_log ? "off" : (hal && hal->dev_log_on ? hal->dev_log_on : "names"));
        break;
    case SET_QUIET_START: snprintf(buf, cap, "%02d:00", s->quiet_start); break;
    case SET_QUIET_END:   snprintf(buf, cap, "%02d:00", s->quiet_end); break;
    case SET_HOUR:        snprintf(buf, cap, "%02d", (int)(now % 86400 / 3600)); break;
    case SET_MINUTE:      snprintf(buf, cap, "%02d", (int)(now % 3600 / 60)); break;
    case SET_CARRY:       snprintf(buf, cap, "%s", s->carry ? "sleeps" : "stays on"); break;
    case SET_PAUSE:       snprintf(buf, cap, "%s", s->paused ? "paused" : "awake"); break;
    case SET_RAISE:       snprintf(buf, cap, "%s", s->raise_off ? "off" : "wakes"); break;
    case SET_TOUCH:       snprintf(buf, cap, "%s", s->touch_off ? "off" : "wakes"); break;
    case SET_LIFE_SPEED:
        snprintf(buf, cap, "%s", s->speed_fixed ? "by file"
                                 : (s->life_speed == LIFE_FAST ? "fast"
                                    : (s->life_speed == LIFE_MEDIUM ? "medium" : "slow")));
        break;
    default:              buf[0] = '\0'; break;
    }
}

bool pet_app_setting_shown(pet_setting_id_t id)
{
    const pet_hal_t *hal = pet_app_hal();
    if (!hal) {
        return false;
    }
    switch (id) {
    case SET_HOUR:
    case SET_MINUTE:     return hal->adjust_clock != NULL;
    case SET_CARD:       return hal->share_card != NULL;
    case SET_DEV_LOG:    return hal->set_dev_log != NULL;
    case SET_BRIGHTNESS: return hal->set_brightness != NULL;
    case SET_RAISE:      return hal->motion != NULL && !hal->screen_managed;
    case SET_CARRY:
    case SET_TOUCH:      return !hal->screen_managed;
    case SET_TEXT:       return !hal->pet_only;     /* it sizes the pages, and there are none */
    default:             return (unsigned)id < SET_COUNT;
    }
}

/* ---- Lists ---------------------------------------------------------------- */

void pet_app_recent_text(int i, char *buf, size_t cap)
{
    const pet_recent_t *list;
    int n = pet_app_recent(&list);
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (i < 0 || i >= n) {
        return;
    }
    const pet_recent_t *r = &list[i];
    snprintf(buf, cap, "%s", food_label(r->best));
    if (r->foods > 1) {
        add(buf, cap, "  +%d", r->foods - 1);
    }
    if (r->new_food || r->craving) {
        add(buf, cap, "  %s", r->new_food ? "new!" : "craving");
    }
}

void pet_app_habitat_text(int habitat, int line, char *buf, size_t cap)
{
    const pet_places_t *pl = &pet_app_pet()->places;
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (habitat < 0 || habitat >= HABITAT_MAX || !pl->habitat[habitat].prints) {
        return;
    }
    const habitat_t *h = &pl->habitat[habitat];
    if (line == 0) {
        char label[HABITAT_NAME_MAX + 4];
        places_label(pl, habitat, label, sizeof(label));
        snprintf(buf, cap, habitat == places_home(pl) && h->name[0] ? "%s (home)" : "%s", label);
    } else if (h->minutes < 90) {
        snprintf(buf, cap, "%d visit%s, %d min", h->visits, h->visits == 1 ? "" : "s", (int)h->minutes);
    } else {
        snprintf(buf, cap, "%d visit%s, %d h", h->visits, h->visits == 1 ? "" : "s",
                 (int)((h->minutes + 30) / 60));
    }
}

void pet_app_resident_text(int which, int line, char *buf, size_t cap)
{
    const pet_farm_t *farm = &pet_app_pet()->farm;
    if (!buf || cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (which < 0 || which >= farm->count) {
        return;
    }
    const pet_resident_t *r = &farm->resident[which];
    if (line == 0) {
        snprintf(buf, cap, "%s", r->name[0] ? r->name : "(never named)");
    } else if (line == 1) {
        snprintf(buf, cap, "%s, generation %d", pet_form_name((pet_form_t)r->form), r->generation);
    } else {
        snprintf(buf, cap, "lived %d days, %lu meals", r->days, (unsigned long)r->meals);
        for (int t = 0; t < TRAIT_COUNT; t++) {
            if ((r->traits >> t) & 1) {
                add(buf, cap, ", %s", pet_trait_name((pet_trait_t)t));
            }
        }
    }
}
