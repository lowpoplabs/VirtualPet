/*
 * Places: habitats, place badges and "off the grid".
 *
 * A habitat is a spot the pet has spent time in, recognised by the mix of
 * fixed Wi-Fi networks around it. What is kept of each network is a
 * "print": 16 bits of a hash salted with a number unique to this device.
 * A print cannot be turned back into an address or a name, and means
 * nothing on any other device.
 *
 * Place badges come from matching a network's name against name lists at
 * the moment it is heard: the ones built into pet_places.c, or the owner's
 * from a settings file (pet_config.h). Only the kind of place, a count and
 * a 16-bit number for each chain already counted are kept, never the name
 * that was heard.
 *
 * Pure logic, like the rest of the pet core: the caller passes time in.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HABITAT_MAX        12
#define HABITAT_PRINTS     10
#define HABITAT_NAME_MAX   13  /* with the ending zero */
#define SCAN_PRINTS_MAX    12
#define SCAN_MATCHES_MAX   4
#define PLACE_CHAINS_MAX   32  /* chains remembered for each kind of place */
#define PLACE_WAIT_MAX     6   /* faint place names it is waiting to hear again */
/*
 * A place name heard only faintly counts once it has been heard again this
 * long after the first time: staying there, not passing by. Carried in a
 * pocket or a bag the device hears even the shop it is standing in faintly.
 */
#define PLACE_STAY_S       150
#define PLACE_WAIT_FORGET_S 900

typedef enum {
    PLACE_COFFEE,
    PLACE_HOTEL,
    PLACE_STORE,
    PLACE_AIRPORT,
    PLACE_INFLIGHT,
    PLACE_TRAIN,
    PLACE_LIBRARY,
    PLACE_UNIVERSITY,
    PLACE_HOSPITAL,
    PLACE_MUSEUM,
    PLACE_PARK,
    PLACE_RESTAURANT,
    PLACE_SAFETY,       /* fire, police and rescue stations */
    PLACE_KIND_COUNT,   /* the order is part of the save format: add new ones at the end */
} place_kind_t;

/* Bronze, silver and gold at these counts; 0 = not earned. */
#define PLACE_TIER_MAX 3
int place_tier(int count);
const char *place_tier_name(int tier);          /* "", "bronze", "silver", "gold" */
const char *place_kind_name(place_kind_t kind); /* "Coffee shops" */
const char *place_kind_how(place_kind_t kind);  /* "visit a coffee shop" */
const char *place_kind_key(place_kind_t kind);  /* "coffee": its name in a settings file */

/*
 * How long with no signal at all earns each "off the grid" tier. With a
 * motion sensor the device must also have been carried for at least half
 * of that time: out walking, not shut in a drawer.
 */
#define OFFGRID_BRONZE_S (1 * 3600)
#define OFFGRID_SILVER_S (3 * 3600)
#define OFFGRID_GOLD_S   (8 * 3600)

/*
 * One entry of the name list: a lower-case piece of a network name. chain
 * is the chain's number, shared by all of its spellings, or 0 for a general
 * word such as "library", which cannot tell one place from the next.
 */
typedef struct {
    uint8_t kind;       /* place_kind_t */
    uint16_t chain;
    const char *piece;
} place_name_t;

/*
 * A chain's number: 16 bits made from its first spelling, never 0. Because
 * it comes from the spelling and not from the chain's position in a list,
 * lists can be edited without disturbing the chains already counted.
 */
uint16_t place_chain_id(const char *spelling);

/* The built-in list. Returns how many entries. */
int place_names_builtin(const place_name_t **list);

/*
 * Replaces the list in use. The entries (and their text) must stay alive.
 * NULL goes back to the built-in list.
 */
void place_names_set(const place_name_t *list, int count);

/*
 * Saves made before chains had numbers kept a bit per chain, by its
 * position in the built-in list of the day. This gives the number of the
 * chain that had that bit, or 0.
 */
uint16_t place_legacy_chain(place_kind_t kind, int bit);

/*
 * Does this network name belong to a kind of place? *chain is the chain's
 * number (so different chains can be told apart without keeping the name),
 * or 0 for a general word.
 */
bool place_match_name(const char *ssid, place_kind_t *kind, uint16_t *chain);

/* The print of one network: id is its address, which is not kept. Never 0. */
uint16_t place_print(uint32_t salt, const uint8_t *id, size_t len);

/* What one scan heard, already reduced to what may be kept. */
typedef struct {
    uint16_t print[SCAN_PRINTS_MAX];    /* fixed networks, strongest first */
    uint8_t prints;
    struct {
        uint8_t kind;                   /* place_kind_t */
        uint16_t chain;                 /* 0 = a general word */
        uint8_t faint;                  /* 0 = heard strongly: counts at once. 1 = must be heard again later */
    } match[SCAN_MATCHES_MAX];          /* place names heard */
    uint8_t matches;
    uint16_t wifi_heard;                /* everything, for "off the grid" */
    uint16_t ble_heard;
    /* Motion since the last scan: 0 = no sensor, 1 = lay still, 2 = was carried, 3 = rode in a vehicle. */
    uint8_t motion;
    /*
     * How far it went since the last scan, in metres, for a platform that
     * can tell (GPS). 0 = nowhere, or not known: either way nothing is added.
     * Only the totals are kept (pet_t.rode_m, walked_m), never where.
     */
    float moved_m;
} pet_scan_t;

typedef struct {
    uint16_t print[HABITAT_PRINTS];
    uint8_t score[HABITAT_PRINTS];      /* how reliably each print shows up */
    uint8_t prints;                     /* 0 = unused slot */
    char name[HABITAT_NAME_MAX];        /* given by the owner; "" until named */
    uint16_t visits;
    uint32_t minutes;                   /* time spent here */
    uint32_t first_day;
    float net_points;                   /* plain-network food eaten here (Homebody or Roamer) */
} habitat_t;

typedef struct {
    habitat_t habitat[HABITAT_MAX];
    float away_points;                  /* plain-network food eaten in no known habitat */
    /*
     * Plain-network food a pet ate on devices it has since left: all of it,
     * and the most in any one habitat there. Habitats do not travel, and
     * this is what the Homebody or Roamer question needs of them.
     */
    float carried_total, carried_top;

    struct {
        uint8_t count;                  /* different chains, plus days with a general match */
        uint16_t chain[PLACE_CHAINS_MAX]; /* the chains already counted */
        uint8_t chains;
        uint32_t last_day;              /* last day a general match counted */
    } kind[PLACE_KIND_COUNT];
    uint8_t offgrid_tier;

    /* Not saved: where it is right now. */
    int8_t here;                        /* habitat index, or -1 */
    uint32_t here_last;                 /* last scan that matched `here` */
    uint16_t here_secs;                 /* seconds here not yet counted as a minute */
    uint8_t here_misses;                /* scans in a row that did not match `here` */
    uint8_t cand_misses;
    int8_t maybe;                       /* a known habitat seen on the last scan */
    uint8_t maybe_hits;
    uint16_t cand[HABITAT_PRINTS];      /* somewhere unknown it may be settling into */
    uint8_t cand_prints;
    uint32_t cand_since, cand_last;
    uint32_t last_scan;
    uint32_t quiet_since;               /* first scan of the current silence; 0 = not silent */
    uint16_t quiet_scans, quiet_carried; /* scans in that silence, and how many found it carried */
    uint8_t refresh;
    struct {
        uint8_t kind;
        uint16_t chain;
        uint32_t first, last;           /* when it was first and last heard */
    } wait[PLACE_WAIT_MAX];             /* faint place names heard lately */
    uint8_t waits;
} pet_places_t;

/* What a scan changed. */
#define PLACE_EV_ARRIVED 0x01   /* back somewhere it knows */
#define PLACE_EV_NEW     0x02   /* settled somewhere new: a habitat was made */
#define PLACE_EV_LEFT    0x04
#define PLACE_EV_BADGE   0x08   /* a place badge was earned or went up a tier */
#define PLACE_EV_OFFGRID 0x10   /* an off the grid tier was earned */

void places_init(pet_places_t *p);

/*
 * Takes one scan. now is in the pet's seconds, day its day number.
 * Returns PLACE_EV_* bits; *badge_kind is set with PLACE_EV_BADGE.
 */
uint32_t places_scan(pet_places_t *p, const pet_scan_t *scan, uint32_t now, uint32_t day,
                     place_kind_t *badge_kind);

/* Plain-network food was eaten: credit it to where the pet is. */
void places_fed(pet_places_t *p, float value);

/* The habitat it has spent the most time in, once that is a couple of hours; else -1. */
int places_home(const pet_places_t *p);

/* How many habitats are known. */
int places_count(const pet_places_t *p);

/*
 * Share (0..1) of plain-network food that came from the single habitat
 * that gave the most, or -1 while too little has been eaten to say.
 */
float places_top_share(const pet_places_t *p);

/* "Home", the owner's name for it, or "Place 3". */
void places_label(const pet_places_t *p, int index, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
