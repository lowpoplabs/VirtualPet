/*
 * Tuning log on the microSD card: /sdcard/foodlog.csv
 *
 * Always written: time, food type, value, signal strength, battery level.
 * Written only while the owner has the "Dev log" setting on: the network
 * or device name and how it was classified. The setting is off until
 * turned on and exists for tuning; a log made with it on is private.
 * Addresses are never written in either mode.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "sensing.h"

/* Call after the card is mounted. Fails cleanly when there is no card. */
esp_err_t foodlog_open(void);

void foodlog_food(const food_event_t *ev);
void foodlog_battery(int percent, bool usb);

/* Pushes everything written so far onto the card. */
/* A network already eaten, heard much more strongly than before (name only with the dev log on). */
void foodlog_heard(const food_event_t *ev);

/*
 * How the device moved since the last scan: "still", "carried" or "unknown",
 * the average shake in thousandths of g, steps per minute, and how many
 * readings that is from. With it, what the scan heard: networks, how many
 * were strong enough to mark the place, place names, and the strongest
 * signal.
 */
void foodlog_motion(const char *state, int shake, int steps_per_min, int samples,
                    const sensing_cycle_t *scan);

/* A change of place: "new", "arrive", "leave" or "badge", and which habitat (-1 = none). */
void foodlog_place(const char *what, int habitat);

/* How a spell on battery went: its length, and the share of it the chip slept. */
void foodlog_sleep(int minutes, int asleep_percent);

/* Why the chip started this time (see crashlog.h). */
void foodlog_start(const char *why);

void foodlog_flush(void);

/* The "Dev log" setting: whether names are written. Off until told otherwise. */
void foodlog_set_names(bool on);
bool foodlog_names(void);

/* Stops logging and lets go of the file, so the card can be unmounted. */
void foodlog_close(void);
