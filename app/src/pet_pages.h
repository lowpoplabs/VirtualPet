/*
 * The screens beyond the home screen: diet, journal, recent finds, and the
 * menu with the food-dex, badges, farm, pet card and settings.
 *
 * A page is built when it opens and thrown away when it closes, so a page
 * costs nothing while the pet is just sitting on the home screen.
 */
#pragma once

#include "lvgl.h"
#include "pet_core.h"
#include "pet_ui.h"   /* the settings, the recent finds and the words: shared with a platform's own screens */

typedef enum {
    PAGE_HOME,      /* no page open */
    PAGE_DIET,
    PAGE_JOURNAL,
    PAGE_RECENT,
    PAGE_MENU,
    PAGE_DEX,
    PAGE_BADGES,
    PAGE_FARM,
    PAGE_INFO,
    PAGE_SETTINGS,
    PAGE_NAME,
    PAGE_RESET,     /* "Start over" */
    PAGE_PLACES,
    PAGE_RETIRE,    /* sending the pet to the farm */
    PAGE_MOVE,      /* moving the pet to another device, or bringing the farms level */
    PAGE_COUNT,
} pet_page_t;

/* What the pages need from the app. */
typedef struct {
    pet_t *pet;
    const pet_settings_t *settings;
    const pet_recent_t *recent;     /* newest first */
    const int *recent_count;
    bool can_set_clock;
    bool can_share_card;
    bool can_dev_log;
    bool can_brightness;
    bool can_raise;                 /* there is a motion sensor, and a screen of ours to light */
    bool screen_managed;            /* the platform darkens its own screen: no "Screen" or "Touch" */
    bool pet_only;                  /* the platform draws its own screens: no page ever opens */
    const char *dev_log_on;         /* the "Dev log" row's word for on; NULL = "names" */
    const char *dev_log_about;      /* its paragraph; NULL = the one about names on the card */
    const char *version;            /* shown at the foot of the settings; NULL = not known */
    uint32_t (*now)(void);
    /* The owner pressed - (dir -1) or + (dir +1) on a setting. The app applies and keeps it. */
    void (*change)(pet_setting_id_t what, int dir);
    /* The owner held the button on the "Start over" page: wipe the pet, keep the settings. */
    void (*start_over)(void);
    /* The owner held the button on the "Retire" page: the pet goes to the farm and leaves an egg. */
    void (*retire)(void);
    /* The owner held the Settings title: show every stage and form in turn (hidden, to keep them a surprise). */
    void (*preview)(void);
} pet_pages_host_t;

/* Call once. Swipes on screen open and close pages from then on. */
void pet_pages_init(lv_obj_t *screen, const pet_pages_host_t *host);

/* Opens a page, replacing any that is open. PAGE_HOME closes whatever is open. */
void pet_pages_open(pet_page_t page);

pet_page_t pet_pages_current(void);

/* The pet's card, opened from the home screen (a long press on the pet). */
void pet_pages_open_card(void);

/* The platform the app was started on, for the words that depend on it. NULL before pet_app_start(). */
const pet_hal_t *pet_app_hal(void);
