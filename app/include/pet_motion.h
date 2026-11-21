/*
 * Motion: what the accelerometer says about how the device is being
 * carried. Pure logic; the board feeds it samples about ten times a second.
 *
 * It answers three things: was the device just turned to be looked at (a
 * wrist raise), is it being carried about right now, and has it moved at
 * all since the last scan.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PET_MOTION_UNKNOWN,     /* no sensor, or neither of the below */
    PET_MOTION_STILL,       /* has not been carried for ten minutes */
    PET_MOTION_MOVING,      /* carried within the last two minutes */
} pet_motion_state_t;

typedef struct {
    int16_t prev[3];
    bool primed;
    int16_t ref[3];             /* which way was down when it last lay steady */
    bool has_ref;
    bool away;                  /* the screen has faced aside or down since it last lay steady */
    uint8_t stir;               /* unsteady readings since it last lay steady */
    uint16_t rests, rests_up, raises;   /* tallies for tuning: came to rest, did so facing up, counted as a raise */
    uint8_t steady;             /* samples in a row with hardly any change */
    uint32_t jolt_ms[3];        /* the last few jolts */
    uint32_t carried_ms;        /* when it was last being carried */
    bool carried_ever;
    volatile bool moved;        /* carried since pet_motion_take_moved() */
} pet_motion_t;

void pet_motion_init(pet_motion_t *m);

/*
 * One reading in thousandths of g, with the time in milliseconds. z points
 * out of the screen, so lying face up reads about +1000.
 * Returns true when the device has just come to rest where its screen can
 * be read after being moved or turned: a wrist raised, the device picked up
 * to look at, or stood up on its edge.
 */
bool pet_motion_sample(pet_motion_t *m, int x, int y, int z, uint32_t ms);

pet_motion_state_t pet_motion_state(const pet_motion_t *m, uint32_t ms);

/* True if it was carried since the last call (clears it). */
bool pet_motion_take_moved(pet_motion_t *m);

/*
 * Gait: a summary of how the device moved over a stretch of time, for
 * telling a walk from a drive in the log. It is fed the same readings and
 * emptied at each scan. Walking shows as a strong rhythm of a step or two
 * a second; a vehicle as a small, steady shake with no rhythm; a desk as
 * nothing at all.
 */
typedef struct {
    uint32_t samples;
    uint32_t first_ms, last_ms;
    uint32_t shake_sum;     /* total change in the strength of the pull, reading to reading */
    uint16_t steps;         /* swings of that strength above and back below its average */
    int32_t average;        /* slow average of the strength, in thousandths of g */
    int32_t prev;
    bool high;
} pet_gait_t;

void pet_gait_init(pet_gait_t *g);
void pet_gait_sample(pet_gait_t *g, int x, int y, int z, uint32_t ms);

/*
 * What was gathered since the last call, which it clears: the number of
 * readings, the average shake in thousandths of g per reading, and steps
 * per minute.
 */
void pet_gait_take(pet_gait_t *g, int *samples, int *shake, int *steps_per_min);

/*
 * Riding: in a car, a bus or a train, as against walking or sitting. Fed
 * once per scan with that stretch's gait and what the scan heard.
 *
 * A ride is a small steady shake with no step rhythm, while the networks
 * around are few and faint. A walk shakes far harder; a desk hardly at
 * all; and a wrist at a desk, which shakes about as much as a car seat,
 * has its own strong network close by. Two scans in a row must agree, so
 * a pause in a walk is not a ride.
 */
typedef struct {
    uint8_t run;    /* scans in a row that looked like a ride */
} pet_ride_t;

/*
 * carried: it moved at all since the last scan. shake and steps_per_min
 * are from pet_gait_take(). wifi_heard is how many networks the scan
 * heard, and wifi_best the strongest in dBm (0 if none). Returns true
 * while it is riding.
 */
bool pet_ride_scan(pet_ride_t *r, bool carried, int shake, int steps_per_min, int wifi_heard, int wifi_best);

#ifdef __cplusplus
}
#endif
