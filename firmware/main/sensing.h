/*
 * Sensing: Wi-Fi scans and Bluetooth LE scans turned into food events.
 *
 * The Wi-Fi scan is passive (no probe requests). The Bluetooth scan is
 * active: it sends the standard one-packet scan request so devices reply
 * with their name, because the day 1 log showed a passive scan could not
 * tell speakers, earbuds or cars from crumbs. It uses a new random address
 * for every scan. Nothing here ever connects or pairs.
 *
 * Addresses of what is heard are used only to merge packets within one
 * scan, to build a salted in-memory hash for the seen list, and to make
 * the scrambled 16-bit prints that habitats are recognised by (see
 * pet_places.h); the addresses themselves are never logged, stored or
 * passed on.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "pet_food.h"
#include "pet_places.h"

typedef struct {
    food_type_t type;
    float value;
    int8_t rssi;
    /* Network or device name, for showing on screen while it is eaten. May be "". */
    char name[33];
    /* How it was classified (security mode, company id...). No address, no name. */
    char detail[48];
} food_event_t;

typedef struct {
    int wifi_seen;   /* networks heard this cycle */
    int wifi_best;   /* the strongest of them in dBm; 0 if none */
    int prints;      /* fixed networks strong enough to say where the pet is */
    int matches;     /* place names among them */
    int wifi_new;    /* of those, eaten (not on the seen list) */
    int ble_seen;
    int ble_new;
} sensing_cycle_t;

typedef struct {
    /* Called once per newly eaten food. May run on the Bluetooth task. */
    void (*on_food)(const food_event_t *ev);
    /*
     * Called when a network that was already eaten is heard much more
     * strongly than before: only type, rssi and name are filled in.
     */
    void (*on_heard)(const food_event_t *ev);
    /* Called at the end of each scan cycle, on the sensing task. */
    void (*on_cycle)(const sensing_cycle_t *cycle);
    /*
     * Called after on_cycle when the Wi-Fi scan worked: the surroundings as
     * prints and kinds of place, for habitats and place badges.
     */
    void (*on_scan)(const pet_scan_t *scan);
    /* Scrambles the prints. Random, made once per device and kept. */
    uint32_t place_salt;
    /*
     * Asked every few seconds: how long from one scan start to the next
     * (0 = do not scan for now), and whether to switch the Wi-Fi radio off
     * between scans. Lets the power plan change the rhythm as it goes.
     */
    uint32_t (*scan_period_s)(bool *radio_off_between);
} sensing_callbacks_t;

/* Starts the radios and the scan loop. Calls are never made concurrently. */
esp_err_t sensing_start(const sensing_callbacks_t *cb);
