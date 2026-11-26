#include "pet_power.h"

#include "pet_core.h"

/* Scan rhythm, in seconds between scan starts. */
#define SCAN_PLUGGED_S   30
#define SCAN_BATTERY_S   180
#define SCAN_MOVING_S    60     /* on battery and being carried: there is new food about */
#define SCAN_PASSING_S   30     /* and it is going by: a minute is a long way in a car */
#define SCAN_STILL_S     300    /* on battery and lying still: the same food as last time */
#define SCAN_NIGHT_S     900
#define SCAN_LOW_S       600

#define BATTERY_LOW_PCT      15
#define BATTERY_CRITICAL_PCT 5

#define SCREEN_IDLE_MS   15000
#define BRIGHT_PCT       60
#define DIM_PCT          20
#define NIGHT_DIM_PCT    6

pet_power_plan_t pet_power_plan(const pet_power_in_t *in)
{
    pet_power_plan_t p = {
        .screen_idle_ms = SCREEN_IDLE_MS,
        .bright_pct = BRIGHT_PCT,
    };

    if (in->usb) {
        /*
         * Power is not a concern, so scan often. On a desk the screen stays
         * on, dimmed. Carried (on a backpack with a power bank) it sleeps
         * when idle so the pet is not lit up all day; scanning carries on.
         */
        p.scan_period_s = SCAN_PLUGGED_S;
        p.screen_stays_on = !in->carry;
        p.idle_pct = in->carry ? 0 : (in->night ? NIGHT_DIM_PCT : DIM_PCT);
    } else {
        /* Wrist mode: a short burst every few minutes, radio and screen off in between. */
        p.scan_period_s = in->motion == PET_MOTION_PASSING ? SCAN_PASSING_S
                          : (in->motion == PET_MOTION_MOVING ? SCAN_MOVING_S
                          : (in->motion == PET_MOTION_STILL ? SCAN_STILL_S : SCAN_BATTERY_S));
        p.radio_off_between = true;
        p.idle_pct = 0;

        /* Low-battery rules only apply when the level is actually known. */
        if (in->battery_pct >= 0 && in->battery_pct <= BATTERY_CRITICAL_PCT) {
            p.drowsy = true;
            p.critical = true;
            p.scan_period_s = 0;
        } else if (in->battery_pct >= 0 && in->battery_pct <= BATTERY_LOW_PCT) {
            p.drowsy = true;
            p.scan_period_s = SCAN_LOW_S;
        }
    }

    /* At night the pet sleeps and scans rarely, on either power source. */
    if (in->night && p.scan_period_s != 0 && p.scan_period_s < SCAN_NIGHT_S) {
        p.scan_period_s = SCAN_NIGHT_S;
    }
    return p;
}

pet_power_plan_t pet_power_now(const pet_hal_t *hal, bool carry, int start, int end)
{
    int hour = (int)(hal->now_s() % 86400 / 3600);
    bool night = start > end ? (hour >= start || hour < end) : (hour >= start && hour < end);
    const pet_power_in_t in = {
        .usb = hal->usb_powered ? hal->usb_powered() : true,
        .battery_pct = hal->battery_percent ? hal->battery_percent() : -1,
        .night = night,
        .carry = carry,
        .motion = hal->motion ? hal->motion() : PET_MOTION_UNKNOWN,
    };
    return pet_power_plan(&in);
}
