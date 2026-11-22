/*
 * Shared application: the home screen and the live pet behind it.
 *
 * Everything under app/ depends only on LVGL and these headers, so the same
 * code runs in the desktop simulator (sim/) and on both boards (firmware/).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_motion.h"
#include "pet_places.h"

#include "pet_food.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Side of the square the pet lives in. Fits both the 410x502 and 480x480 panels. */
#define PET_AREA_SIZE 400

typedef enum {
    PET_MOOD_HAPPY,
    PET_MOOD_CONTENT,
    PET_MOOD_BORED,
    PET_MOOD_HUNGRY,
    PET_MOOD_SLEEPY,
    PET_MOOD_QUEASY,
} pet_mood_t;

struct pet_sound;   /* pet_sound_t, in pet_sound.h */

/* Platform services the app needs. Each platform fills this in once. */
typedef struct {
    /* 0-100, or -1 when unknown. */
    int (*battery_percent)(void);
    /* True when running from USB power (backpack mode). */
    bool (*usb_powered)(void);
    /* How the device is being carried, from its motion sensor. NULL = no sensor. */
    pet_motion_state_t (*motion)(void);
    /*
     * Local time in seconds, arranged so a multiple of 86400 is local
     * midnight. Must never return 0 and should not jump backwards. The
     * simulator runs this faster than real time.
     */
    uint32_t (*now_s)(void);
    /* A properly random number, different on every start. Seeds each new egg. */
    uint32_t (*random)(void);
    /*
     * Storage for the pet's save (a few hundred bytes; see pet_save()).
     * load returns the number of bytes read, 0 when there is no save.
     * Either may be NULL, in which case the pet starts fresh every time.
     */
    size_t (*load)(void *buf, size_t cap);
    bool (*save)(const void *data, size_t len);
    /*
     * Plays a sound (see pet_sound.h). Must return at once and play in the
     * background; the sound stays valid forever. May be NULL for silence.
     */
    void (*play)(const struct pet_sound *sound);
    /* Screen brightness, 0-100. 0 means dark. May be NULL. */
    void (*set_brightness)(int percent);
    /*
     * Storage for the owner's settings (a few bytes), separate from the
     * pet's save. Same rules as load and save. Either may be NULL.
     */
    size_t (*load_settings)(void *buf, size_t cap);
    bool (*save_settings)(const void *data, size_t len);
    /*
     * Moves the clock behind now_s() forward or back by this many seconds
     * and keeps it that way across restarts. NULL hides "set the time".
     */
    void (*adjust_clock)(int32_t seconds);
    /*
     * true: hand the memory card to a computer over USB, as a drive, and
     * stop using it. false: take it back, which may restart the device.
     * Returns whether the card is now with the computer. NULL hides the
     * setting.
     */
    bool (*share_card)(bool share);
    /*
     * The "Dev log" setting: turns the logging of network and device names
     * on or off. It is off until the owner turns it on. NULL hides the
     * setting.
     */
    void (*set_dev_log)(bool names);
    /* The version this was built as, such as "0.1.1", for the settings screen. NULL shows none. */
    const char *version;
    /*
     * Settings files (pet_config.h), kept where the owner can edit them: on
     * the memory card. read_file fills buf with the named file's text and
     * returns its length, 0 when there is no such file (or no card).
     * write_file replaces the named file. Either may be NULL.
     */
    size_t (*read_file)(const char *name, char *buf, size_t cap);
    bool (*write_file)(const char *name, const char *text, size_t len);
} pet_hal_t;

/*
 * Builds the home screen on the default LVGL display and starts the pet.
 * Call once, with the LVGL lock held, after the display and input are up.
 * The hal struct must outlive the app.
 */
void pet_app_start(const pet_hal_t *hal);

/*
 * A food was found. It joins the batch for the pet's next eating moment.
 * name is shown on screen while it is eaten and is not kept; it may be
 * NULL or "". Call with the LVGL lock held.
 */
void pet_app_feed(food_type_t type, float base_value, const char *name);

/*
 * Saves the pet now. It is also saved by itself shortly after each eating
 * moment and every few minutes; call this before a planned power-off.
 * Call with the LVGL lock held.
 */
void pet_app_save_now(void);

/*
 * The side button: turns a lit screen off, and a dark one on. A touch also
 * wakes the screen. Call with the LVGL lock held.
 */
void pet_app_button(void);

/* True while the screen is off. Safe to call from any task. */
bool pet_app_screen_dark(void);

/*
 * The device was turned to be looked at (a wrist raise). Lights the screen
 * when on battery; plugged in it is ignored, since a backpack swings about
 * all day. Call with the LVGL lock held.
 */
bool pet_app_raise(void);

/* Whether a touch lights a dark screen ("Touch" in Settings). If not, the panel need not be asked. */
bool pet_app_touch_wakes(void);

/* How often a dark screen was lit, and by what, since the last call; and for how long it was lit. */
typedef struct {
    uint16_t touch, raise, button;
    uint32_t lit_s;
} pet_wakes_t;
void pet_app_take_wakes(pet_wakes_t *out);

/*
 * What was wrong with a settings file at start-up, as one line ("tuning.txt
 * line 12: ..."), or "" when they were all fine or absent.
 */
const char *pet_app_config_warning(void);
     /* true if it lit the screen */

/*
 * Where the pet is. Desk (false, the default): on USB power the screen
 * stays on, dimmed when idle. Carry (true), for a backpack with a power
 * bank: the screen sleeps when idle on any power source; eating sounds
 * still play, and a tap shows what was eaten. Scanning is unaffected.
 * A long press of the side button switches between the two.
 * The platform is expected to remember the setting across restarts.
 * Call these with the LVGL lock held.
 */
void pet_app_set_carry(bool carry);
bool pet_app_carry(void);
void pet_app_button_long(void);

/* True while the owner has paused the pet (settings): do not scan, the pet's time stands still. */
bool pet_app_paused(void);

/*
 * What one scan heard around the pet, reduced to prints and kinds of place
 * (see pet_places.h). Returns PLACE_EV_* bits for what changed. Call with
 * the LVGL lock held.
 */
uint32_t pet_app_scan(const pet_scan_t *scan);

/* The habitat the pet is in (0 and up), or -1 on the move. */
int pet_app_here(void);

/*
 * Opens one of the screens by name: home, diet, journal, recent, menu, dex,
 * badges, farm, pet, settings, name, reset, places. For tests and screenshots; on the device
 * they open by swiping. Returns false for an unknown name.
 */
bool pet_app_open_page(const char *name);

/*
 * Sound setting: 0 off, 1 rare finds only, 2 everything (the default).
 * These are pet_sound_level_t values. Quiet hours are silent regardless.
 */
void pet_app_set_sound_level(int level);

/*
 * Growth speed: 1 is real life (about 12 weeks from egg to retirement);
 * 2016 runs a whole life in an hour, for testing. Call right after
 * pet_app_start().
 */
void pet_app_set_growth_speed(float speed);

/* One line describing the pet, for logs: "new egg, traits: lazy, curious". */
void pet_app_describe(char *out, size_t size);

/*
 * Shows a short line under the pet for a few seconds.
 * Call with the LVGL lock held.
 */
void pet_app_show_find(const char *text);

#ifdef __cplusplus
}
#endif
