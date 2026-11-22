/*
 * Build stage 2: sensing. Brings the board up, then scans passively for
 * Wi-Fi networks and Bluetooth LE devices, turns each new one into a food
 * event, shows it on screen and logs it to the card for tuning.
 *
 * A missing card or a failed peripheral is logged and skipped rather than
 * treated as fatal.
 */
#define _XOPEN_SOURCE 700   /* strptime */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "board.h"
#include "crashlog.h"
#include "foodlog.h"
#include "pet_app.h"
#include "pet_power.h"
#include "sensing.h"

/* Battery level goes into the log about this often (at the next scan after it is due). */
#define BATTERY_LOG_EVERY_US (5 * 60 * 1000000LL)

static const char *TAG = "pet";

/*
 * The clock chip holds local time and the system clock is loaded from it
 * with no time zone, so the system time is already "local seconds" with
 * midnight on a multiple of 86400. clock_setup() guarantees it is past the
 * build time, so it is never 0.
 */
static uint32_t hal_now_s(void)
{
    return (uint32_t)time(NULL);
}

/*
 * The pet's save lives in the chip's own flash (NVS), not on the card, so
 * the pet survives a missing or swapped card. It holds numbers only.
 */
#define SAVE_NAMESPACE "pet"
#define SAVE_KEY       "save"

#define SETTINGS_KEY   "settings"

static size_t blob_load(const char *key, void *buf, size_t cap)
{
    nvs_handle_t nvs;
    if (nvs_open(SAVE_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }
    size_t len = cap;
    esp_err_t err = nvs_get_blob(nvs, key, buf, &len);
    nvs_close(nvs);
    return err == ESP_OK ? len : 0;
}

static bool blob_save(const char *key, const void *data, size_t len)
{
    nvs_handle_t nvs;
    if (nvs_open(SAVE_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(nvs, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err == ESP_OK;
}

static size_t hal_load(void *buf, size_t cap) { return blob_load(SAVE_KEY, buf, cap); }
static bool hal_save(const void *data, size_t len) { return blob_save(SAVE_KEY, data, len); }

/* The owner's settings (brightness, sound, quiet hours, carry or desk, pause): a few bytes. */
static size_t hal_load_settings(void *buf, size_t cap) { return blob_load(SETTINGS_KEY, buf, cap); }
static bool hal_save_settings(const void *data, size_t len)
{
    return blob_save(SETTINGS_KEY, data, len);
}

/* "Set the time" in settings: moves the system clock and the clock chip together. */
static void hal_adjust_clock(int32_t seconds)
{
    struct timeval tv = {.tv_sec = time(NULL) + seconds};
    settimeofday(&tv, NULL);
    struct tm now;
    localtime_r(&tv.tv_sec, &now);
    esp_err_t err = board_clock_set(&now);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "clock chip not updated: %s", esp_err_to_name(err));
    }
}

static esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "storage was unreadable and has been erased; the pet starts over");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return err;
}

/*
 * Sounds play on their own task: a sound takes up to a second and a half
 * with the amplifier's wake-up and wind-down, and the pet asks for one
 * from the screen's task, which must not stall.
 */
static QueueHandle_t s_sound_queue;

static void sound_task(void *arg)
{
    const pet_sound_t *sound;
    for (;;) {
        if (xQueueReceive(s_sound_queue, &sound, portMAX_DELAY) == pdTRUE) {
            board_play(sound);
        }
    }
}

static void hal_play(const pet_sound_t *sound)
{
    /* If two are already waiting, drop this one rather than fall behind. */
    if (s_sound_queue) {
        xQueueSend(s_sound_queue, &sound, 0);
    }
}

/*
 * Power readings are asked for many times a second by the screen logic.
 * The power chip is read once every couple of seconds by the housekeeping
 * task and everyone else gets the last reading.
 */
static volatile int s_battery_pct = -1;
static volatile bool s_usb = true;

static int hal_battery(void) { return s_battery_pct; }
static bool hal_usb(void) { return s_usb; }

static void read_power(void)
{
    s_battery_pct = board_battery_percent();
    s_usb = board_usb_powered();
}

/*
 * Sleep. On battery the chip sleeps whenever every task is waiting, and
 * wakes for the next timer: that is where the battery life comes from.
 * Plugged in it stays awake, because a sleeping chip drops off the USB
 * port and could not be flashed or read.
 *
 * How well it slept is counted, kept across a restart, and reported at
 * the next start and in the card log, since on battery there is no serial
 * port to watch.
 */
typedef struct {
    uint32_t magic;
    uint32_t battery_s;     /* length of the last (or current) spell on battery */
    uint32_t asleep_s;
    uint32_t wakes;
} sleep_stats_t;

#define SLEEP_STATS_MAGIC 0x534C5031u

static RTC_NOINIT_ATTR sleep_stats_t s_sleep;
static volatile int64_t s_asleep_us;
static volatile uint32_t s_wakes;
static int64_t s_battery_since_us;

static esp_err_t on_sleep_exit(int64_t slept_us, void *arg)
{
    (void)arg;
    s_asleep_us += slept_us;
    s_wakes++;
    return ESP_OK;
}

static void sleep_stats_update(void)
{
    const uint32_t spell_s = (uint32_t)((esp_timer_get_time() - s_battery_since_us) / 1000000);
    if (spell_s < 60) {
        return;     /* a moment off the cable must not replace the last real record */
    }
    s_sleep.magic = SLEEP_STATS_MAGIC;
    s_sleep.battery_s = spell_s;
    s_sleep.asleep_s = (uint32_t)(s_asleep_us / 1000000);
    s_sleep.wakes = s_wakes;
}

/* Call whenever the power source may have changed. */
static void sleep_follow_power(bool on_battery)
{
    static int applied = -1;
    if (applied == (int)on_battery) {
        return;
    }
    if (applied == 1) {
        /* Back on USB: the spell on battery is over. */
        sleep_stats_update();
        if (esp_timer_get_time() - s_battery_since_us >= 60 * 1000000LL) {
            foodlog_sleep((int)(s_sleep.battery_s / 60),
                          (int)(s_sleep.asleep_s * 100 / s_sleep.battery_s));
        }
    }
    applied = on_battery;
    if (on_battery) {
        s_asleep_us = 0;
        s_wakes = 0;
        s_battery_since_us = esp_timer_get_time();
    }
    const esp_pm_config_t pm = {
        .max_freq_mhz = 240, .min_freq_mhz = 80, .light_sleep_enable = on_battery};
    esp_err_t err = esp_pm_configure(&pm);
    ESP_LOGI(TAG, "power: %s, sleep %s%s", on_battery ? "battery" : "usb",
             on_battery ? "allowed" : "off", err == ESP_OK ? "" : " (FAILED)");
}

static void hal_brightness(int percent)
{
    board_set_brightness(percent);
}

/* What the raise detector did during the last run (see pet_motion.h). */
typedef struct {
    uint32_t magic;
    uint16_t rests, rests_up, raises, lit, missed;
    int16_t last[3];
} raise_stats_t;

#define RAISE_MAGIC 0x52414953u
static RTC_NOINIT_ATTR raise_stats_t s_raise;

/* How the device is being carried. Fed by the housekeeping task, read by the others. */
static pet_motion_t s_motion;
static pet_gait_t s_gait;       /* how it moved since the last scan, for the log */
static pet_ride_t s_ride;       /* whether those stretches add up to a ride */
static sensing_cycle_t s_last_cycle;    /* what the latest scan heard, for the same row */
static bool s_motion_ok;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static pet_motion_state_t hal_motion(void)
{
    return s_motion_ok ? pet_motion_state(&s_motion, now_ms()) : PET_MOTION_UNKNOWN;
}

/*
 * Settings files live in a folder on the card, where the owner can edit
 * them by lending the card to a computer. They are read once, at start-up.
 */
#define CONFIG_DIR "/sdcard/pet"
static bool s_card_ok;

static size_t hal_read_file(const char *name, char *buf, size_t cap)
{
    if (!s_card_ok) {
        return 0;
    }
    char path[64];
    snprintf(path, sizeof(path), CONFIG_DIR "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static bool hal_write_file(const char *name, const char *text, size_t len)
{
    if (!s_card_ok) {
        return false;
    }
    mkdir(CONFIG_DIR, 0775);    /* fails harmlessly when it is already there */
    char path[64];
    snprintf(path, sizeof(path), CONFIG_DIR "/%s", name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    return ok;
}

static pet_hal_t s_hal = {
    /* .version is filled in at start: it comes from firmware/version.txt by way of the build. */
    .battery_percent = hal_battery,
    .usb_powered = hal_usb,
    .motion = hal_motion,
    .now_s = hal_now_s,
    .random = esp_random,
    .load = hal_load,
    .save = hal_save,
    .play = hal_play,
    .set_brightness = hal_brightness,
    .load_settings = hal_load_settings,
    .save_settings = hal_save_settings,
    .adjust_clock = hal_adjust_clock,
    .read_file = hal_read_file,
    .write_file = hal_write_file,
};

/*
 * The "Card" setting. While the card is with the computer nothing is
 * logged; the pet itself lives in flash and carries on as usual.
 */
/* The "Dev log" setting. */
static void hal_set_dev_log(bool names)
{
    foodlog_set_names(names);
    if (names) {
        ESP_LOGW(TAG, "dev log on: names are logged to the card and here");
    } else {
        ESP_LOGI(TAG, "dev log off");
    }
}

static bool hal_share_card(bool share)
{
    if (!share) {
        board_card_take_back();     /* restarts; does not return if the card was lent */
        return false;
    }
    foodlog_close();
    if (board_card_lend()) {
        ESP_LOGI(TAG, "card lent to the computer");
        return true;
    }
    foodlog_open();
    return false;
}

/*
 * Carry or desk mode used to have a storage slot of its own, before there
 * was a settings screen. It is read once more so the choice carries over.
 */
#define OLD_CARRY_KEY "carry"

static bool old_carry_load(void)
{
    nvs_handle_t nvs;
    uint8_t v = 0;
    if (nvs_open(SAVE_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, OLD_CARRY_KEY, &v);
        nvs_close(nvs);
    }
    return v != 0;
}

/* Housekeeping: watches the side buttons and keeps the power readings fresh. */
#define BUTTON_POLL_MS 30
#define MOTION_POLL_MS 90
#define LONG_PRESS_MS  800
#define POWER_POLL_MS  2000
#define POWER_POLL_BATTERY_MS 300

static void housekeeping_task(void *arg)
{
    bool was_down = board_button_down();
    int held_ms = 0;
    bool long_fired = false;
    int since_power = 0;
    bool was_usb = s_usb;
    int since_motion = 0;
    bool motion_logged = false;
    int since_stats = 0, touch_turn = 0;
    for (;;) {
        /*
         * With the screen off on battery every wake costs, so this loop
         * slows to the motion sensor's pace. A button press shorter than
         * that could be missed; a tenth of a second is not a long press.
         */
        const bool battery = !s_usb;
        const int step = battery && pet_app_screen_dark() ? MOTION_POLL_MS : BUTTON_POLL_MS;
        vTaskDelay(pdMS_TO_TICKS(step));

        if (battery) {
            /* A sleeping chip does not hear the touch interrupt: ask the panel. */
            if (!pet_app_screen_dark() || (++touch_turn & 1)) {
                board_touch_poll();
            }
            since_stats += step;
            if (since_stats >= 10000) {
                since_stats = 0;
                sleep_stats_update();
            }
        }

        /* The motion sensor, about ten times a second. */
        since_motion += step;
        if (since_motion >= MOTION_POLL_MS) {
            since_motion = 0;
            int mg[3];
            if (board_motion_read(mg)) {
                s_motion_ok = true;
                if (!motion_logged) {
                    motion_logged = true;
                    ESP_LOGI(TAG, "motion     ok (%d %d %d mg)", mg[0], mg[1], mg[2]);
                }
                pet_gait_sample(&s_gait, mg[0], mg[1], mg[2], now_ms());
                if (pet_motion_sample(&s_motion, mg[0], mg[1], mg[2], now_ms())) {
                    if (board_lvgl_lock(200)) {
                        s_raise.lit += pet_app_raise();
                        board_lvgl_unlock();
                    } else {
                        s_raise.missed++;
                    }
                }
                /* Tallies for tuning the raise, kept across a restart like the sleep figures. */
                s_raise.magic = RAISE_MAGIC;
                s_raise.rests = s_motion.rests;
                s_raise.rests_up = s_motion.rests_up;
                s_raise.raises = s_motion.raises;
                s_raise.last[0] = (int16_t)mg[0];
                s_raise.last[1] = (int16_t)mg[1];
                s_raise.last[2] = (int16_t)mg[2];
            }
        }

        /*
         * Short press (acts on release): screen on or off.
         * Long press (acts while still held): switch carry and desk mode.
         */
        bool down = board_button_down();
        if (down) {
            held_ms = was_down ? held_ms + step : 0;
            if (held_ms >= LONG_PRESS_MS && !long_fired) {
                long_fired = true;
                if (board_lvgl_lock(200)) {
                    pet_app_button_long();
                    bool carry = pet_app_carry();
                    board_lvgl_unlock();
                    ESP_LOGI(TAG, "button held: now in %s mode", carry ? "carry" : "desk");
                }
            }
        } else if (was_down) {
            if (!long_fired) {
                ESP_LOGI(TAG, "button pressed");
                if (board_lvgl_lock(100)) {
                    pet_app_button();
                    board_lvgl_unlock();
                }
            }
            long_fired = false;
        }
        was_down = down;

        /* On battery, look for the cable often: the chip must be awake to answer the computer. */
        since_power += step;
        if (since_power >= (battery ? POWER_POLL_BATTERY_MS : POWER_POLL_MS)) {
            since_power = 0;
            read_power();
            sleep_follow_power(!s_usb);
            if (s_usb != was_usb) {
                ESP_LOGI(TAG, "power: now on %s, battery %d%%", s_usb ? "usb" : "battery",
                         s_battery_pct);
                was_usb = s_usb;
            }
        }
    }
}

/* The scanner asks this for its rhythm; the answer comes from the shared power plan. */
static uint32_t scan_period_s(bool *radio_off_between)
{
    /* Follows the settings screen too: carry or desk, quiet hours, and no scanning while paused. */
    const pet_power_plan_t plan = pet_app_power_plan(&s_hal);
    *radio_off_between = plan.radio_off_between;
    return plan.scan_period_s;
}

static void report(const char *what, esp_err_t err)
{
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "%-10s ok", what);
    } else {
        ESP_LOGW(TAG, "%-10s FAILED: %s", what, esp_err_to_name(err));
    }
}

/*
 * Loads the system clock from the clock chip. A chip that lost its time,
 * or is behind the moment this firmware was built, is set to the build
 * time: roughly right just after flashing, and never earlier than
 * reality. The owner can correct it from the settings screen.
 */
static esp_err_t clock_setup(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    char stamp[40];
    struct tm built = {.tm_isdst = -1};
    snprintf(stamp, sizeof(stamp), "%s %s", app->date, app->time);
    time_t build_time = strptime(stamp, "%b %d %Y %H:%M:%S", &built) ? mktime(&built) : 0;

    struct tm chip;
    esp_err_t err = board_clock_read(&chip);
    time_t now = (err == ESP_OK) ? mktime(&chip) : 0;

    if (now < build_time) {
        ESP_LOGW(TAG, "clock chip %s; setting it to the build time",
                 err == ESP_OK ? "is behind" : "has no valid time");
        now = build_time;
        err = board_clock_set(&built);
    }
    if (now > 0) {
        struct timeval tv = {.tv_sec = now};
        settimeofday(&tv, NULL);
    }
    return err;
}

/* For tuning: a network already eaten has come much closer. Logged, not eaten again. */
static void on_heard(const food_event_t *ev)
{
    ESP_LOGI(TAG, "heard closer: %s at %d dBm", food_label(ev->type), ev->rssi);
    foodlog_heard(ev);
}

static void on_food(const food_event_t *ev)
{
    if (foodlog_names()) {
        ESP_LOGI(TAG, "found %-14s %-24s +%.1f  %4d dBm  [%s]", food_label(ev->type), ev->name,
                 ev->value, ev->rssi, ev->detail);
    } else {
        ESP_LOGI(TAG, "found %-14s +%.1f  %4d dBm", food_label(ev->type), ev->value, ev->rssi);
    }
    foodlog_food(ev);

    /*
     * Hand it to the pet, which eats in batches. The name goes along only
     * to be shown at the eating moment. The pet lives on the LVGL side, so
     * this needs the LVGL lock; a find is dropped rather than block a scan.
     */
    if (board_lvgl_lock(200)) {
        pet_app_feed(ev->type, ev->value, ev->name);
        board_lvgl_unlock();
    }
}

/* The surroundings after each scan: habitats, place badges, off the grid. */
static void on_scan(const pet_scan_t *scan)
{
    uint32_t ev = 0;
    int here = -1;
    /* Add what the motion sensor saw since the last scan: carried, or lying still. */
    pet_scan_t with_motion = *scan;
    with_motion.motion = s_motion_ok ? (pet_motion_take_moved(&s_motion) ? 2 : 1) : 0;
    scan = &with_motion;
    if (s_motion_ok) {
        /* The other task is still adding readings; a reading lost here does not matter. */
        int samples, shake, steps;
        pet_gait_take(&s_gait, &samples, &shake, &steps);
        if (pet_ride_scan(&s_ride, with_motion.motion == 2, s_last_cycle.wifi_seen, s_last_cycle.wifi_new)) {
            with_motion.motion = 3;
        }
        const char *state = with_motion.motion == 3 ? "riding" : (with_motion.motion == 2 ? "carried" : "still");
        ESP_LOGI(TAG, "motion: %s, shake %d mg, %d steps a minute (%d readings); scan: %d prints, %d names, best %d dBm",
                 state, shake, steps, samples, s_last_cycle.prints, s_last_cycle.matches,
                 s_last_cycle.wifi_best);
        foodlog_motion(state, shake, steps, samples, &s_last_cycle);
    }
    if (board_lvgl_lock(200)) {
        ev = pet_app_scan(scan);
        here = pet_app_here();
        board_lvgl_unlock();
    }
    static const struct {
        uint32_t bit;
        const char *what;
    } EVENTS[] = {
        {PLACE_EV_LEFT, "leave"},    {PLACE_EV_NEW, "new"},         {PLACE_EV_ARRIVED, "arrive"},
        {PLACE_EV_BADGE, "badge"},   {PLACE_EV_OFFGRID, "offgrid"},
    };
    for (size_t i = 0; i < sizeof(EVENTS) / sizeof(EVENTS[0]); i++) {
        if (ev & EVENTS[i].bit) {
            ESP_LOGI(TAG, "place: %s (habitat %d)", EVENTS[i].what, here);
            foodlog_place(EVENTS[i].what, here);
        }
    }
}

/* The number that scrambles habitat prints: random, made once, kept with the settings. */
static uint32_t place_salt(void)
{
    uint32_t salt = 0;
    if (blob_load("placesalt", &salt, sizeof(salt)) != sizeof(salt) || salt == 0) {
        salt = esp_random() | 1;
        blob_save("placesalt", &salt, sizeof(salt));
    }
    return salt;
}

static void on_cycle(const sensing_cycle_t *c)
{
    s_last_cycle = *c;
    static int64_t last_battery_row = -BATTERY_LOG_EVERY_US;
    ESP_LOGI(TAG, "cycle: wifi %d heard / %d eaten, bluetooth %d heard / %d eaten (%s, %d%%)",
             c->wifi_seen, c->wifi_new, c->ble_seen, c->ble_new, s_usb ? "usb" : "battery",
             s_battery_pct);
    /* Internal RAM is the tight resource; the screen stalls if it runs out. */
    ESP_LOGI(TAG, "free internal RAM %u KB (largest block %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024));
    /* By the clock, not by cycle count: cycles are minutes apart on battery. */
    if (esp_timer_get_time() - last_battery_row >= BATTERY_LOG_EVERY_US) {
        last_battery_row = esp_timer_get_time();
        foodlog_battery(s_battery_pct, s_usb);
    }
    foodlog_flush();
}

void app_main(void)
{
    ESP_LOGI(TAG, "board: %s", board_name());
    static char started[160];
    crashlog_boot(started, sizeof(started));

    report("storage", storage_init());
    report("power", board_power_init());
    read_power();
    report("clock", clock_setup());
    board_buttons_init();

    /*
     * The CPU drops from 240 to 80 MHz whenever nothing needs the speed,
     * and on battery the chip sleeps between bursts of work.
     */
    if (s_sleep.magic == SLEEP_STATS_MAGIC && s_sleep.battery_s >= 60) {
        ESP_LOGI(TAG, "last time on battery: %lu min, asleep %lu%%, %.1f wakes a second",
                 (unsigned long)(s_sleep.battery_s / 60),
                 (unsigned long)(s_sleep.asleep_s * 100 / s_sleep.battery_s),
                 (double)s_sleep.wakes / s_sleep.battery_s);
    }
    if (s_raise.magic == RAISE_MAGIC) {
        ESP_LOGI(TAG, "last run's raises: came to rest %u times (%u facing up), %u raises, %u lit the screen, %u missed; last reading %d %d %d",
                 s_raise.rests, s_raise.rests_up, s_raise.raises, s_raise.lit, s_raise.missed,
                 s_raise.last[0], s_raise.last[1], s_raise.last[2]);
    }
    memset(&s_raise, 0, sizeof(s_raise));
    esp_pm_sleep_cbs_register_config_t cbs = {.exit_cb = on_sleep_exit};
    esp_pm_light_sleep_register_cbs(&cbs);
    sleep_follow_power(!s_usb);

    /* The card first: the pet's settings files are on it. */
    esp_err_t card = board_sdcard_mount();
    report("sdcard", card);
    s_card_ok = card == ESP_OK;
    if (s_card_ok) {
        report("foodlog", foodlog_open());
        foodlog_start(started);
    }

    esp_err_t err = board_display_start();
    report("display", err);
    if (err == ESP_OK) {
        if (board_lvgl_lock(1000)) {
            s_hal.set_dev_log = hal_set_dev_log;
            s_hal.version = esp_app_get_description()->version;
            if (board_card_can_lend()) {
                s_hal.share_card = hal_share_card;
            }
            pet_app_start(&s_hal);
            if (pet_app_config_warning()[0]) {
                ESP_LOGW(TAG, "settings file ignored: %s", pet_app_config_warning());
            } else {
                ESP_LOGI(TAG, "settings files ok");
            }
            uint8_t probe[8];
            if (hal_load_settings(probe, sizeof(probe)) == 0 && old_carry_load()) {
                pet_app_set_carry(true);
            }
            ESP_LOGI(TAG, "mode: %s%s",
                     pet_app_carry() ? "carry (screen sleeps)" : "desk (screen stays on)",
                     pet_app_paused() ? ", pet paused" : "");
            char about[160];
            pet_app_describe(about, sizeof(about));
            ESP_LOGI(TAG, "pet: %s", about);
            board_lvgl_unlock();
        }
    }

    /* Start-up sound doubles as the speaker check. */
    report("sound", board_play(pet_sound(SOUND_JINGLE)));
    s_sound_queue = xQueueCreate(2, sizeof(const pet_sound_t *));
    if (s_sound_queue) {
        xTaskCreate(sound_task, "sound", 4096, NULL, 3, NULL);
    }

    ESP_LOGI(TAG, "battery %d%%, usb %s", s_battery_pct, s_usb ? "yes" : "no");
    xTaskCreate(housekeeping_task, "housekeeping", 4096, NULL, 2, NULL);

    static sensing_callbacks_t callbacks = {
        .on_food = on_food,
        .on_heard = on_heard,
        .on_cycle = on_cycle,
        .on_scan = on_scan,
        .scan_period_s = scan_period_s,
    };
    callbacks.place_salt = place_salt();
    report("sensing", sensing_start(&callbacks));
}
