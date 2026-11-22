/* Motion: see pet_motion.h. */
#include "pet_motion.h"

#include <string.h>

#define JOLT_MG         120     /* change between two readings that counts as a jolt */
#define JOLTS           3       /* this many... */
#define JOLT_WINDOW_MS  5000    /* ...within this long means it is being carried, not bumped */
#define STEADY_MG       130     /* change small enough to call steady, even in a hand */
#define STEADY_SAMPLES  3
#define FACING_MG       500     /* screen within about 60 degrees of straight up */
#define AWAY_MG         300     /* screen clearly turned aside or down */
#define UPRIGHT_MG      (-250)  /* screen upright or better: anything but tipped face down */
#define TURN_COS        0.82f   /* about 35 degrees between two resting angles */
#define STIR_SAMPLES    4       /* this many unsteady readings is a movement, not a knock */
#define MOVING_FOR_MS   (2 * 60 * 1000)
#define STILL_AFTER_MS  (10 * 60 * 1000)

void pet_motion_init(pet_motion_t *m)
{
    memset(m, 0, sizeof(*m));
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

bool pet_motion_sample(pet_motion_t *m, int x, int y, int z, uint32_t ms)
{
    const int now[3] = {x, y, z};
    if (!m->primed) {
        for (int i = 0; i < 3; i++) {
            m->prev[i] = (int16_t)now[i];
        }
        m->primed = true;
        return false;
    }
    int change = iabs(now[0] - m->prev[0]) + iabs(now[1] - m->prev[1]) + iabs(now[2] - m->prev[2]);
    for (int i = 0; i < 3; i++) {
        m->prev[i] = (int16_t)now[i];
    }

    /* Carried: several jolts close together. One bump of the desk is not that. */
    if (change > JOLT_MG) {
        m->jolt_ms[0] = m->jolt_ms[1];
        m->jolt_ms[1] = m->jolt_ms[2];
        m->jolt_ms[2] = ms | 1;
        if (m->jolt_ms[0] && ms - m->jolt_ms[0] <= JOLT_WINDOW_MS) {
            m->carried_ms = ms;
            m->carried_ever = true;
            m->moved = true;
        }
    }

    /*
     * Raised: come to rest with the screen facing up, after either having
     * faced away or a real movement (lifted off a desk, an arm brought up).
     * A knock on the table is too short to count, and holding it there
     * does not count twice.
     *
     * Stood upright counts too, but only when that is a new angle. Hanging
     * upright from a bag it comes to rest at the same angle at every pause
     * in a walk, and that must not light the screen each time.
     */
    if (z < AWAY_MG) {
        m->away = true;
    }
    if (change >= STEADY_MG) {
        m->steady = 0;
        if (m->stir < 255) {
            m->stir++;
        }
        return false;
    }
    if (m->steady < 255) {
        m->steady++;
    }
    if (m->steady != STEADY_SAMPLES) {
        return false;
    }
    bool turned = false;
    if (m->has_ref) {
        float dot = 0, a = 0, b = 0;
        for (int i = 0; i < 3; i++) {
            dot += (float)now[i] * m->ref[i];
            a += (float)now[i] * now[i];
            b += (float)m->ref[i] * m->ref[i];
        }
        /* dot / (|a||b|) < cos, without the square root */
        turned = dot <= 0 || dot * dot < TURN_COS * TURN_COS * a * b;
    }
    for (int i = 0; i < 3; i++) {
        m->ref[i] = (int16_t)now[i];
    }
    m->has_ref = true;
    const bool raised = z >= FACING_MG ? (m->away || m->stir >= STIR_SAMPLES)
                                       : (z >= UPRIGHT_MG && turned);
    m->rests++;
    m->rests_up += z >= FACING_MG;
    m->raises += raised;
    m->away = z < AWAY_MG;
    m->stir = 0;
    return raised;
}

pet_motion_state_t pet_motion_state(const pet_motion_t *m, uint32_t ms)
{
    if (!m->primed) {
        return PET_MOTION_UNKNOWN;
    }
    if (m->carried_ever && ms - m->carried_ms < MOVING_FOR_MS) {
        return PET_MOTION_MOVING;
    }
    /* Counted from switch-on when it has never been carried. */
    uint32_t quiet = m->carried_ever ? ms - m->carried_ms : ms;
    return quiet >= STILL_AFTER_MS ? PET_MOTION_STILL : PET_MOTION_UNKNOWN;
}

bool pet_motion_take_moved(pet_motion_t *m)
{
    bool moved = m->moved;
    m->moved = false;
    return moved;
}

/* ---- Riding ------------------------------------------------------------- */

/*
 * Shake and step rhythm cannot tell a ride. A car seat shook a jacket
 * pocket 33 to 64 with 13 to 40 false steps a minute, but a truck on
 * country roads shook a shirt pocket 46 to 134 with 32 to 116, which is a
 * walk by those numbers, and an evening sat by a fire read like the car.
 *
 * What a ride has that nothing else does is networks going by: each scan
 * hears ones it has never met and none of the ones before. On foot the
 * last scan's networks are still in earshot, and sitting still they are
 * all old friends. On the logged drives nearly every network heard was
 * new; in a shop, at home and by the fire almost none were.
 */
#define RIDE_PASSES     3   /* scans of networks going by before it counts as a ride */
#define RIDE_QUIET_MAX  5   /* scans in a row hearing nothing new or old that a ride lasts through */

bool pet_ride_scan(pet_ride_t *r, bool carried, int wifi_heard, int wifi_new)
{
    int known = wifi_heard - wifi_new;
    if (!carried || known > wifi_new) {
        /* Lying still, or mostly among networks it already knows: somewhere, not on the way. */
        r->run = 0;
        r->quiet = 0;
    } else if (wifi_new > 0) {
        if (r->run < RIDE_PASSES) {
            r->run++;
        }
        r->quiet = 0;
    } else if (r->quiet < RIDE_QUIET_MAX) {
        /* An empty stretch of road says nothing either way. */
        r->quiet++;
    } else {
        r->run = 0;
    }
    return r->run >= RIDE_PASSES;
}

/* ---- Gait --------------------------------------------------------------- */

#define STEP_UP_MG    70    /* the pull must rise this far above its average to start a step */
#define STEP_DOWN_MG  20    /* and fall back to within this of it to end one */

void pet_gait_init(pet_gait_t *g)
{
    *g = (pet_gait_t){0};
}

/* Whole-number square root, good enough for a few thousand. */
static int32_t root(int32_t v)
{
    int32_t r = 0;
    for (int32_t bit = 1 << 14; bit; bit >>= 1) {
        if ((r + bit) * (r + bit) <= v) {
            r += bit;
        }
    }
    return r;
}

void pet_gait_sample(pet_gait_t *g, int x, int y, int z, uint32_t ms)
{
    /* The strength of the pull, whichever way up the device is: 1000 at rest. */
    int32_t pull = root((int32_t)x * x + (int32_t)y * y + (int32_t)z * z);
    if (g->samples == 0) {
        g->first_ms = ms;
        if (g->average == 0) {
            g->average = pull;
            g->prev = pull;
        }
    }
    int32_t change = pull - g->prev;
    g->shake_sum += (uint32_t)(change < 0 ? -change : change);
    g->prev = pull;
    g->average += (pull - g->average) / 16;
    if (!g->high && pull > g->average + STEP_UP_MG) {
        g->high = true;
        if (g->steps < UINT16_MAX) {
            g->steps++;
        }
    } else if (g->high && pull < g->average + STEP_DOWN_MG) {
        g->high = false;
    }
    g->last_ms = ms;
    g->samples++;
}

void pet_gait_take(pet_gait_t *g, int *samples, int *shake, int *steps_per_min)
{
    uint32_t span = g->last_ms - g->first_ms;
    *samples = (int)g->samples;
    *shake = g->samples ? (int)(g->shake_sum / g->samples) : 0;
    *steps_per_min = span >= 1000 ? (int)((uint64_t)g->steps * 60000 / span) : 0;
    g->samples = 0;
    g->shake_sum = 0;
    g->steps = 0;
}
