/* The critter itself: placeholder hatchling body with eyes drawn by code. */
#pragma once

#include "lvgl.h"
#include "pet_app.h"
#include "pet_brain.h"

/*
 * The side of the square the pet is drawn for, in pixels. 0 or
 * PET_AREA_SIZE is the size it was designed at; any other size is drawn
 * smooth at that size, not enlarged. Call before pet_view_create().
 */
void pet_view_set_size(int32_t pixels);

/* Creates the pet inside parent (expected to be a square of that size). */
void pet_view_create(lv_obj_t *parent);

/*
 * Which body to draw: the egg, the hatchling, or a juvenile or adult form.
 * underfed adds the scrawny flicker. Redraws only when something changed.
 */
void pet_view_set_look(pet_stage_t stage, pet_form_t form, bool underfed);

/* Changes the eye expression to match the mood. */
void pet_view_set_mood(pet_mood_t mood);

/* Starts the animation for what the pet is doing now. */
void pet_view_set_action(pet_action_t action);

/* Chews: the mouth opens and shuts a few times. For eating moments. */
void pet_view_munch(void);

/* Stops the pet's own timers while the screen is dark, so nothing is animated unseen. */
void pet_view_set_paused(bool paused);

/* Happy squint and a little hop: used for taps and for eating moments. */
void pet_view_hop(void);

/*
 * A farewell: the pet hops off the side of its area over ms milliseconds.
 * It stays gone until the look changes, which draws the next one.
 */
void pet_view_walk_off(uint32_t ms);

/* An arrival: whatever is drawn now comes in from the other side and settles. */
void pet_view_roll_in(uint32_t ms);

/* Called when the pet is tapped (the hop has its own short cooldown). */
void pet_view_set_tap_cb(void (*cb)(void));

/* Called when the pet is pressed and held. */
void pet_view_set_long_cb(void (*cb)(void));
