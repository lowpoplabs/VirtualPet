#include "foodlog.h"

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#define LOG_PATH "/sdcard/foodlog.csv"

static FILE *s_file;
/* Rows come from several tasks, and the card can be taken away (foodlog_close). */
static SemaphoreHandle_t s_lock;

static bool begin(void)
{
    if (!s_lock || !xSemaphoreTake(s_lock, portMAX_DELAY)) {
        return false;
    }
    if (!s_file) {
        xSemaphoreGive(s_lock);
        return false;
    }
    return true;
}

static void end(void)
{
    xSemaphoreGive(s_lock);
}

static void write_time(void)
{
    char stamp[24] = "";
    time_t now = time(NULL);
    if (now > 1700000000) {   /* clock has been set */
        struct tm t;
        localtime_r(&now, &t);
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &t);
    }
    fprintf(s_file, "%s,%lld,", stamp, (long long)(esp_timer_get_time() / 1000000));
}

/* The "Dev log" setting: off unless the owner turns it on. Read from the sensing tasks, written from the screen's. */
static volatile bool s_names;

void foodlog_set_names(bool on)
{
    s_names = on;
}

bool foodlog_names(void)
{
    return s_names;
}

/* Quoted CSV field; names come off the air and may hold commas or quotes. */
static void write_quoted(const char *text)
{
    fputc('"', s_file);
    for (; *text; text++) {
        if (*text == '"') {
            fputc('"', s_file);
        }
        fputc(*text, s_file);
    }
    fputc('"', s_file);
}

esp_err_t foodlog_open(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    struct stat st;
    bool is_new = stat(LOG_PATH, &st) != 0 || st.st_size == 0;
    s_file = fopen(LOG_PATH, "a");
    if (!s_file) {
        return ESP_FAIL;
    }
    if (is_new) {
        fputs("time,uptime_s,kind,food,value,rssi,name,detail\n", s_file);
    }
    write_time();
    fputs("boot,,,,,\n", s_file);
    fflush(s_file);
    fsync(fileno(s_file));
    return ESP_OK;
}

void foodlog_food(const food_event_t *ev)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "%s,%s,%.1f,%d,", food_is_ble(ev->type) ? "ble" : "wifi",
            food_label(ev->type), ev->value, ev->rssi);
    if (s_names) {
        write_quoted(ev->name);
        fputc(',', s_file);
        write_quoted(ev->detail);
    } else {
        fputc(',', s_file);
    }
    fputc('\n', s_file);
    end();
}

void foodlog_battery(int percent, bool usb)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "battery,%s,%d,,,\n", usb ? "usb" : "battery", percent);
    end();
}

void foodlog_heard(const food_event_t *ev)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "heard,%s,,%d,", food_label(ev->type), ev->rssi);
    if (s_names) {
        write_quoted(ev->name);
    }
    fprintf(s_file, ",\n");
    end();
}

void foodlog_motion(const char *state, int shake, int steps_per_min, int samples,
                    const sensing_cycle_t *scan)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "motion,%s,%d,%d,,n=%d heard=%d prints=%d names=%d best=%d new=%d fixed=%d same=%d\n",
            state, shake, steps_per_min, samples, scan->wifi_seen, scan->prints, scan->matches,
            scan->wifi_best, scan->wifi_new, scan->ride_prints, scan->wifi_same);
    end();
}

void foodlog_wakes(int touch, int raise, int button, int lit_s)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "wakes,screen,%d,,,touch=%d raise=%d button=%d\n", lit_s, touch, raise, button);
    end();
}

void foodlog_place(const char *what, int habitat)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "place,%s,%d,,,\n", what, habitat);
    end();
}

void foodlog_sleep(int minutes, int asleep_percent)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "sleep,battery,%d,%d,,\n", minutes, asleep_percent);
    end();
}

void foodlog_start(const char *why)
{
    if (!begin()) {
        return;
    }
    write_time();
    fprintf(s_file, "start,%s,,,,\n", why);
    fflush(s_file);
    fsync(fileno(s_file));
    end();
}

void foodlog_flush(void)
{
    if (!begin()) {
        return;
    }
    fflush(s_file);
    fsync(fileno(s_file));
    end();
}

void foodlog_close(void)
{
    if (!begin()) {
        return;
    }
    fclose(s_file);
    s_file = NULL;
    end();
}
