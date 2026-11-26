/*
 * Hearing: networks and devices a platform heard, turned into food and
 * surroundings.
 *
 * The platform does the listening with whatever radios it has, and hands
 * over each network and device as a plain record. Here the classifiers
 * (pet_food.h) decide what food it is, the seen list keeps it from being
 * eaten twice in a day, and the surroundings are reduced to scrambled prints
 * and kinds of place (pet_places.h) for habitats, badges and rides.
 *
 * Addresses are used only to make a salted in-memory hash for the seen list
 * and the count of devices, and the scrambled 16-bit prints. They are never
 * kept, and neither are names.
 *
 * Nothing here locks: call it from one thread, or under one lock.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_food.h"
#include "pet_motion.h"
#include "pet_places.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One network heard. */
typedef struct {
    const uint8_t *addr;    /* its address, 6 bytes; not kept */
    int rssi;               /* dBm */
    wifi_sighting_t what;   /* its name (cleaned: see hearing_clean_name) and security */
} heard_network_t;

/*
 * One device heard, with everything it said in one scan put together (an
 * advertisement and its scan response are one device).
 */
typedef struct {
    const uint8_t *addr;    /* its address, 6 bytes; not kept */
    int rssi;
    ble_sighting_t what;
} heard_device_t;

/* Something to eat, or to log. */
typedef struct {
    food_type_t type;
    float value;
    int8_t rssi;
    /* Network or device name, for showing on screen while it is eaten. May be "". */
    char name[33];
    /*
     * How it was classified. No address, no name. For a device it is filled
     * in here (company id, appearance, services); for a network it is left
     * empty for the platform, which knows the channel and the like.
     */
    char detail[48];
} food_event_t;

/* The counts of one round of listening. */
typedef struct {
    int wifi_seen;   /* networks heard this cycle */
    int wifi_best;   /* the strongest of them in dBm; 0 if none */
    int prints;      /* fixed networks strong enough to say where the pet is */
    int matches;     /* place names among them */
    int wifi_new;    /* of those, eaten (not on the seen list) */
    /* For telling a ride: the fixed networks heard, nearest first, as scrambled prints. */
    uint16_t ride_print[RIDE_PRINTS];
    int ride_prints;
    int wifi_same;   /* of those, how many were in one of the last few scans; filled in by the listener */
    int ble_seen;    /* different devices heard */
    int ble_new;
} hearing_cycle_t;

/* Different devices counted in one cycle; past this they are still counted, perhaps twice. */
#define HEARING_DEVICES_MAX 256

/* About 26 KB, most of it the seen list. A platform short of memory keeps it where there is room. */
typedef struct {
    seen_list_t seen;
    uint32_t place_salt;
    uint32_t tally_salt;
    uint32_t (*random)(void);
    hearing_cycle_t cycle;
    pet_scan_t scan;
    uint32_t tally[HEARING_DEVICES_MAX];
} pet_hearing_t;

/*
 * place_salt scrambles the prints: random, made once per device and kept.
 * random gives a properly random number; it also salts the seen list, so
 * that differs on every start.
 */
void hearing_init(pet_hearing_t *h, uint32_t place_salt, uint32_t (*random)(void));

/* Starts a cycle: the counts and the surroundings begin again from nothing. */
void hearing_begin(pet_hearing_t *h);

typedef enum {
    HEARD_OLD,      /* eaten within the last day: nothing to do */
    HEARD_NEARER,   /* eaten already, and now much stronger than ever before: ev has type, rssi and name, for a log */
    HEARD_EATEN,    /* new: ev is a meal */
} heard_t;

/*
 * One network. Give a scan's networks strongest first, since the prints
 * kept are the first ones heard. now_s is any steadily increasing clock in
 * seconds, starting above 0. ev is always written.
 */
heard_t hearing_network(pet_hearing_t *h, const heard_network_t *n, uint32_t now_s, food_event_t *ev);

/* One device. True when it is a meal, in ev. The same device may be given many times. */
bool hearing_device(pet_hearing_t *h, const heard_device_t *d, uint32_t now_s, food_event_t *ev);

/*
 * Ends the cycle. Returns its counts and, in *scan, the surroundings for
 * pet_app_scan(); motion is left 0 for the platform. Both stay valid until
 * hearing_begin(). A platform whose devices arrive all the time, not in
 * scans, calls hearing_begin() straight after this.
 */
const hearing_cycle_t *hearing_end(pet_hearing_t *h, const pet_scan_t **scan);

/* Names come off the air: keeps printable ASCII and replaces the rest with '?'. */
void hearing_clean_name(char *dst, size_t dst_size, const uint8_t *src, size_t len);

#ifdef __cplusplus
}
#endif
