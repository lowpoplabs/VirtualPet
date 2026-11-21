/*
 * Power plan: how often to scan and what the screen does, from where the
 * power is coming from, how much is left and whether it is night.
 *
 * Pure logic, shared by the screen (app) and the scanner (firmware) so the
 * two always agree, and testable on the desktop.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool usb;           /* plugged in (backpack mode) */
    int battery_pct;    /* 0-100, or -1 when unknown */
    bool night;         /* quiet hours */
    /*
     * Where the pet is, set by the owner: false = on a desk (screen may
     * stay on), true = being carried, e.g. on a backpack (screen sleeps
     * when idle even with a power bank attached).
     */
    bool carry;
    /* From the motion sensor: on battery, scan more when carried and less when still. */
    pet_motion_state_t motion;
} pet_power_in_t;

typedef struct {
    /* Seconds from the start of one scan to the start of the next. 0 = do not scan. */
    uint32_t scan_period_s;
    /* Switch the Wi-Fi radio off between scans (it is the biggest idle drain). */
    bool radio_off_between;
    /* Screen: stays on (dimmed when idle) or goes dark when idle. */
    bool screen_stays_on;
    uint32_t screen_idle_ms;    /* idle time before it dims or goes dark */
    int bright_pct;             /* in use */
    int idle_pct;               /* idle; 0 = dark */
    /* Battery is low: the pet is drowsy. */
    bool drowsy;
    /* Battery is nearly flat: save and stop scanning. */
    bool critical;
} pet_power_plan_t;

pet_power_plan_t pet_power_plan(const pet_power_in_t *in);

/* The plan for right now, read through the platform services. Quiet hours are whole hours. */
pet_power_plan_t pet_power_now(const pet_hal_t *hal, bool carry, int night_start_hour,
                               int night_end_hour);

/*
 * The plan in force with the owner's settings applied (carry or desk, quiet
 * hours, pause). Only reads a few bytes, so the scanner may call it from
 * its own task. Works before pet_app_start() with default settings.
 */
pet_power_plan_t pet_app_power_plan(const pet_hal_t *hal);

#ifdef __cplusplus
}
#endif
