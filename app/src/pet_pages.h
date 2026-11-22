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
    PAGE_COUNT,
} pet_page_t;

/* One eating moment, as the recent-finds page lists it. No names are kept. */
typedef struct {
    uint32_t time;
    food_type_t best;
    uint16_t foods;
    float value;
    bool new_food;
    bool craving;
} pet_recent_t;

#define PET_RECENT_MAX 8

typedef struct {
    uint8_t brightness;     /* percent, 20-100 */
    uint8_t sound;          /* pet_sound_level_t */
    uint8_t volume;         /* 1-5; 3 is each sound as written */
    uint8_t quiet_start;    /* hour the quiet hours begin */
    uint8_t quiet_end;
    bool carry;             /* see pet_app_set_carry() */
    bool paused;            /* on a shelf: the pet's time stands still, no scanning */
    bool card_shared;       /* the card is lent to a computer over USB; never saved */
    bool dev_log;           /* for tuning: names are written to the log. Off unless turned on */
    uint8_t life_speed;     /* pet_life_speed_t: how long a whole life takes */
    bool raise_off;         /* on battery, turning the screen toward you does not light it */
    bool touch_off;         /* a touch does not light a dark screen: only the side button, or a raise */
    bool text_large;        /* the pages are set in the next size of letters up */
    bool speed_fixed;       /* growth_speed is given in tuning.txt, which wins; never saved */
} pet_settings_t;

typedef enum {
    LIFE_SLOW,      /* about 12 weeks: the built-in pace */
    LIFE_MEDIUM,    /* about 6 weeks */
    LIFE_FAST,      /* about 3 weeks */
    LIFE_SPEED_COUNT,
} pet_life_speed_t;

typedef enum {
    SET_BRIGHTNESS,
    SET_TEXT,
    SET_SOUND,
    SET_VOLUME,
    SET_QUIET_START,
    SET_QUIET_END,
    SET_HOUR,
    SET_MINUTE,
    SET_CARRY,
    SET_PAUSE,
    SET_LIFE_SPEED,
    SET_RAISE,
    SET_TOUCH,
    SET_CARD,
    SET_DEV_LOG,
    SET_COUNT,
} pet_setting_id_t;

/* What the pages need from the app. */
typedef struct {
    pet_t *pet;
    const pet_settings_t *settings;
    const pet_recent_t *recent;     /* newest first */
    const int *recent_count;
    bool can_set_clock;
    bool can_share_card;
    bool can_dev_log;
    const char *version;            /* shown at the foot of the settings; NULL = not known */
    uint32_t (*now)(void);
    /* The owner pressed - (dir -1) or + (dir +1) on a setting. The app applies and keeps it. */
    void (*change)(pet_setting_id_t what, int dir);
    /* pet->name was edited. */
    void (*renamed)(void);
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
