/*
 * For a platform that draws its own screens around the pet (a phone): what
 * the watch's pages read, the words they work out, and what their buttons
 * do. The watch's own pages are built on these same functions, so the two
 * cannot come to say different things.
 *
 * Call all of it after pet_app_start(), with the LVGL lock held. Nothing
 * here keeps a pointer it is given, and the pointers it returns stay good
 * for as long as the app runs; what they point at changes as the pet lives.
 *
 * See also pet_hal_t in pet_app.h: pet_only leaves the pages out, and
 * notice says when to read again.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_core.h"
#include "pet_app.h"
#include "pet_link.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- What the pages are given ------------------------------------------- */

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

/* The pet. Read it through pet_core.h; do not change it. */
const pet_t *pet_app_pet(void);

/* The owner's settings as they are now. */
const pet_settings_t *pet_app_settings(void);

/* The eating moments since start-up, newest first. Returns how many (0 to PET_RECENT_MAX). */
int pet_app_recent(const pet_recent_t **list);

/* The record of one day: the day in progress or one from the week before it. NULL when there is none. */
const pet_day_t *pet_app_day(uint32_t day);

/* ---- The words the pages work out --------------------------------------- */

typedef enum {
    /* The home screen. */
    PET_LINE_MOOD,          /* "paused", "egg", "drowsy", "underfed", or the mood */
    PET_LINE_BELL,          /* what the bell is about: the craving, or "ready to retire"; "" = no bell */
    /* The diet page. */
    PET_LINE_STAGE,         /* "juvenile Grazer" */
    PET_LINE_GROWTH_DAYS,   /* "day 2 of 14, about 3 real days left"; "A full life lived." at the end; "" = no days to count */
    PET_LINE_GROWTH_MEALS,  /* "meals 3 of 40"; "" once a full life is lived */
    PET_LINE_LEANING,       /* "drifting toward Grazer"; "" for an adult */
    PET_LINE_MISSING,       /* "none this week: music, cars"; "" when nothing is missing */
    /* The pet's card. */
    PET_LINE_AGE,           /* "3 days", in the pet's days */
    PET_LINE_TRAITS,        /* "lazy, curious" */
    PET_LINE_EATS_MOSTLY,   /* a food group, or "nothing yet" */
    PET_LINE_BADGES,        /* "3 of 24" */
    PET_LINE_FAVOURITE,     /* "Favourite: cars". The watch offers the choice only before the pet is adult */
    /* The places page. */
    PET_LINE_HERE,          /* "Here now: home", "On the move.", or that it does not know the place */
    /* The farm. */
    PET_LINE_RETIRING,      /* "Retire Bob..." (the watch's button) when it can; otherwise why not yet */
    /* The line under each menu tile. */
    PET_LINE_TILE_DEX,      /* "9 of 15 tasted" */
    PET_LINE_TILE_BADGES,   /* "3 of 24 earned" */
    PET_LINE_TILE_PLACES,   /* "2 habitats" */
    PET_LINE_TILE_FARM,     /* "ready to retire", "empty", "2 retired" */
    PET_LINE_TILE_PET,      /* its name, or else its form or stage */
    PET_LINE_TILE_SETTINGS, /* "dev log on", "pet paused", or "" */
    PET_LINE_COUNT,
} pet_line_t;

/*
 * Writes one of those lines into buf. Returns true where the watch draws
 * the line as something to notice (its amber, or for PET_LINE_RETIRING a
 * button): a bell, a food group gone missing, a place it does not know,
 * a pet ready to retire, a badge not yet looked at, a paused pet.
 */
bool pet_app_line(pet_line_t which, char *buf, size_t cap);

/* PET_LINE_MOOD as a constant string: the same pointer for as long as the mood is the same. */
const char *pet_app_mood_text(void);

/* A setting's row: its name ("Quiet from") and what it is set to ("22:00"). */
const char *pet_app_setting_label(pet_setting_id_t id);
void pet_app_setting_text(pet_setting_id_t id, char *buf, size_t cap);
/* Whether the watch's settings page would list it on this platform (see the notes in pet_hal_t). */
bool pet_app_setting_shown(pet_setting_id_t id);

/* Recent find i (0 = newest), without its time and value: "open network  +2  new!". */
void pet_app_recent_text(int i, char *buf, size_t cap);

/*
 * A habitat's row on the places page. line 0: its name as listed, "Den
 * (home)"; line 1: "3 visits, 2 h". "" for a habitat that is not in use.
 */
void pet_app_habitat_text(int habitat, int line, char *buf, size_t cap);

/*
 * A farm resident, 0 to farm.count - 1. line 0: its name, or "(never
 * named)"; line 1: "Roamer, generation 2"; line 2: "lived 84 days, 912
 * meals, lazy, curious".
 */
void pet_app_resident_text(int which, int line, char *buf, size_t cap);

/* ---- What the pages' buttons do ----------------------------------------- */

/*
 * The - (dir -1) or + (dir +1) of a settings row: applies it, keeps it and
 * makes its sound. An on/off setting flips either way. Does nothing for a
 * setting that is not shown here.
 */
void pet_app_setting_step(pet_setting_id_t what, int dir);

/* Names the pet, and saves. "" takes the name away. Cut to PET_NAME_MAX - 1 bytes. */
void pet_app_rename(const char *name);

/* The favourite food: 0 for none, up to PET_FAVOURITE_MAX. False for an adult, whose form is decided. */
bool pet_app_set_favourite(int favourite);

/* Names a habitat ("" takes the name away). False when there is no such habitat. */
bool pet_app_name_place(int habitat, const char *name);

/*
 * Sends the pet to the farm: it says goodbye and hops out, and a few
 * seconds later its egg comes in. False, and nothing happens, unless
 * pet_can_retire(). Ask the owner first; this does not.
 */
bool pet_app_retire(void);

/* A new egg in place of the pet and everything it had; the settings stay. Ask the owner first. */
void pet_app_start_over(void);

/* ---- Moving between devices --------------------------------------------- */

/*
 * A pet lives on one device at a time and can be moved to another; the
 * owner's collection (the farm, the badges, the place badges and off the
 * grid) is joined between devices whenever a parcel passes. Habitats stay
 * on the device that learned them. See "Moving between devices" in
 * pet_core.h for the rules, and pet_exchange.c for the bytes.
 *
 * To send: pet_app_export() into a buffer of PET_PARCEL_MAX bytes, get the
 * bytes to the other device (a file, a link), and only when they are known
 * to be safe there call pet_app_pet_left(). To receive: pet_app_import().
 * A parcel without the pet brings the two farms level and moves nothing.
 */

/* True when no pet lives here. The pet's square is then empty, and the pet's own lines mean nothing. */
bool pet_app_pasture_empty(void);

/* Writes a parcel. Returns its length; 0 if the pet was asked for and there is none, or cap is too small. */
size_t pet_app_export(bool with_pet, uint8_t *buf, size_t cap);

/* The pet that was exported is safe elsewhere: it leaves, and this pasture is empty. Saves. */
void pet_app_pet_left(void);

/*
 * Takes a parcel in, and saves. Anything but PET_IMPORT_OK changed
 * nothing: say why (pet_import_t). *pet_arrived (may be NULL) is true
 * when a pet moved in; it is told as news too ("Mossy has arrived").
 */
pet_import_t pet_app_import(const uint8_t *buf, size_t len, bool *pet_arrived);

/* Starts a first-generation egg in an empty pasture. False if a pet lives here. Ask the owner first. */
bool pet_app_new_egg(void);

/*
 * The exchange over a link between two devices (pet_link.h), on the app's
 * pet. Start it when the link is up, hand arriving bytes to
 * pet_app_link_in(), and use pet_link_out(), pet_link_lost() and
 * pet_link_state() on the same struct for the rest. Each change is saved
 * as it is made, and a pet arriving or leaving is told as news. False if
 * the pet is at this moment on its way to the farm.
 */
bool pet_app_link_start(pet_link_t *link, bool give_pet, bool caller);
void pet_app_link_in(pet_link_t *link, const uint8_t *data, size_t len);

/*
 * The exchange page of a device that runs the pages (pet_pages.c): what
 * it shows, and what its buttons do. A device with screens of its own
 * runs the link itself and needs none of these.
 */
bool pet_app_can_exchange(void);
bool pet_app_exchange_open(bool give_pet);
void pet_app_exchange_close(void);
pet_exchange_step_t pet_app_exchange_step(uint32_t *code);
const char *pet_app_exchange_result(void);

/*
 * The preview of every form: the pet's square shows each stage and form in
 * turn, the egg first, moving on at each tap on the pet and going back to
 * the real pet after the last. Each one is told as news ("preview 3 of 11:
 * Grazer"), then "preview over". Left alone for a minute it ends without a
 * word. Only the drawing changes; the pet itself is untouched, and its
 * finds are not told while it runs. Calling it again starts from the egg.
 *
 * The forms are meant to be a surprise, so this is false, and nothing
 * happens, unless Dev log is on. On the watch it is a hold on the Settings
 * title.
 */
bool pet_app_preview(void);

/* ---- The farm's pictures ------------------------------------------------ */

/*
 * One piece of a small picture: a box with rounded corners, in a square of
 * PET_ICON_SIZE. x and y are its top left corner. radius is of its corners;
 * at half the shorter side the box is a circle or a pill.
 */
typedef struct {
    uint8_t x, y, w, h;
    uint8_t radius;
    uint32_t color;     /* 0xRRGGBB */
} pet_icon_part_t;

#define PET_ICON_SIZE      56
#define PET_ICON_PARTS_MAX 16

/*
 * The picture the farm shows for a retired pet of this form (a resident's
 * form is pet->farm.resident[i].form). Fills parts with up to max pieces,
 * to be drawn in order, each over the ones before, and returns how many
 * the picture has; PET_ICON_PARTS_MAX holds any of them. To draw it at
 * another size, multiply every number by size / PET_ICON_SIZE. A piece
 * may run a pixel or two past the right edge (a tail): cut the picture
 * off at its square, as the farm does. It needs
 * no app running and no LVGL.
 */
int pet_farm_icon(pet_form_t form, pet_icon_part_t *parts, int max);

#ifdef __cplusplus
}
#endif
