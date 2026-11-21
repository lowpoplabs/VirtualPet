/*
 * Desktop simulator: the shared app, with a live pet, in an SDL window
 * sized like one of the two boards.
 *
 *   virtualpet_sim                       demo: random food, 1 minute = 1 pet hour
 *   virtualpet_sim --replay LOG.csv      relive a watch log (default 10 minutes
 *                                        of log per second)
 *   virtualpet_sim --speed 600           pet seconds per real second
 *   virtualpet_sim --save pet.bin        keep the pet between runs in this file
 *   virtualpet_sim --sound off|rare|all  eating sounds (default all)
 *   virtualpet_sim --growth 2016         grow fast: a whole life in an hour of pet
 *                                        time (so about a minute at the demo speed)
 *   virtualpet_sim --carry             carry mode: plugged in, but the screen
 *                                        sleeps when idle (backpack with power bank)
 *   virtualpet_sim --battery 80         run as if on battery at 80%: the screen
 *                                        goes dark after 15 s; click to wake it
 *   virtualpet_sim --board square        480x480 instead of the watch's 410x502
 *   virtualpet_sim --page diet           open a screen: diet, journal, recent, menu,
 *                                        dex, badges, farm, pet, settings, name
 *   virtualpet_sim --screenshot out.bmp [--after-ms 1500]
 *                                        save one frame and exit (for checks)
 *
 *   virtualpet_sim --swipe left|right|up|down|hold|touch|press|title
 *                                        make that gesture one second in (for checks)
 *
 * Click the pet to pat it, hold it for its card, and drag across the
 * window to swipe: left for diet, right for the journal, up for the menu,
 * down for recent finds.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "log_reader.h"
#include "lvgl.h"
#include "pet_app.h"
#include "pet_sound.h"
#include "screenshot.h"

#define DEMO_SPEED   60
#define REPLAY_SPEED 600

/* ---- Pet time: real time, sped up ------------------------------------ */

static uint32_t s_time_base;     /* pet seconds at launch */
static double s_speed = DEMO_SPEED;

static uint32_t sim_now(void)
{
    return s_time_base + (uint32_t)(SDL_GetTicks() / 1000.0 * s_speed);
}

/* "Set the time": the pet's clock moves; nothing is kept between runs. */
static void sim_adjust_clock(int32_t seconds)
{
    s_time_base += (uint32_t)seconds;
}

static uint32_t sim_random(void)
{
    return (uint32_t)time(NULL) * 2246822519u ^ (uint32_t)SDL_GetPerformanceCounter();
}

/* Plugged in by default; --battery PCT simulates running on the battery. */
static int s_battery_pct = -1;

static int sim_battery(void) { return s_battery_pct < 0 ? 82 : s_battery_pct; }
static bool sim_usb(void) { return s_battery_pct < 0; }

/* There is no backlight to turn down, so a black sheet over everything stands in for it. */
static void sim_brightness(int percent)
{
    static lv_obj_t *shade;
    if (!shade) {
        shade = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(shade);
        lv_obj_set_size(shade, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
        lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE);
    }
    printf("[%6.1fs] screen brightness %d%%\n", SDL_GetTicks() / 1000.0, percent);
    int full = 60;   /* the app's "bright" level counts as fully lit */
    int dark = percent >= full ? 0 : (full - percent) * 255 / full;
    lv_obj_set_style_bg_opa(shade, (lv_opa_t)dark, 0);
}

/* ---- Sound: notes turned into samples and queued to the sound card ---- */

#define AUDIO_RATE 22050

static SDL_AudioDeviceID s_audio;

static void sim_play(const pet_sound_t *sound)
{
    if (!s_audio) {
        return;
    }
    static int16_t buf[AUDIO_RATE * 2];
    int n = 0;
    for (int i = 0; i < sound->count; i++) {
        int len = AUDIO_RATE * sound->notes[i].ms / 1000;
        for (int k = 0; k < len && n < (int)(sizeof(buf) / sizeof(buf[0])); k++) {
            /* Short fade at each end of a note so it does not click. */
            int edge = k < len - k ? k : len - k;
            double env = edge < 80 ? edge / 80.0 : 1.0;
            double amp = sound->notes[i].hz ? 160.0 * sound->volume * env : 0;
            buf[n++] = (int16_t)(amp * sin(6.283185307 * sound->notes[i].hz * k / AUDIO_RATE));
        }
    }
    SDL_QueueAudio(s_audio, buf, (Uint32)(n * sizeof(buf[0])));
}

static void sim_audio_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        return;   /* no sound card: carry on silently */
    }
    SDL_AudioSpec want = {.freq = AUDIO_RATE, .format = AUDIO_S16SYS, .channels = 1, .samples = 1024};
    s_audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (s_audio) {
        SDL_PauseAudioDevice(s_audio, 0);
    }
}

/* ---- Save file (only with --save) ------------------------------------- */

static const char *s_save_path;

static size_t sim_load(void *buf, size_t cap)
{
    FILE *f = fopen(s_save_path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool sim_save(const void *data, size_t len)
{
    FILE *f = fopen(s_save_path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

/* Settings live beside the save file, as PET.bin.cfg. */
static const char *settings_path(void)
{
    static char path[512];
    snprintf(path, sizeof(path), "%s.cfg", s_save_path);
    return path;
}

/* The "Dev log" setting. The simulator has no log of its own to switch. */
static void sim_set_dev_log(bool names)
{
    printf("dev log: %s\n", names ? "names" : "off");
}

static size_t sim_load_settings(void *buf, size_t cap)
{
    FILE *f = fopen(settings_path(), "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool sim_save_settings(const void *data, size_t len)
{
    FILE *f = fopen(settings_path(), "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len;
    return fclose(f) == 0 && ok;
}

/* --card DIR: a folder standing in for the memory card's "pet" folder. */
static const char *s_card_dir;

static size_t sim_read_file(const char *name, char *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_card_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool sim_write_file(const char *name, const char *text, size_t len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", s_card_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    return ok;
}

static pet_hal_t s_hal = {
    .battery_percent = sim_battery,
    .usb_powered = sim_usb,
    .now_s = sim_now,
    .random = sim_random,
    .play = sim_play,
    .set_brightness = sim_brightness,
    .adjust_clock = sim_adjust_clock,
    .set_dev_log = sim_set_dev_log,
};

/* ---- Demo feeder: something random turns up now and then ------------- */

static const struct {
    food_type_t type;
    const char *name;
    int weight;     /* how common */
} DEMO_FOODS[] = {
    {FOOD_WIFI_SECURED, "HomeNet", 30},      {FOOD_BLE_UNKNOWN, "", 30},
    {FOOD_BLE_PHONE, "", 20},                {FOOD_WIFI_HIDDEN, "", 15},
    {FOOD_WIFI_WPA3, "Maple Street", 8},     {FOOD_WIFI_OPEN, "CoffeeShop Guest", 8},
    {FOOD_WIFI_PRINTER, "HP-Print-4A", 5},   {FOOD_BLE_TRACKER, "", 5},
    {FOOD_BLE_TV, "[TV] Living Room", 5},    {FOOD_BLE_WATCH, "Galaxy Watch6", 4},
    {FOOD_BLE_EARBUDS, "AirPods Pro", 4},    {FOOD_WIFI_HOTSPOT, "Sam's iPhone", 3},
    {FOOD_BLE_SPEAKER, "JBL Flip 6", 3},     {FOOD_WIFI_CAR, "SYNC_1A2B", 1},
    {FOOD_BLE_CAR, "Uconnect", 1},
};
#define DEMO_COUNT (sizeof(DEMO_FOODS) / sizeof(DEMO_FOODS[0]))

/*
 * Pretend surroundings for the demo: the networks of a home, and with
 * --card DIR  read settings files (tuning.txt, foods.txt) from this folder,
 *             as the watch does from the card's "pet" folder.
 * --travel N a trip after N seconds to a coffee shop it has never seen.
 */
static int s_travel_after_s = -1;

static void demo_scan(lv_timer_t *t)
{
    LV_UNUSED(t);
    static int ticks;
    bool away = s_travel_after_s >= 0 && ++ticks > s_travel_after_s;
    pet_scan_t scan = {.prints = 5, .wifi_heard = 7, .ble_heard = 4};
    for (int i = 0; i < 5; i++) {
        scan.print[i] = (uint16_t)((away ? 700 : 100) + i);
    }
    if (away) {
        place_kind_t kind;
        uint16_t chain;
        if (place_match_name("Starbucks WiFi", &kind, &chain)) {
            scan.match[0].kind = (uint8_t)kind;
            scan.match[0].chain = chain;
            scan.matches = 1;
        }
    }
    uint32_t ev = pet_app_scan(&scan);
    if (ev) {
        printf("place events: %s%s%s%s (here: %d)\n", ev & PLACE_EV_NEW ? "new " : "",
               ev & PLACE_EV_ARRIVED ? "arrived " : "", ev & PLACE_EV_LEFT ? "left " : "",
               ev & PLACE_EV_BADGE ? "badge " : "", pet_app_here());
    }
}

static void demo_feed(lv_timer_t *t)
{
    int total = 0;
    for (size_t i = 0; i < DEMO_COUNT; i++) {
        total += DEMO_FOODS[i].weight;
    }
    int pick = (int)lv_rand(0, total - 1);
    for (size_t i = 0; i < DEMO_COUNT; i++) {
        pick -= DEMO_FOODS[i].weight;
        if (pick < 0) {
            pet_app_feed(DEMO_FOODS[i].type, food_value(DEMO_FOODS[i].type, lv_rand(0, 9999)),
                         DEMO_FOODS[i].name);
            break;
        }
    }
    lv_timer_set_period(t, lv_rand(2000, 9000));
}

/* ---- Replay feeder: rows from a watch log, as pet time reaches them --- */

static log_reader_t s_log;
static log_row_t s_next;
static bool s_have_next;

static void replay_feed(lv_timer_t *t)
{
    uint32_t now = sim_now();
    while (s_have_next && s_next.time <= now) {
        if (s_next.is_food) {
            pet_app_feed(s_next.type, food_value(s_next.type, lv_rand(0, 9999)), s_next.name);
        }
        s_have_next = log_reader_next(&s_log, &s_next);
    }
    if (!s_have_next) {
        pet_app_show_find("end of log");
        lv_timer_delete(t);
    }
}

/* ---- A made-up swipe, fed in as mouse events, to check navigation ----- */

static int s_swipe_dx, s_swipe_dy, s_swipe_step = -1;
static int s_touch_dy, s_hold_steps = 60;   /* "touch" and "press": a short and a long hold above the middle */

static void swipe_timer(lv_timer_t *t)
{
    enum { MOVES = 8 };
    int moves = (s_swipe_dx || s_swipe_dy) ? MOVES : s_hold_steps;
    lv_display_t *disp = lv_display_get_default();
    int cx = lv_display_get_horizontal_resolution(disp) / 2;
    int cy = lv_display_get_vertical_resolution(disp) / 2 + s_touch_dy;
    SDL_Window *win = NULL;
    Uint32 id = 0;
    for (id = 1; id < 16 && !win; id++) {
        win = SDL_GetWindowFromID(id);
    }
    id--;

    SDL_Event e = {0};
    int n = ++s_swipe_step;
    int x = cx + s_swipe_dx * (n > moves ? moves : n) / moves;
    int y = cy + s_swipe_dy * (n > moves ? moves : n) / moves;
    if (n == 0) {
        e.type = SDL_MOUSEBUTTONDOWN;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.windowID = id;
        e.button.x = x;
        e.button.y = y;
    } else if (n <= moves) {
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = id;
        e.motion.x = x;
        e.motion.y = y;
    } else {
        e.type = SDL_MOUSEBUTTONUP;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.windowID = id;
        e.button.x = x;
        e.button.y = y;
        lv_timer_delete(t);
    }
    SDL_PushEvent(&e);
}

static void swipe_start(lv_timer_t *t)
{
    lv_timer_delete(t);
    lv_timer_create(swipe_timer, 20, NULL);
}

static uint32_t tick_cb(void) { return SDL_GetTicks(); }

static int usage(const char *exe)
{
    fprintf(stderr,
            "usage: %s [--board watch|square] [--replay LOG.csv] [--speed N]\n"
            "       [--save PET.bin] [--sound off|rare|all] [--battery PERCENT] [--carry]\n"
            "       [--growth SPEED] [--page NAME]\n"
            "       [--screenshot file.bmp [--after-ms N]]\n",
            exe);
    return 2;
}

int main(int argc, char **argv)
{
    int w = 410, h = 502;
    const char *shot = NULL, *replay = NULL, *page = NULL;
    uint32_t shot_after = 1500;
    double speed = 0;
    int sound_level = -1;
    bool carry = false, swipe = false;
    double growth = 0;      /* 0 = not asked for */

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--board") && i + 1 < argc) {
            const char *b = argv[++i];
            if (!strcmp(b, "square")) {
                w = 480; h = 480;
            } else if (strcmp(b, "watch")) {
                fprintf(stderr, "unknown board '%s' (use watch or square)\n", b);
                return 2;
            }
        } else if (!strcmp(argv[i], "--replay") && i + 1 < argc) {
            replay = argv[++i];
        } else if (!strcmp(argv[i], "--save") && i + 1 < argc) {
            s_save_path = argv[++i];
            s_hal.load = sim_load;
            s_hal.save = sim_save;
            s_hal.load_settings = sim_load_settings;
            s_hal.save_settings = sim_save_settings;
        } else if (!strcmp(argv[i], "--swipe") && i + 1 < argc) {
            const char *d = argv[++i];
            s_swipe_dx = !strcmp(d, "left") ? -160 : (!strcmp(d, "right") ? 160 : 0);
            s_swipe_dy = !strcmp(d, "up") ? -160 : (!strcmp(d, "down") ? 160 : 0);
            if (!strcmp(d, "title")) {
                s_touch_dy = -223;      /* a hold on the page title, on the watch's screen */
            }
            if (!strcmp(d, "touch") || !strcmp(d, "press")) {
                s_touch_dy = -67;                               /* the "Start over" button */
                s_hold_steps = !strcmp(d, "press") ? 200 : 60;  /* 4 s, or 1.2 s */
            }
            swipe = true;
        } else if (!strcmp(argv[i], "--page") && i + 1 < argc) {
            page = argv[++i];
        } else if (!strcmp(argv[i], "--growth") && i + 1 < argc) {
            growth = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--card") && i + 1 < argc) {
            s_card_dir = argv[++i];
            s_hal.read_file = sim_read_file;
            s_hal.write_file = sim_write_file;
        } else if (!strcmp(argv[i], "--carry")) {
            carry = true;
        } else if (!strcmp(argv[i], "--travel") && i + 1 < argc) {
            s_travel_after_s = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--battery") && i + 1 < argc) {
            s_battery_pct = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sound") && i + 1 < argc) {
            const char *v = argv[++i];
            sound_level = !strcmp(v, "off") ? 0 : (!strcmp(v, "rare") ? 1 : 2);
        } else if (!strcmp(argv[i], "--speed") && i + 1 < argc) {
            speed = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            shot = argv[++i];
        } else if (!strcmp(argv[i], "--after-ms") && i + 1 < argc) {
            shot_after = (uint32_t)atoi(argv[++i]);
        } else {
            return usage(argv[0]);
        }
    }
    s_speed = speed > 0 ? speed : (replay ? REPLAY_SPEED : DEMO_SPEED);

    if (replay) {
        if (!log_reader_open(&s_log, replay, false)) {
            fprintf(stderr, "cannot open %s\n", replay);
            return 1;
        }
        s_have_next = log_reader_next(&s_log, &s_next);
        if (!s_have_next) {
            fprintf(stderr, "no timestamped rows in %s\n", replay);
            return 1;
        }
        s_time_base = s_next.time;
    } else {
        /* Start at the real local time of day. */
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        s_time_base = 20000u * 86400u + (uint32_t)(t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
    }

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t *disp = lv_sdl_window_create(w, h);
    char title[96];
    snprintf(title, sizeof(title), "VirtualPet: %s, %gx speed", replay ? "replay" : "demo", s_speed);
    lv_sdl_window_set_title(disp, title);
    /*
     * LVGL restarts its swipe count whenever a reading shows no movement,
     * and with a mouse it takes extra readings between the mouse's own, so
     * all but the fastest drags were lost. Counting every movement makes
     * any drag of a few dozen pixels a swipe. The boards' touch panels are
     * read on a steady beat and keep LVGL's default.
     */
    lv_indev_set_gesture_min_velocity(lv_sdl_mouse_create(), 0);

    sim_audio_open();
    pet_app_start(&s_hal);
    if (pet_app_config_warning()[0]) {
        printf("settings file ignored: %s\n", pet_app_config_warning());
    }
    /* Only what was asked for on the command line overrides the saved settings. */
    if (sound_level >= 0) {
        pet_app_set_sound_level(sound_level);
    }
    if (carry) {
        pet_app_set_carry(true);
    }
    /* With a swipe the page opens first, so the swipe lands on it. */
    if (page && (!shot || swipe) && !pet_app_open_page(page)) {
        fprintf(stderr, "unknown page '%s'\n", page);
        return 2;
    }
    if (growth > 0) {
        pet_app_set_growth_speed((float)growth);
    }
    char about[160];
    pet_app_describe(about, sizeof(about));
    printf("pet: %s\n", about);
    if (swipe) {
        lv_timer_create(swipe_start, 1000, NULL);
    }
    if (replay) {
        lv_timer_create(replay_feed, 50, NULL);
    } else {
        lv_timer_create(demo_feed, 1500, NULL);
        lv_timer_create(demo_scan, 1000, NULL);
    }

    for (;;) {
        uint32_t wait = lv_timer_handler();
        if (shot && SDL_GetTicks() >= shot_after) {
            pet_app_save_now();
            pet_app_describe(about, sizeof(about));
            printf("pet at exit: %s\n", about);
            if (page && !swipe) {
                /* Opened only now, so the picture shows the pet's state at this moment. */
                if (!pet_app_open_page(page)) {
                    fprintf(stderr, "unknown page '%s'\n", page);
                    return 2;
                }
                lv_refr_now(NULL);
            }
            bool ok = save_screenshot(shot);
            fprintf(ok ? stdout : stderr, ok ? "saved %s\n" : "could not save %s\n", shot);
            return ok ? 0 : 1;
        }
        SDL_Delay(wait < 5 ? 5 : (wait > 16 ? 16 : wait));
    }
}
