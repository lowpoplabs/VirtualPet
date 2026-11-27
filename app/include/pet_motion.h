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
    PET_MOTION_PASSING,     /* carried, and the networks are going by: a ride, or the start of one */
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
 * once per scan with what the scan heard.
 *
 * A ride is networks going by: scan after scan hears ones that were not
 * there a moment ago. Walking keeps the last scan's networks in earshot,
 * and sitting anywhere hears the same ones over and over. Three such scans
 * make a ride, and it lasts through a few scans of empty road. The shake of
 * the thing is no help: a truck shakes like a walk.
 *
 * "Not there a moment ago" is asked of the last few scans, not of everything
 * the pet has ever met: a road it drives every day is still a ride. For that
 * it remembers the scrambled prints of the last few scans' networks, here
 * and nowhere else; they are never saved.
 */
#define RIDE_PRINTS 24  /* networks remembered from one scan, the nearest ones */
#define RIDE_SCANS  6   /* scans remembered: one more than an empty stretch a ride lasts through */

typedef struct {
    uint8_t run;    /* scans whose networks had mostly just come into earshot, since it last stayed put */
    uint8_t quiet;  /* scans in a row since then that heard nothing at all */
    uint8_t at;     /* where the next scan's prints go */
    uint8_t count[RIDE_SCANS];
    uint16_t recent[RIDE_SCANS][RIDE_PRINTS];
    uint8_t fast;   /* scans in a row at a ride's speed, where the speed is known */
    uint8_t slow;   /* scans since then that were not */
} pet_ride_t;

/*
 * carried: it moved at all since the last scan. prints are the scrambled
 * prints of the fixed networks the scan heard, strongest first, and heard
 * how many. *same, if asked for, gets how many of them were in one of the
 * last few scans. Returns true while it is riding.
 */
bool pet_ride_scan(pet_ride_t *r, bool carried, const uint16_t *prints, int heard, int *same);

/* True from the first scan of networks going by: worth looking again soon. */
bool pet_ride_passing(const pet_ride_t *r);

/*
 * The whole of the question, for a platform that may know its speed (from
 * GPS, say): still, carried or riding, as pet_scan_t.motion wants it.
 *
 * sensor is the motion sensor's word since the last scan: 0 = there is
 * none, 1 = lay still, 2 = was carried. speed_mps is the speed at the scan
 * in metres a second, or PET_SPEED_UNKNOWN. prints, heard and same are as
 * for pet_ride_scan(), which this calls: do not call both.
 *
 * A known speed decides. Two scans in a row at RIDE_SPEED_MPS or more are a
 * ride, and it lasts through RIDE_SLOW_MAX slower ones: lights, a queue, a
 * station. Below that, WALK_SPEED_MPS or the sensor makes it carried. When
 * the speed is not known (indoors, a tunnel, no GPS at all) it is the
 * networks going by that tell a ride, as before.
 *
 * Returns 0 = cannot say, 1 = still, 2 = carried, 3 = riding.
 */
#define PET_SPEED_UNKNOWN (-1.0f)
#define RIDE_SPEED_MPS    6.0f  /* 22 km/h: faster than a run is kept up, slower than a town street is driven */
#define WALK_SPEED_MPS    0.5f  /* below this a position that wanders is as likely as a walk */
#define RIDE_SLOW_MAX     4

uint8_t pet_travel_scan(pet_ride_t *r, uint8_t sensor, float speed_mps, const uint16_t *prints, int heard,
                        int *same);

#ifdef __cplusplus
}
#endif
