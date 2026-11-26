/*
 * Home screen: pet centered on black, clock and battery on the top strip,
 * stomach bar, mood and craving on the bottom strip. Behind it runs the
 * pet core, ticked from an LVGL timer.
 *
 * Layout rule from the plan: the pet owns a centered 400x400 square, and
 * whatever height is left over (51px on the watch, 40px on the square
 * board) is split between the two strips.
 */
#include "pet_app.h"
#include "pet_config.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "pet_core.h"
#include "pet_pages.h"
#include "pet_power.h"
#include "pet_sound.h"
#include "pet_view.h"

#define COLOR_TEXT   0xB8C0CC
#define COLOR_DIM    0x5A6270
#define COLOR_ACCENT 0x8CF2B0
#define COLOR_WARN   0xFFB454
#define COLOR_FIND   0xFFE27A

#define FIND_SHOW_MS    4000
#define PET_TICK_MS     100     /* short, so a sped-up simulator still steps finely */
#define PET_TICK_DARK_MS 500    /* screen dark: only the pet's numbers and a wake check */
#define STRIP_REFRESH_MS 1000
#define PAT_COOLDOWN_S  30
#define SNIFF_MS        1500    /* from the sniff cue to the munch */
/* Saving, in real time: soon after a meal, and at least this often regardless. */
#define SAVE_AFTER_MEAL_MS (30 * 1000)
#define SAVE_EVERY_MS      (5 * 60 * 1000)

static const pet_hal_t *s_hal;
static pet_t s_pet;
static pet_brain_t s_brain;
/* The owner's settings. Kept apart from the pet: a new egg does not reset them. */
static pet_settings_t s_set = {
    .brightness = 60, .sound = SOUND_LEVEL_ALL, .volume = 3, .quiet_start = 22, .quiet_end = 7,
};
static pet_recent_t s_recent[PET_RECENT_MAX];   /* newest first; memory only */
static int s_recent_count;
static uint32_t s_last_sound_ms;   /* real time of the last eating sound; 0 = none yet */
static bool s_drowsy;           /* battery is low */
static bool s_loaded;           /* this pet came from a save */
static uint32_t s_save_due_ms;  /* real-time deadline for the next save */

/* Name of the best food in the batch being gathered. Shown at the eating moment, then wiped. */
static char s_best_name[40];
static float s_best_value;
static uint32_t s_last_pat;

static lv_obj_t *s_clock;
static lv_obj_t *s_battery;
static lv_obj_t *s_stomach;
static lv_obj_t *s_mood;
static lv_obj_t *s_craving;
static lv_obj_t *s_find;
static lv_timer_t *s_find_timer;

/* ---- Word for a platform that draws its own screens (pet_hal_t.notice) ---- */

static bool s_changed;      /* something the pages show has changed; told at the end of the tick */

static void notice(pet_notice_t what, const char *text)
{
    if (s_hal && s_hal->notice) {
        s_hal->notice(what, text);
    }
}

static void changed(void)
{
    s_changed = true;
}

/* Once a tick at most, however much happened in it. A new day counts too. */
static void flush_changed(void)
{
    static uint32_t day;
    if (s_pet.today.day != day) {
        day = s_pet.today.day;
        s_changed = true;
    }
    if (s_changed) {
        s_changed = false;
        notice(PET_NOTICE_CHANGED, NULL);
    }
}

/* The mood the face shows: a low battery makes the pet drowsy whatever else is going on. */
static pet_mood_t face_mood(void)
{
    return s_drowsy ? PET_MOOD_SLEEPY : pet_mood(&s_pet);
}

const char *pet_app_mood_text(void)
{
    if (s_set.paused) {
        return "paused";
    }
    if (s_pet.stage == STAGE_EGG) {
        return "egg";
    }
    if (s_drowsy) {
        return "drowsy";
    }
    if (s_pet.is_static) {
        return "underfed";
    }
    switch (pet_mood(&s_pet)) {
    case PET_MOOD_HAPPY:   return "happy";
    case PET_MOOD_CONTENT: return "content";
    case PET_MOOD_BORED:   return "bored";
    case PET_MOOD_HUNGRY:  return "hungry";
    case PET_MOOD_SLEEPY:  return "sleepy";
    case PET_MOOD_QUEASY:  return "queasy";
    }
    return "";
}

static const char *battery_symbol(int pct)
{
    if (pct >= 88) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 63) return LV_SYMBOL_BATTERY_3;
    if (pct >= 38) return LV_SYMBOL_BATTERY_2;
    if (pct >= 13) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void refresh_strip(lv_timer_t *t)
{
    LV_UNUSED(t);

    uint32_t now = s_hal->now_s();
    lv_label_set_text_fmt(s_clock, "%02d:%02d", (int)(now % 86400 / 3600), (int)(now % 3600 / 60));

    int pct = s_hal->battery_percent ? s_hal->battery_percent() : -1;
    bool usb = s_hal->usb_powered && s_hal->usb_powered();
    if (pct < 0) {
        lv_label_set_text(s_battery, usb ? LV_SYMBOL_USB : "");
    } else {
        lv_label_set_text_fmt(s_battery, "%s%d%% %s", usb ? LV_SYMBOL_CHARGE " " : "",
                              pct, battery_symbol(pct));
    }
    lv_obj_set_style_text_color(s_battery,
                                lv_color_hex((pct >= 0 && pct <= 15) ? COLOR_WARN : COLOR_TEXT), 0);
}

static void craving_tap_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    pet_pages_open_card();
}

/* Pushes the pet's current state onto the bottom strip and its face. */
/* ---- Preview of every form ----------------------------------------------
 * Hidden: hold the Settings title. The pet on the home screen is drawn as
 * each stage and form in turn, moving on at each tap, so the artwork can be
 * seen on the device without waiting weeks for a pet to grow into it. Only
 * the drawing changes; the pet itself is untouched.
 */
#define PREVIEW_IDLE_MS 60000   /* left alone this long, it ends by itself */

static const struct {
    uint8_t stage, form;
} PREVIEW[] = {
    {STAGE_EGG, FORM_NONE},       {STAGE_HATCHLING, FORM_NONE},
    {STAGE_JUVENILE, FORM_GRAZER}, {STAGE_JUVENILE, FORM_HUNTER},
    {STAGE_ADULT, FORM_HOMEBODY}, {STAGE_ADULT, FORM_ROAMER},  {STAGE_ADULT, FORM_SCAVENGER},
    {STAGE_ADULT, FORM_SINGER},   {STAGE_ADULT, FORM_RACER},   {STAGE_ADULT, FORM_GLOWBUG},
    {STAGE_ADULT, FORM_PRISM},
};
#define PREVIEW_COUNT ((int)(sizeof(PREVIEW) / sizeof(PREVIEW[0])))

static bool s_retire_told;      /* the owner has been told this pet is ready to retire */
static int s_preview = -1;      /* which one is showing; -1 = the real pet */
static uint32_t s_preview_tick;

static void find_line(pet_notice_t kind, const char *text);
static void show_line(pet_notice_t kind, const char *text);

static void preview_show(void)
{
    pet_stage_t stage = (pet_stage_t)PREVIEW[s_preview].stage;
    pet_form_t form = (pet_form_t)PREVIEW[s_preview].form;
    pet_view_set_look(stage, form, false);
    char text[64];
    snprintf(text, sizeof(text), "preview %d of %d: %s", s_preview + 1, PREVIEW_COUNT,
             form == FORM_NONE ? pet_stage_name(stage) : pet_form_name(form));
    find_line(PET_NOTICE_NEWS, text);
    s_preview_tick = lv_tick_get();
}

static void on_preview(void)
{
    s_preview = 0;
    preview_show();
}

/* A tap during the preview. True if it was used up moving to the next form. */
static bool preview_next(void)
{
    if (s_preview < 0) {
        return false;
    }
    if (++s_preview >= PREVIEW_COUNT) {
        s_preview = -1;
        pet_view_set_look(s_pet.stage, s_pet.form, s_pet.is_static);
        find_line(PET_NOTICE_NEWS, "preview over");
    } else {
        preview_show();
    }
    return true;
}

static void refresh_pet(void)
{
    static int32_t shown_stomach = -1;
    static const char *shown_mood, *shown_craving;

    /* Said once, when its adult life is complete; it then waits for the owner. */
    if (!s_retire_told && pet_can_retire(&s_pet)) {
        s_retire_told = true;
        pet_app_show_find("ready to retire: see the farm");
    }
    if (s_preview >= 0 && lv_tick_elaps(s_preview_tick) > PREVIEW_IDLE_MS) {
        s_preview = -1;
    }
    /* Redraws the body only when the stage, form or underfed state has changed. */
    if (s_preview < 0) {
        pet_view_set_look(s_pet.stage, s_pet.form, s_pet.is_static);
    }

    int32_t stomach = (int32_t)(s_pet.stomach * 10);
    if (stomach != shown_stomach) {
        shown_stomach = stomach;
        changed();
        if (s_stomach) {
            lv_bar_set_value(s_stomach, stomach, LV_ANIM_ON);
            lv_obj_set_style_bg_color(
                s_stomach,
                lv_color_hex(s_pet.stomach < s_pet.tune.hungry_below ? COLOR_WARN : COLOR_ACCENT),
                LV_PART_INDICATOR);
        }
    }
    /* Both texts are string constants, so comparing pointers is enough. */
    const char *mood = pet_app_mood_text();
    if (mood != shown_mood) {
        shown_mood = mood;
        changed();
        if (s_mood) {
            lv_label_set_text(s_mood, mood);
        }
        pet_view_set_mood(face_mood());
    }
    /*
     * The bell: it wants something, or it is ready to retire. Which, is on
     * the pet's card. Both texts are constants, so pointers tell a change.
     */
    const char *craving = pet_craving_text(&s_pet);
    if (!craving[0] && pet_can_retire(&s_pet)) {
        craving = "ready to retire";
    }
    if (craving != shown_craving) {
        shown_craving = craving;
        changed();
        if (!s_craving) {
            /* no strip to put a bell on */
        } else if (craving[0]) {
            lv_obj_remove_flag(s_craving, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_craving, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * An eating moment plays out in two beats, as the plan asks: the pet
 * sniffs (with a chirp) so there is time to look, then a second or two
 * later the food pops up and it munches.
 */
static struct {
    bool pending;
    pet_meal_t meal;
    char name[40];
    bool with_sound;
    lv_timer_t *timer;
} s_reveal;

static void play(pet_sound_id_t id)
{
    if (s_hal->play) {
        /* The volume setting scales each sound; 3 plays it as written. */
        static const int PERCENT[6] = {100, 20, 45, 100, 150, 220};
        /* The device may play it a moment later, so each call gets a copy of its own. */
        static pet_sound_t copies[4];
        static unsigned next;
        pet_sound_t *scaled = &copies[next++ % 4];
        *scaled = *pet_sound(id);
        scaled->volume = scaled->volume * PERCENT[s_set.volume <= 5 ? s_set.volume : 3] / 100;
        if (scaled->volume > 160) {
            scaled->volume = 160;   /* as loud as the speaker's samples go */
        }
        s_hal->play(scaled);
    }
}

static void reveal_meal(void)
{
    if (!s_reveal.pending) {
        return;
    }
    s_reveal.pending = false;
    lv_timer_pause(s_reveal.timer);

    const pet_meal_t *m = &s_reveal.meal;
    char line[80];
    int len = snprintf(line, sizeof(line), "%s", food_label(m->best));
    if (s_reveal.name[0]) {
        len += snprintf(line + len, sizeof(line) - len, ": %s", s_reveal.name);
    }
    if (m->foods > 1 && len < (int)sizeof(line)) {
        snprintf(line + len, sizeof(line) - len, "  +%d", m->foods - 1);
    }
    show_line(PET_NOTICE_FIND, line);
    pet_view_set_action(s_brain.action);   /* stop sniffing */
    pet_view_hop();
    pet_view_munch();
    if (s_reveal.with_sound) {
        play(pet_meal_sound(m));
    }

    /* The name has been shown; nothing keeps it. */
    memset(s_reveal.name, 0, sizeof(s_reveal.name));
}

static void reveal_timer(lv_timer_t *t)
{
    LV_UNUSED(t);
    reveal_meal();
}

/* ---- Screen: bright in use, then dimmed (plugged in) or dark (on battery) ---- */

typedef enum { SCREEN_BRIGHT, SCREEN_DIM, SCREEN_DARK } screen_state_t;

static screen_state_t s_screen = SCREEN_BRIGHT;
static int s_brightness = -1;       /* last level sent to the panel */
static bool s_forced_dark;          /* side button turned the screen off */
/* What lit a dark screen, counted for the tuning log (pet_app_take_wakes). */
typedef enum { WAKE_TOUCH, WAKE_RAISE, WAKE_BUTTON, WAKE_COUNT } wake_cause_t;
static wake_cause_t s_wake_cause = WAKE_TOUCH;  /* a touch, unless a raise or the button says otherwise */
static uint16_t s_wake_tally[WAKE_COUNT];
static uint32_t s_lit_ms, s_lit_since_ms;       /* how long the screen has been lit since the last take */
static bool s_glance;               /* lit by a raise and not touched since: it goes dark again sooner */
#define GLANCE_MS 6000
static uint32_t s_last_idle_ms;
static lv_timer_t *s_pet_timer;
static pet_tuning_t s_tune;        /* the pet's numbers: built in, or from the card (config_load) */

/* What the pet ate while the screen was dark, for the catch-up line on wake. */
static struct {
    int foods;
    food_type_t best;
    float best_value;
    char name[40];
} s_missed;

static void set_brightness(int pct)
{
    if (pct != s_brightness) {
        s_brightness = pct;
        if (s_hal->set_brightness) {
            s_hal->set_brightness(pct);
        }
    }
}

/* With the screen dark nothing needs drawing: stop the work that only feeds the display. */
static void set_drawing(bool on)
{
    lv_timer_t *refresh = lv_display_get_refr_timer(lv_display_get_default());
    lv_timer_t *anim = lv_anim_get_timer();
    pet_view_set_paused(!on);
    if (on) {
        lv_timer_resume(anim);
        lv_timer_resume(refresh);
        lv_timer_set_period(s_pet_timer, PET_TICK_MS);
        lv_obj_invalidate(lv_screen_active());
    } else {
        lv_refr_now(NULL);      /* leave a finished frame behind */
        lv_timer_pause(anim);
        lv_timer_pause(refresh);
        lv_timer_set_period(s_pet_timer, PET_TICK_DARK_MS);
    }
}

/* ---- Growing up -------------------------------------------------------- */

static char s_growth_news[48];   /* waiting to be shown when the screen next lights */

static void show_growth_news(void)
{
    if (s_growth_news[0]) {
        pet_app_show_find(s_growth_news);
        pet_view_hop();
        s_growth_news[0] = '\0';
    }
}

/* The pet has just hatched, grown or taken its adult form. */
static void on_evolved(void)
{
    switch (s_pet.stage) {
    case STAGE_HATCHLING:
        snprintf(s_growth_news, sizeof(s_growth_news), "hatched!");
        break;
    case STAGE_JUVENILE:
        snprintf(s_growth_news, sizeof(s_growth_news), "grew into a %s", pet_form_name(s_pet.form));
        break;
    default:
        snprintf(s_growth_news, sizeof(s_growth_news), "became a %s", pet_form_name(s_pet.form));
        break;
    }
    /* A once-in-a-life moment: it gets the fanfare unless sound is off or it is night. */
    if (s_set.sound != SOUND_LEVEL_OFF && !pet_is_night(&s_pet)) {
        s_last_sound_ms = lv_tick_get() | 1;
        play(SOUND_FANFARE);
    }
    pet_app_save_now();
    if (s_screen != SCREEN_DARK) {
        reveal_meal();
        refresh_pet();          /* draws the new body */
        show_growth_news();
    }
}

/* One or more badges were just earned. Growing up, if it happened too, is the bigger news. */
static void on_badges(uint32_t fresh)
{
    int first = -1, count = 0;
    for (int b = 0; b < BADGE_COUNT; b++) {
        if (fresh & (1u << b)) {
            first = first < 0 ? b : first;
            count++;
        }
    }
    if (first < 0 || s_growth_news[0]) {
        return;
    }
    int len = snprintf(s_growth_news, sizeof(s_growth_news), "badge: %s",
                       pet_badge_name((pet_badge_t)first));
    if (count > 1 && len < (int)sizeof(s_growth_news)) {
        snprintf(s_growth_news + len, sizeof(s_growth_news) - (size_t)len, "  +%d", count - 1);
    }
    if (s_set.sound != SOUND_LEVEL_OFF && !pet_is_night(&s_pet)) {
        s_last_sound_ms = lv_tick_get() | 1;
        play(SOUND_FANFARE);
    }
    pet_app_save_now();
    if (s_screen != SCREEN_DARK) {
        reveal_meal();
        show_growth_news();
    }
}

/* News about places: shown now, or when the screen next lights. */
static void place_news(const char *text, bool fanfare)
{
    if (s_growth_news[0]) {
        return;     /* growing up and badges outrank it */
    }
    snprintf(s_growth_news, sizeof(s_growth_news), "%s", text);
    if (fanfare && s_set.sound != SOUND_LEVEL_OFF && !pet_is_night(&s_pet)) {
        s_last_sound_ms = lv_tick_get() | 1;
        play(SOUND_FANFARE);
    }
    if (s_screen != SCREEN_DARK) {
        show_growth_news();
    }
}

uint32_t pet_app_scan(const pet_scan_t *scan)
{
    if (!s_hal || s_set.paused) {
        return 0;
    }
    place_kind_t kind = PLACE_KIND_COUNT;
    uint32_t ev = pet_scan(&s_pet, scan, &kind);
    changed();
    char line[48], label[HABITAT_NAME_MAX + 4];
    if (ev & PLACE_EV_BADGE) {
        snprintf(line, sizeof(line), "badge: %s (%s)", place_kind_name(kind),
                 place_tier_name(place_tier(s_pet.places.kind[kind].count)));
        place_news(line, true);
    } else if (ev & PLACE_EV_OFFGRID) {
        snprintf(line, sizeof(line), "badge: Off the grid (%s)",
                 place_tier_name(s_pet.places.offgrid_tier));
        place_news(line, true);
    } else if (ev & PLACE_EV_NEW) {
        place_news("somewhere new!", true);
    } else if (ev & PLACE_EV_ARRIVED) {
        places_label(&s_pet.places, s_pet.places.here, label, sizeof(label));
        if (s_pet.places.here == places_home(&s_pet.places) && !s_pet.places.habitat[s_pet.places.here].name[0]) {
            snprintf(line, sizeof(line), "home again");
        } else {
            snprintf(line, sizeof(line), "back at %s", label);
        }
        place_news(line, false);
    }
    if (ev & (PLACE_EV_BADGE | PLACE_EV_OFFGRID | PLACE_EV_NEW)) {
        pet_app_save_now();
    }
    return ev;
}

int pet_app_here(void)
{
    return s_hal ? s_pet.places.here : -1;
}

static void show_missed(void)
{
    /* Growing up outranks a list of snacks. */
    if (s_growth_news[0]) {
        refresh_pet();
        show_growth_news();
        memset(&s_missed, 0, sizeof(s_missed));
        return;
    }
    if (s_missed.foods == 0) {
        return;
    }
    char line[96];
    int len = snprintf(line, sizeof(line), "%d eaten while away, best: %s", s_missed.foods,
                       food_label(s_missed.best));
    if (s_missed.name[0] && len < (int)sizeof(line)) {
        snprintf(line + len, sizeof(line) - len, " %s", s_missed.name);
    }
    show_line(PET_NOTICE_FIND, line);
    pet_view_hop();
    memset(&s_missed, 0, sizeof(s_missed));
}

static void screen_update(void)
{
    const pet_power_plan_t plan = pet_app_power_plan(s_hal);
    uint32_t idle = lv_display_get_inactive_time(NULL);
    /* Reading a page takes longer than a glance at the pet. */
    uint32_t idle_limit = plan.screen_idle_ms * (pet_pages_current() != PAGE_HOME ? 4 : 1);

    /* Any touch (idle time dropping back) undoes the side button's "off", and makes a glance a look. */
    if (idle < s_last_idle_ms) {
        s_forced_dark = false;
        s_glance = false;
    }
    if (s_glance && idle_limit > GLANCE_MS) {
        idle_limit = GLANCE_MS;
    }
    s_last_idle_ms = idle;

    screen_state_t next;
    if (s_forced_dark) {
        next = SCREEN_DARK;
    } else if (idle < idle_limit || s_hal->screen_managed) {
        next = SCREEN_BRIGHT;   /* a platform that darkens its own screen is never idle to us */
    } else {
        next = plan.screen_stays_on ? SCREEN_DIM : SCREEN_DARK;
    }
    /* Left alone, it goes back to the pet. */
    if (next != SCREEN_BRIGHT && pet_pages_current() != PAGE_HOME) {
        pet_pages_open(PAGE_HOME);
    }

    bool was_dark = s_screen == SCREEN_DARK;
    if (next != s_screen) {
        if (next == SCREEN_DARK) {
            reveal_meal();      /* do not go dark mid-sniff */
        }
        s_screen = next;
    }
    /* Order matters: draw the frame before lighting the panel, darken it before stopping. */
    if (was_dark && next != SCREEN_DARK) {
        if (s_wake_tally[s_wake_cause] < UINT16_MAX) {
            s_wake_tally[s_wake_cause]++;
        }
        s_wake_cause = WAKE_TOUCH;
        s_lit_since_ms = lv_tick_get();
        set_drawing(true);
        pet_view_set_action(s_brain.action);
        show_missed();
    }
    int bright = s_set.brightness;
    int dim = plan.idle_pct < bright ? plan.idle_pct : bright;
    set_brightness(next == SCREEN_BRIGHT ? bright : (next == SCREEN_DIM ? dim : 0));
    if (!was_dark && next == SCREEN_DARK) {
        s_lit_ms += lv_tick_get() - s_lit_since_ms;
        set_drawing(false);
        s_glance = false;
    }
    /*
     * With "Touch" off a dark screen does not feel a finger at all, so a
     * pocket cannot light it or press what is on it. Lit, it works as ever.
     */
    lv_indev_enable(NULL, !(s_set.touch_off && next == SCREEN_DARK));

    /* Low battery: the pet is drowsy; nearly flat: make sure it is saved. */
    static bool was_critical;
    s_drowsy = plan.drowsy;
    if (plan.critical && !was_critical) {
        pet_app_save_now();
    }
    was_critical = plan.critical;
}

bool pet_app_screen_dark(void)
{
    return s_screen == SCREEN_DARK;
}

bool pet_app_touch_wakes(void)
{
    return !s_set.touch_off;
}

void pet_app_take_wakes(pet_wakes_t *out)
{
    uint32_t ms = lv_tick_get();
    if (s_screen != SCREEN_DARK) {
        s_lit_ms += ms - s_lit_since_ms;
        s_lit_since_ms = ms;
    }
    *out = (pet_wakes_t){.touch = s_wake_tally[WAKE_TOUCH], .raise = s_wake_tally[WAKE_RAISE],
                         .button = s_wake_tally[WAKE_BUTTON], .lit_s = s_lit_ms / 1000};
    memset(s_wake_tally, 0, sizeof(s_wake_tally));
    s_lit_ms %= 1000;
}

bool pet_app_raise(void)
{
    if (s_hal && !s_set.raise_off && s_screen == SCREEN_DARK &&
        !(s_hal->usb_powered && s_hal->usb_powered())) {
        s_forced_dark = false;
        s_glance = true;
        s_wake_cause = WAKE_RAISE;
        lv_display_trigger_activity(NULL);
        s_last_idle_ms = 0;
        screen_update();    /* now, not at the next slow tick of a dark screen */
        return s_screen != SCREEN_DARK;
    }
    return false;
}

/*
 * Burn-in guard. An AMOLED left showing the same picture for months wears
 * unevenly, so once a minute the pet's area and the two strips move a few
 * pixels along a slow loop. Too little to notice, enough to spread the wear.
 */
#define BURN_GUARD_MS 60000

static lv_obj_t *s_guard_area, *s_guard_top, *s_guard_bottom;

static void burn_guard(lv_timer_t *t)
{
    LV_UNUSED(t);
    static const int8_t PET[8][2] = {{0, 0},   {8, 4},   {12, -4}, {6, -10},
                                     {-4, -12}, {-12, -4}, {-8, 6}, {0, 10}};
    static const int8_t STRIP[4][2] = {{0, 0}, {3, 1}, {-2, 2}, {-3, 0}};
    static unsigned step;
    step++;
    lv_obj_set_style_translate_x(s_guard_area, PET[step % 8][0], 0);
    lv_obj_set_style_translate_y(s_guard_area, PET[step % 8][1], 0);
    lv_obj_set_style_translate_x(s_guard_top, STRIP[step % 4][0], 0);
    lv_obj_set_style_translate_y(s_guard_top, STRIP[step % 4][1], 0);
    lv_obj_set_style_translate_x(s_guard_bottom, STRIP[(step + 2) % 4][0], 0);
    lv_obj_set_style_translate_y(s_guard_bottom, -STRIP[(step + 2) % 4][1], 0);
}

void pet_app_button(void)
{
    if (s_screen == SCREEN_DARK) {
        s_forced_dark = false;
        s_wake_cause = WAKE_BUTTON;
        lv_display_trigger_activity(NULL);
    } else {
        s_forced_dark = true;
    }
    /* Keep the next idle reading from looking like a touch. */
    s_last_idle_ms = 0;
}

/* ---- Settings ----------------------------------------------------------- */

#define SETTINGS_MAGIC   0x53
#define SETTINGS_VERSION 7      /* 1 had no volume: 8 bytes. 2 no dev log: 9. 3 no life speed: 10. 4 no raise: 11. 5 no text size: 12. 6 no touch: 13 */

/* Growth speed for each choice of "Life speed": a whole life in 12, 6 and 3 weeks. */
static const float LIFE_SPEED[LIFE_SPEED_COUNT] = {
    [LIFE_SLOW] = 1, [LIFE_MEDIUM] = 2, [LIFE_FAST] = 4,
};

static void settings_save(void)
{
    if (s_hal && s_hal->save_settings) {
        const uint8_t b[14] = {SETTINGS_MAGIC, SETTINGS_VERSION, s_set.brightness, s_set.sound,
                               s_set.quiet_start, s_set.quiet_end, s_set.carry, s_set.paused,
                               s_set.volume, s_set.dev_log, s_set.life_speed, s_set.raise_off,
                               s_set.text_large, s_set.touch_off};
        s_hal->save_settings(b, sizeof(b));
    }
}

static void settings_load(void)
{
    uint8_t b[14] = {0};
    size_t len = s_hal->load_settings ? s_hal->load_settings(b, sizeof(b)) : 0;
    bool v7 = len == 14 && b[1] == 7;
    bool v6 = (len == 13 && b[1] == 6) || v7;
    bool v5 = (len == 12 && b[1] == 5) || v6;
    bool v1 = len == 8 && b[1] == 1, v2 = len == 9 && b[1] == 2, v4 = (len == 11 && b[1] == 4) || v5;
    bool v3 = (len == 10 && b[1] == 3) || v4;
    if (b[0] != SETTINGS_MAGIC || (!v1 && !v2 && !v3)) {
        return;
    }
    if (b[2] >= 20 && b[2] <= 100 && b[3] <= SOUND_LEVEL_ALL && b[4] < 24 && b[5] < 24) {
        s_set = (pet_settings_t){.brightness = b[2], .sound = b[3], .quiet_start = b[4],
                                 .quiet_end = b[5], .carry = b[6] != 0, .paused = b[7] != 0,
                                 .volume = (!v1 && b[8] >= 1 && b[8] <= 5) ? b[8] : 3,
                                 .dev_log = v3 && b[9] != 0,
                                 .life_speed = (v4 && b[10] < LIFE_SPEED_COUNT) ? b[10] : LIFE_SLOW,
                                 .raise_off = v5 && b[11] != 0,
                                 .text_large = v6 && b[12] != 0,
                                 .touch_off = v7 && b[13] != 0,
                                 .speed_fixed = s_set.speed_fixed};
    }
}

/* Settings that live inside the pet's own numbers. */
static void settings_apply(void)
{
    s_pet.tune.night_start_hour = s_set.quiet_start;
    s_pet.tune.night_end_hour = s_set.quiet_end;
    /* growth_speed in tuning.txt wins over the Life speed setting. */
    if (!s_set.speed_fixed) {
        s_tune.growth_speed = LIFE_SPEED[s_set.life_speed];
    }
    pet_set_growth_speed(&s_pet, s_tune.growth_speed);
}

/*
 * Picks the pet up and puts it down at the present moment, exactly as it
 * was: used after a pause and around a change of the clock, so neither
 * starves it nor ages it. between, if given, runs while it is in the air.
 */
static void rebase_pet(void (*between)(int32_t), int32_t arg)
{
    static uint8_t buf[PET_SAVE_MAX];   /* static: this runs deep inside a button press */
    size_t len = pet_save(&s_pet, buf, sizeof(buf));
    const pet_tuning_t tune = s_pet.tune;
    if (between) {
        between(arg);
    }
    if (len == 0 || !pet_load(&s_pet, &tune, buf, len, s_hal->now_s())) {
        LV_LOG_WARN("could not carry the pet across a time change");
    }
}

/* The one small line on the home screen follows "Text size" too. */
static void text_size_apply(void)
{
    if (s_mood) {
        lv_obj_set_style_text_font(s_mood, s_set.text_large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    }
}

static void setting_step(pet_setting_id_t what, int dir)
{
    switch (what) {
    case SET_BRIGHTNESS: {
        int v = s_set.brightness + 20 * dir;
        s_set.brightness = (uint8_t)(v < 20 ? 20 : (v > 100 ? 100 : v));
        break;
    }
    case SET_SOUND:
        s_set.sound = (uint8_t)((s_set.sound + 3 + dir) % 3);
        if (s_set.sound != SOUND_LEVEL_OFF) {
            play(SOUND_CHIRP);
        }
        break;
    case SET_VOLUME: {
        int v = s_set.volume + dir;
        s_set.volume = (uint8_t)(v < 1 ? 1 : (v > 5 ? 5 : v));
        play(SOUND_JINGLE);     /* so each step can be heard */
        break;
    }
    case SET_CARD:
        if (s_hal->share_card) {
            pet_app_save_now();     /* taking the card back may restart the device */
            s_set.card_shared = s_hal->share_card(!s_set.card_shared);
        }
        return;
    case SET_DEV_LOG:
        if (!s_hal->set_dev_log) {
            return;
        }
        s_set.dev_log = !s_set.dev_log;
        s_hal->set_dev_log(s_set.dev_log);
        break;
    case SET_QUIET_START:
        s_set.quiet_start = (uint8_t)((s_set.quiet_start + 24 + dir) % 24);
        break;
    case SET_QUIET_END:
        s_set.quiet_end = (uint8_t)((s_set.quiet_end + 24 + dir) % 24);
        break;
    case SET_HOUR:
    case SET_MINUTE:
        if (s_hal->adjust_clock) {
            rebase_pet(s_hal->adjust_clock, dir * (what == SET_HOUR ? 3600 : 60));
            pet_app_save_now();
        }
        return;
    case SET_CARRY:
        s_set.carry = !s_set.carry;
        break;
    case SET_RAISE:
        s_set.raise_off = !s_set.raise_off;
        break;
    case SET_TOUCH:
        s_set.touch_off = !s_set.touch_off;
        break;
    case SET_TEXT:
        s_set.text_large = !s_set.text_large;
        text_size_apply();
        break;
    case SET_LIFE_SPEED: {
        int v = s_set.life_speed + dir;
        if (s_set.speed_fixed || v < 0 || v >= LIFE_SPEED_COUNT) {
            return;
        }
        s_set.life_speed = (uint8_t)v;
        break;
    }
    case SET_PAUSE:
        s_set.paused = !s_set.paused;
        if (!s_set.paused) {
            rebase_pet(NULL, 0);
        }
        pet_app_save_now();
        break;
    default:
        return;
    }
    settings_apply();
    settings_save();
}

static void on_setting(pet_setting_id_t what, int dir)
{
    setting_step(what, dir);
    changed();
}

void pet_app_setting_step(pet_setting_id_t what, int dir)
{
    if (s_hal && pet_app_setting_shown(what)) {
        on_setting(what, dir < 0 ? -1 : 1);
    }
}

/* ---- The pet's name, its favourite food and the names of its places ------ */

/* Copies a name, cut to fit; a cut never leaves half of a character behind. */
static void copy_name(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (!src) {
        src = "";
    }
    while (n + 1 < cap && src[n]) {
        dst[n] = src[n];
        n++;
    }
    if (((unsigned char)src[n] & 0xC0) == 0x80) {
        /* Stopped inside a character of several bytes: take its first part back out. */
        while (n > 0 && ((unsigned char)dst[n - 1] & 0xC0) == 0x80) {
            n--;
        }
        if (n > 0) {
            n--;
        }
    }
    dst[n] = '\0';
}

/* Something kept with the pet was edited: keep it. */
static void pet_edited(void)
{
    pet_app_save_now();
    changed();
}

void pet_app_rename(const char *name)
{
    if (s_hal) {
        copy_name(s_pet.name, sizeof(s_pet.name), name);
        pet_edited();
    }
}

bool pet_app_set_favourite(int favourite)
{
    if (!s_hal || favourite < 0 || favourite > PET_FAVOURITE_MAX || s_pet.stage >= STAGE_ADULT) {
        return false;
    }
    s_pet.favourite = (uint8_t)favourite;
    pet_edited();
    return true;
}

bool pet_app_name_place(int habitat, const char *name)
{
    if (!s_hal || habitat < 0 || habitat >= HABITAT_MAX || !s_pet.places.habitat[habitat].prints) {
        return false;
    }
    copy_name(s_pet.places.habitat[habitat].name, HABITAT_NAME_MAX, name);
    pet_edited();
    return true;
}

/* "Start over": a new egg in place of the pet and everything it had. Settings stay. */
static void on_start_over(void)
{
    uint32_t now = s_hal->now_s();
    uint32_t seed = (s_hal->random ? s_hal->random() : 0) ^ (now * 2654435761u) ^ lv_tick_get();
    pet_init(&s_pet, &s_tune, now, seed | 1);
    settings_apply();
    pet_brain_init(&s_brain, NULL, (seed >> 7) ^ lv_rand(1, 0x7FFFFFFF));
    s_recent_count = 0;
    s_best_name[0] = '\0';
    s_best_value = 0;
    s_last_pat = 0;
    s_loaded = false;
    s_retire_told = false;
    pet_app_save_now();
    changed();
    pet_app_show_find("a new egg");
}

void pet_app_start_over(void)
{
    if (s_hal) {
        on_start_over();
    }
}

/* "Retire": the pet moves to the farm and the egg it leaves takes its place. */
#define FAREWELL_MS 2600    /* the pet hops out of the picture */
#define ARRIVAL_MS  1100    /* the egg comes in */

static lv_timer_t *s_farewell;      /* running while the pet is on its way out */

/* The pet has gone: now it really moves to the farm, and its egg comes in. */
static void retire_finish(lv_timer_t *t)
{
    LV_UNUSED(t);
    s_farewell = NULL;      /* a timer that has run its one time deletes itself */
    changed();
    char who[PET_NAME_MAX];
    snprintf(who, sizeof(who), "%s", s_pet.name);
    uint32_t seed = (s_hal->random ? s_hal->random() : 0) ^ (s_pet.now * 2654435761u) ^ lv_tick_get();
    if (!pet_retire(&s_pet, seed | 1)) {
        pet_view_roll_in(ARRIVAL_MS);   /* cannot happen; but never leave the picture empty */
        return;
    }
    settings_apply();
    pet_brain_init(&s_brain, NULL, (seed >> 7) ^ lv_rand(1, 0x7FFFFFFF));
    s_recent_count = 0;
    s_best_name[0] = '\0';
    s_best_value = 0;
    s_last_pat = 0;
    s_retire_told = false;
    pet_app_save_now();
    pet_view_set_look(s_pet.stage, s_pet.form, s_pet.is_static);
    pet_view_roll_in(ARRIVAL_MS);
    char text[64];
    snprintf(text, sizeof(text), "%s is at the farm. An egg!", who[0] ? who : "your pet");
    pet_app_show_find(text);
}

/* "Retire" was held: a farewell first, then retire_finish() does the rest. */
bool pet_app_retire(void)
{
    if (!s_hal || s_farewell || !pet_can_retire(&s_pet)) {
        return false;
    }
    if (s_set.sound != SOUND_LEVEL_OFF && !pet_is_night(&s_pet)) {
        s_last_sound_ms = lv_tick_get() | 1;
        play(SOUND_FANFARE);
    }
    char text[64];
    snprintf(text, sizeof(text), "goodbye, %s!", s_pet.name[0] ? s_pet.name : "little one");
    pet_app_show_find(text);
    pet_view_walk_off(FAREWELL_MS);
    s_farewell = lv_timer_create(retire_finish, FAREWELL_MS + 200, NULL);
    lv_timer_set_repeat_count(s_farewell, 1);
    return true;
}

static void on_retire(void)
{
    pet_app_retire();
}

void pet_app_set_carry(bool carry)
{
    if (s_set.carry != carry) {
        s_set.carry = carry;
        settings_save();
    }
}

bool pet_app_carry(void)
{
    return s_set.carry;
}

bool pet_app_paused(void)
{
    return s_set.paused;
}

pet_power_plan_t pet_app_power_plan(const pet_hal_t *hal)
{
    pet_power_plan_t plan = pet_power_now(hal, s_set.carry, s_set.quiet_start, s_set.quiet_end);
    if (s_set.paused) {
        plan.scan_period_s = 0;
    }
    return plan;
}

bool pet_app_open_page(const char *name)
{
    static const char *const NAMES[PAGE_COUNT] = {
        "home", "diet", "journal", "recent", "menu", "dex", "badges", "farm", "pet",
        "settings", "name", "reset", "places", "retire",
    };
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (!strcmp(name, NAMES[i])) {
            pet_pages_open((pet_page_t)i);
            return true;
        }
    }
    return false;
}

void pet_app_button_long(void)
{
    s_set.carry = !s_set.carry;
    settings_save();
    /* Light the screen to say so; in carry mode it then sleeps as usual. */
    s_forced_dark = false;
    s_last_idle_ms = 0;
    s_wake_cause = WAKE_BUTTON;
    lv_display_trigger_activity(NULL);
    screen_update();
    pet_app_show_find(s_set.carry ? "carry mode: screen sleeps" : "desk mode: screen stays on");
    play(SOUND_CHIRP);
}

static void start_meal(const pet_meal_t *m)
{
    /* Still mid-sniff from the last one (a sped-up simulator): finish it now. */
    reveal_meal();

    memmove(&s_recent[1], &s_recent[0], sizeof(s_recent) - sizeof(s_recent[0]));
    s_recent[0] = (pet_recent_t){.time = s_pet.now, .best = m->best, .foods = (uint16_t)m->foods,
                                 .value = m->value, .new_food = m->new_foods_ever > 0,
                                 .craving = m->craving_satisfied};
    if (s_recent_count < PET_RECENT_MAX) {
        s_recent_count++;
    }

    uint32_t ms = lv_tick_get();
    pet_sound_id_t sound = pet_meal_sound(m);
    uint32_t since = s_last_sound_ms == 0 ? UINT32_MAX : ms - s_last_sound_ms;
    bool with_sound = pet_sound_allowed((pet_sound_level_t)s_set.sound, sound, pet_is_night(&s_pet), since);
    if (with_sound) {
        s_last_sound_ms = ms ? ms : 1;
        play(SOUND_CHIRP);
    }

    if (s_screen == SCREEN_DARK) {
        /*
         * Nobody is looking: no sniff-and-reveal, just the sounds, and a
         * note of it for the catch-up line when the screen wakes.
         */
        if (with_sound) {
            play(sound);
        }
        s_missed.foods += m->foods;
        if (s_missed.foods == m->foods || m->best_value > s_missed.best_value) {
            s_missed.best = m->best;
            s_missed.best_value = m->best_value;
            memcpy(s_missed.name, s_best_name, sizeof(s_missed.name));
        }
    } else {
        s_reveal.meal = *m;
        s_reveal.pending = true;
        s_reveal.with_sound = with_sound;
        memcpy(s_reveal.name, s_best_name, sizeof(s_reveal.name));
        pet_view_set_action(ACT_SNIFF);
        lv_timer_reset(s_reveal.timer);
        lv_timer_resume(s_reveal.timer);
        /* Plugged in, the dimmed screen brightens for the event. */
        if (s_screen == SCREEN_DIM) {
            lv_display_trigger_activity(NULL);
        }
    }
    memset(s_best_name, 0, sizeof(s_best_name));
    s_best_value = 0;
}

void pet_app_set_growth_speed(float speed)
{
    s_tune.growth_speed = speed > 0 ? speed : 1;
    s_set.speed_fixed = true;       /* like tuning.txt, it overrides the Life speed setting */
    pet_set_growth_speed(&s_pet, s_tune.growth_speed);
}

void pet_app_set_sound_level(int level)
{
    s_set.sound = level <= 0 ? SOUND_LEVEL_OFF
                             : (level == 1 ? SOUND_LEVEL_RARE : SOUND_LEVEL_ALL);
    settings_save();
}

void pet_app_save_now(void)
{
    if (!s_hal || !s_hal->save) {
        return;
    }
    static uint8_t buf[PET_SAVE_MAX];
    size_t len = pet_save(&s_pet, buf, sizeof(buf));
    if (len == 0 || !s_hal->save(buf, len)) {
        LV_LOG_WARN("pet save failed");
    }
    s_save_due_ms = lv_tick_get() + SAVE_EVERY_MS;
}

static void pet_timer(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t ms = lv_tick_get();

    if (s_set.paused) {
        /* On a shelf: the pet's time stands still. The screen still works. */
        screen_update();
        if (s_screen != SCREEN_DARK) {
            refresh_pet();
        }
        flush_changed();
        return;
    }

    pet_meal_t meal;
    if (pet_tick(&s_pet, s_hal->now_s(), &meal)) {
        start_meal(&meal);
        changed();
        /* Save soon, but let a run of meals in a busy place share one write. */
        if ((int32_t)(s_save_due_ms - ms) > SAVE_AFTER_MEAL_MS) {
            s_save_due_ms = ms + SAVE_AFTER_MEAL_MS;
        }
    }
    /* While it sniffs at a meal the screen holds still: no new action, and the
       stomach bar waits for the munch. */
    if (pet_take_evolved(&s_pet)) {
        on_evolved();
        changed();
    }
    uint32_t fresh = pet_take_new_badges(&s_pet);
    if (fresh) {
        on_badges(fresh);
        changed();
    }
    bool changed = pet_brain_step(&s_brain, &s_pet, ms);
    screen_update();
    if (!s_reveal.pending && s_screen != SCREEN_DARK) {
        if (changed) {
            pet_view_set_action(s_brain.action);
        }
        refresh_pet();
    }

    if ((int32_t)(ms - s_save_due_ms) >= 0) {
        pet_app_save_now();
    }
    flush_changed();
}

/* ---- What a platform's own screens read (pet_ui.h) ----------------------- */

const pet_hal_t *pet_app_hal(void)
{
    return s_hal;
}

const pet_t *pet_app_pet(void)
{
    return &s_pet;
}

const pet_settings_t *pet_app_settings(void)
{
    return &s_set;
}

int pet_app_recent(const pet_recent_t **list)
{
    if (list) {
        *list = s_recent;
    }
    return s_recent_count;
}

void pet_app_describe(char *out, size_t size)
{
    float time_done, meals_done;
    pet_growth_progress(&s_pet, &time_done, &meals_done);
    int len = snprintf(out, size, "%s, %s%s%s (stage %d%% by time, %d%% by meals), %lu meals, "
                                  "stomach %.1f, traits:",
                       s_loaded ? "loaded from save" : "new egg", pet_stage_name(s_pet.stage),
                       s_pet.form != FORM_NONE ? " " : "", pet_form_name(s_pet.form),
                       (int)(time_done * 100), (int)(meals_done * 100),
                       (unsigned long)s_pet.meals, (double)s_pet.stomach);
    for (int i = 0; i < TRAIT_COUNT && len < (int)size; i++) {
        if (pet_has_trait(&s_pet, (pet_trait_t)i)) {
            len += snprintf(out + len, size - len, " %s", pet_trait_name((pet_trait_t)i));
        }
    }
}

static void on_tap(void)
{
    if (preview_next()) {
        return;
    }
    /* A pat cheers the pet up, but only so often. */
    if (s_last_pat == 0 || s_pet.now - s_last_pat >= PAT_COOLDOWN_S) {
        s_last_pat = s_pet.now;
        pet_pat(&s_pet);
    }
}

static void hide_find(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_find) {
        lv_obj_add_flag(s_find, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_pause(s_find_timer);
    notice(PET_NOTICE_LINE_GONE, NULL);
}

static lv_obj_t *plain_box(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/*
 * The settings files. Each is read once, here; a wrong one is ignored whole
 * and named in the warning. The built-in values are also written out beside
 * them as a reference to copy from.
 */
static char s_config_warning[112];

static void config_reference(const char *name, const char *text, size_t len, char *scratch)
{
    if (!len || !s_hal->read_file || !s_hal->write_file) {
        return;
    }
    size_t have = s_hal->read_file(name, scratch, PET_CONFIG_TEXT_MAX);
    if (have != len || memcmp(scratch, text, len) != 0) {
        s_hal->write_file(name, text, len);
    }
}

static void config_warn(const char *file, const pet_config_error_t *err)
{
    if (s_config_warning[0]) {
        return;
    }
    if (err->line) {
        snprintf(s_config_warning, sizeof(s_config_warning), "%s line %d: %s", file, err->line, err->why);
    } else {
        snprintf(s_config_warning, sizeof(s_config_warning), "%s: %s", file, err->why);
    }
}

/* Reads one file's text. False, with a warning, when it is too big to be one of ours. */
static bool config_read(const char *file, char *text)
{
    size_t n = s_hal->read_file(file, text, PET_CONFIG_TEXT_MAX - 1);
    text[n] = 0;
    if (n >= PET_CONFIG_TEXT_MAX - 1) {
        if (!s_config_warning[0]) {
            snprintf(s_config_warning, sizeof(s_config_warning), "%s: file too big", file);
        }
        return false;
    }
    return true;
}

static void config_load(void)
{
    s_tune = pet_tuning_default();
    food_values_t foods = food_values_default();
    s_config_warning[0] = 0;
    char *text = s_hal->read_file ? lv_malloc(PET_CONFIG_TEXT_MAX) : NULL;
    char *ref = text ? lv_malloc(PET_CONFIG_TEXT_MAX) : NULL;
    if (text && ref) {
        pet_config_error_t err;
        if (config_read(PET_CONFIG_TUNING_FILE, text)) {
            if (!pet_config_tuning(text, &s_tune, &err)) {
                config_warn(PET_CONFIG_TUNING_FILE, &err);
            } else {
                s_set.speed_fixed = pet_config_gives(text, "growth_speed");
            }
        }
        if (config_read(PET_CONFIG_FOODS_FILE, text) && !pet_config_foods(text, &foods, &err)) {
            config_warn(PET_CONFIG_FOODS_FILE, &err);
        }
        if (config_read(PET_CONFIG_FOOD_NAMES_FILE, text) && !pet_config_food_names(text, &err)) {
            config_warn(PET_CONFIG_FOOD_NAMES_FILE, &err);
        }
        if (config_read(PET_CONFIG_PLACE_NAMES_FILE, text) && !pet_config_place_names(text, &err)) {
            config_warn(PET_CONFIG_PLACE_NAMES_FILE, &err);
        }
        if (config_read(PET_CONFIG_JOURNAL_FILE, text) && !pet_config_journal(text, &err)) {
            config_warn(PET_CONFIG_JOURNAL_FILE, &err);
        }
        config_reference("tuning-defaults.txt", ref, pet_config_tuning_text(ref, PET_CONFIG_TEXT_MAX), text);
        config_reference("foods-defaults.txt", ref, pet_config_foods_text(ref, PET_CONFIG_TEXT_MAX), text);
        config_reference("food-names-defaults.txt", ref,
                         pet_config_food_names_text(ref, PET_CONFIG_TEXT_MAX), text);
        config_reference("place-names-defaults.txt", ref,
                         pet_config_place_names_text(ref, PET_CONFIG_TEXT_MAX), text);
        config_reference("journal-defaults.txt", ref, pet_config_journal_text(ref, PET_CONFIG_TEXT_MAX), text);
    }
    if (ref) {
        lv_free(ref);
    }
    if (text) {
        lv_free(text);
    }
    food_values_set(&foods);
}

const char *pet_app_config_warning(void)
{
    return s_config_warning;
}

void pet_app_start(const pet_hal_t *hal)
{
    s_hal = hal;
    uint32_t now = hal->now_s();
    config_load();
    /* The settings first: the Life speed chosen there is the speed the pet wakes at. */
    settings_load();
    if (!s_set.speed_fixed) {
        s_tune.growth_speed = LIFE_SPEED[s_set.life_speed];
    }

    /* Wake the saved pet if there is one; otherwise a new egg. */
    static uint8_t buf[PET_SAVE_MAX];   /* static: too big for a small stack */
    size_t len = hal->load ? hal->load(buf, sizeof(buf)) : 0;
    s_loaded = len > 0 && pet_load(&s_pet, &s_tune, buf, len, now);
    /*
     * LVGL's own random numbers start the same way on every boot, which
     * would give every egg the same traits. Mix in the platform's.
     */
    uint32_t seed = (hal->random ? hal->random() : 0) ^ (now * 2654435761u);
    if (!s_loaded) {
        pet_init(&s_pet, &s_tune, now, seed | 1);
    }
    settings_apply();
    pet_brain_init(&s_brain, NULL, (seed >> 7) ^ lv_rand(1, 0x7FFFFFFF));
    s_save_due_ms = lv_tick_get() + SAVE_EVERY_MS;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    int32_t sw = lv_display_get_horizontal_resolution(NULL);
    int32_t sh = lv_display_get_vertical_resolution(NULL);
    int32_t strip_h = (sh - PET_AREA_SIZE) / 2;
    if (strip_h < 32) strip_h = 32;
    /* The panels have rounded corners, so keep strip content off the edges. */
    int32_t strip_w = sw - 2 * 44;

    /* Only a platform with nothing around the pet can have it at another size. */
    int32_t pet_size = hal->pet_only && hal->pet_size > 0 ? hal->pet_size : PET_AREA_SIZE;
    lv_obj_t *area = plain_box(scr, pet_size, pet_size);
    pet_view_set_size(pet_size);
    lv_obj_center(area);
    pet_view_set_look(s_pet.stage, s_pet.form, s_pet.is_static);
    pet_view_create(area);
    pet_view_set_tap_cb(on_tap);
    /* A platform with screens of its own (pet_only) gets the pet's square and nothing else. */
    const bool full = !hal->pet_only;
    if (full) {
        pet_view_set_long_cb(pet_pages_open_card);

        /* Latest find, along the bottom edge of the pet area. */
        s_find = lv_label_create(area);
        lv_obj_set_width(s_find, PET_AREA_SIZE - 40);
        lv_label_set_long_mode(s_find, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(s_find, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(s_find, lv_color_hex(COLOR_FIND), 0);
        lv_obj_align(s_find, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_add_flag(s_find, LV_OBJ_FLAG_HIDDEN);
    }
    s_find_timer = lv_timer_create(hide_find, FIND_SHOW_MS, NULL);
    lv_timer_pause(s_find_timer);
    s_reveal.timer = lv_timer_create(reveal_timer, SNIFF_MS, NULL);
    lv_timer_pause(s_reveal.timer);

    if (full) {
        /* Top strip */
        lv_obj_t *top = plain_box(scr, strip_w, strip_h);
        lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
        s_clock = lv_label_create(top);
        lv_obj_align(s_clock, LV_ALIGN_LEFT_MID, 0, 4);
        s_battery = lv_label_create(top);
        lv_obj_align(s_battery, LV_ALIGN_RIGHT_MID, 0, 4);

        /* Bottom strip */
        lv_obj_t *bottom = plain_box(scr, strip_w, strip_h);
        lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);

        s_stomach = lv_bar_create(bottom);
        lv_obj_set_size(s_stomach, 64, 12);
        lv_obj_align(s_stomach, LV_ALIGN_LEFT_MID, 0, -4);
        lv_bar_set_range(s_stomach, 0, (int32_t)(s_pet.tune.stomach_max * 10));
        lv_obj_set_style_bg_color(s_stomach, lv_color_hex(0x232830), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_stomach, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_stomach, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);

        s_mood = lv_label_create(bottom);
        text_size_apply();
        lv_obj_align(s_mood, LV_ALIGN_LEFT_MID, 72, -4);

        /* The pet wants something: a bell. Tapping it opens the card, which says what. */
        s_craving = lv_label_create(bottom);
        lv_label_set_text(s_craving, LV_SYMBOL_BELL);
        lv_obj_set_style_text_font(s_craving, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_craving, lv_color_hex(COLOR_WARN), 0);
        lv_obj_align(s_craving, LV_ALIGN_RIGHT_MID, 0, -4);
        lv_obj_add_flag(s_craving, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_ext_click_area(s_craving, 20);
        lv_obj_add_event_cb(s_craving, craving_tap_cb, LV_EVENT_CLICKED, NULL);

        s_guard_area = area;
        s_guard_top = top;
        s_guard_bottom = bottom;
        lv_timer_create(burn_guard, BURN_GUARD_MS, NULL);

        refresh_strip(NULL);
        lv_timer_create(refresh_strip, STRIP_REFRESH_MS, NULL);
    }
    refresh_pet();
    s_pet_timer = lv_timer_create(pet_timer, PET_TICK_MS, NULL);

    static pet_pages_host_t host;
    host = (pet_pages_host_t){
        .pet = &s_pet, .settings = &s_set, .recent = s_recent, .recent_count = &s_recent_count,
        .can_set_clock = hal->adjust_clock != NULL, .can_share_card = hal->share_card != NULL,
        .can_dev_log = hal->set_dev_log != NULL,
        .can_brightness = hal->set_brightness != NULL,
        .can_raise = hal->motion != NULL && !hal->screen_managed,
        .screen_managed = hal->screen_managed, .pet_only = hal->pet_only,
        .dev_log_on = hal->dev_log_on, .dev_log_about = hal->dev_log_about,
        .version = hal->version,
        .now = hal->now_s,
        .change = on_setting, .start_over = on_start_over,
        .preview = on_preview, .retire = on_retire,
    };
    pet_pages_init(scr, &host);
    if (hal->set_dev_log) {
        hal->set_dev_log(s_set.dev_log);
        if (s_set.dev_log) {
            pet_app_show_find(hal->dev_log_on ? "dev log on" : "dev log: names are logged");
        }
    }
    /* A settings file with a mistake was ignored: say which, so it can be fixed. */
    if (s_config_warning[0]) {
        pet_app_show_find(s_config_warning);
    }
    screen_update();
}

void pet_app_feed(food_type_t type, float base_value, const char *name)
{
    if (!s_hal || s_set.paused) {
        return;
    }
    pet_feed(&s_pet, type, base_value);
    /* Remember only the name of the best food so far, for the eating moment. */
    if (s_best_value == 0 || base_value > s_best_value) {
        s_best_value = base_value;
        snprintf(s_best_name, sizeof(s_best_name), "%s", name ? name : "");
    }
}

void pet_app_show_find(const char *text)
{
    show_line(PET_NOTICE_NEWS, text);
}

static void show_line(pet_notice_t kind, const char *text)
{
    /* During the preview the line says which form is showing; finds do not replace it. */
    if (s_preview < 0) {
        find_line(kind, text);
    }
}

static void find_line(pet_notice_t kind, const char *text)
{
    if (!s_find_timer) {
        return;     /* not started yet */
    }
    if (s_find) {
        lv_label_set_text(s_find, text);
        lv_obj_remove_flag(s_find, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_reset(s_find_timer);
    lv_timer_resume(s_find_timer);
    notice(kind, text);
}
