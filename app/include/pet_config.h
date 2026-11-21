/*
 * Settings files: the numbers a contributor may want to change, read from
 * plain-text files on the memory card. Pure logic; the platform only
 * supplies each file's text.
 *
 * A file is lines of "name = number". Blank lines and anything after a #
 * are ignored. Every number is checked against a sensible range, and a file
 * with any mistake in it is ignored as a whole, so a typo cannot leave the
 * pet half configured: it runs on the built-in values and says which line
 * was wrong.
 *
 * Two more files hold lists of names instead of numbers: "name = a, b, c".
 * The same rules apply to them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "pet_core.h"
#include "pet_food.h"
#include "pet_places.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PET_CONFIG_TUNING_FILE "tuning.txt"
#define PET_CONFIG_FOODS_FILE  "foods.txt"
/* Room for any of the files, and for the built-in copy of one. */
#define PET_CONFIG_FOOD_NAMES_FILE  "food-names.txt"
#define PET_CONFIG_PLACE_NAMES_FILE "place-names.txt"
#define PET_CONFIG_TEXT_MAX    8192

typedef struct {
    int line;           /* 0 when the trouble is not on one line */
    char why[72];       /* empty when the file was fine */
} pet_config_error_t;

/*
 * Reads a file's text into *out. text may be NULL or empty (no file): that
 * gives the built-in values and is not an error. Returns false, with the
 * built-in values in *out and the reason in *err, when the file is wrong.
 */
bool pet_config_tuning(const char *text, pet_tuning_t *out, pet_config_error_t *err);

/* Whether a tuning or foods file gives this setting a value (a "name = ..." line). */
bool pet_config_gives(const char *text, const char *name);
bool pet_config_foods(const char *text, food_values_t *out, pet_config_error_t *err);

/*
 * The built-in values written out as a file, with a comment on each line:
 * the reference a contributor copies from. Returns the length, or 0 if it
 * did not fit.
 */
size_t pet_config_tuning_text(char *buf, size_t cap);
size_t pet_config_foods_text(char *buf, size_t cap);

/*
 * The name lists. Unlike the two above these put what they read straight
 * into use (food_names_set, place_names_set), and keep it in memory of
 * their own until the next call. text NULL or empty: the built-in lists.
 * A wrong file also leaves the built-in lists in use, and returns false.
 *
 * food-names.txt: "earbuds = AirPods, Buds, WF-". The names that make a
 * network or device that food. A food may have several lines. A food the
 * file leaves out keeps its built-in list.
 *
 * place-names.txt: "coffee = peets, peet's" is one chain and its spellings,
 * a line per chain; "coffee_word = cafe, espresso" are general words. A
 * kind of place the file leaves out keeps its built-in list; one it
 * mentions is replaced. A chain is remembered by its first spelling.
 *
 * In both, a name in "quotes" keeps the spaces at its ends.
 */
bool pet_config_food_names(const char *text, pet_config_error_t *err);
bool pet_config_place_names(const char *text, pet_config_error_t *err);
size_t pet_config_food_names_text(char *buf, size_t cap);
size_t pet_config_place_names_text(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
