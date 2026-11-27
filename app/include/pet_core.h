/*
 * Pet core: stomach, mood, eating rules and cravings.
 *
 * Pure logic. No hardware, no LVGL, no clock of its own: the caller passes
 * the time in, so the same code runs on the boards, in desktop tests and
 * in the log replayer at any speed.
 *
 * Time is "local seconds": seconds since any fixed point, arranged so that
 * a multiple of 86400 falls on local midnight (the pet's day starts there).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet_app.h"   /* pet_mood_t */
#include "pet_food.h"
#include "pet_places.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Numbers in play. All of these will move to a file on the card. */
typedef struct {
    float stomach_max;          /* 20 */
    float drain_per_hour;       /* 1 */
    float start_stomach;        /* where a new pet begins */
    /* Each repeat of a food type in a day is worth this much of the last: 1, 1/2, 1/4... */
    float repeat_factor;        /* 0.5 */
    uint32_t meal_gap_s;        /* least time between eating moments */
    int junk_limit;             /* open networks per day before the pet is queasy */
    uint32_t queasy_s;          /* how long queasy lasts */
    float mood_per_new_type;    /* variety bonus: first of each type each day */
    float mood_per_value;       /* mood gained per point of food */
    float mood_drift_per_hour;  /* how fast mood settles toward its resting level */
    float craving_value_factor; /* a craved food is worth this much more */
    float craving_mood;         /* mood boost for satisfying a craving */
    uint32_t craving_every_s;   /* wait between cravings */
    uint32_t craving_lasts_s;
    float hungry_below;         /* stomach level that reads as hungry */
    float empty_below;          /* "near empty" for the Static rule */
    uint32_t static_after_s;    /* near empty this long = Static (2 days) */
    float static_recover_at;    /* stomach level that ends Static */
    int night_start_hour;       /* quiet hours: the pet is sleepy */
    int night_end_hour;

    /* Growth. A stage ends only when BOTH its days and its meals are done. */
    float growth_speed;         /* 1 = real life (about 12 weeks); 2016 = a life in an hour */
    int egg_meals;              /* an egg hatches on meals alone */
    float hatchling_days;
    int hatchling_meals;
    float juvenile_days;
    int juvenile_meals;
    float adult_days;
    int adult_meals;
    /* Evolution */
    /* Roamer: less than this share of its plain-network food came from any one habitat. */
    float roamer_max_home_share;
    float roamer_finds_per_day; /* stand-in until it knows a habitat: plain networks found per day */
    float new_place_mood;       /* mood boost for settling somewhere new */
    float visit_mood;           /* ...and for arriving somewhere that is not home */
    float prism_max_share;      /* Prism: no food group above this share of the diet */
    int prism_min_types;        /* ...and at least this many food types tasted */

    /*
     * Personality: how each trait bends what the pet feels like doing. Each
     * is what the pull toward that action is multiplied by, so 1 leaves it
     * as it was, 2 doubles it and 0.5 halves it.
     */
    float greedy_grumble_at;    /* a greedy pet grumbles once it is below this share of a full stomach (others: 0.2) */
    float lazy_nap, lazy_idle, lazy_play;
    float greedy_grumble, greedy_sniff;
    float curious_look, curious_sniff;
    float playful_play;
    float shy_play_less;        /* how much of its play a shy pet gives up where it is busiest, 0 to 1 */
    float shy_idle_more;        /* how much more it stands still there: 1 = twice as much */
    float cheerful_play, cheerful_grumble;

    /* How many times over the owner's pick of favourite food counts toward the form. */
    float favourite_weight;
} pet_tuning_t;

pet_tuning_t pet_tuning_default(void);

/* One eating moment: everything the pet gathered since the last one. */
typedef struct {
    int foods;                  /* how many were eaten in this batch */
    float value;                /* what they were worth after the eating rules */
    float wasted;               /* part of value that did not fit in the stomach */
    food_type_t best;           /* the most valuable food in the batch, for the screen */
    float best_value;           /* its table value: how rare a find this was */
    int new_types_today;        /* variety: types eaten for the first time today */
    int new_foods_ever;         /* types never tasted before in this pet's life */
    bool craving_satisfied;
    bool became_queasy;
} pet_meal_t;

#define PET_NO_CRAVING FOOD_TYPE_COUNT

/*
 * Personality. Every egg gets two at random; they shift how the pet
 * chooses what to do (see pet_brain.h), so no two pets act alike.
 */
typedef enum {
    TRAIT_LAZY,
    TRAIT_GREEDY,
    TRAIT_CURIOUS,
    TRAIT_PLAYFUL,
    TRAIT_SHY,
    TRAIT_CHEERFUL,
    TRAIT_COUNT,
} pet_trait_t;

#define PET_TRAITS_PER_PET 2

/* Life stages, in order. */
typedef enum {
    STAGE_EGG,
    STAGE_HATCHLING,    /* the same for every pet */
    STAGE_JUVENILE,     /* Grazer or Hunter */
    STAGE_ADULT,        /* one of the adult forms */
    STAGE_COUNT,
} pet_stage_t;

/* What the pet's diet has made it. */
typedef enum {
    FORM_NONE,          /* egg and hatchling */
    FORM_GRAZER,        /* juvenile: mostly Wi-Fi */
    FORM_HUNTER,        /* juvenile: mostly Bluetooth */
    FORM_HOMEBODY,      /* Grazer adult: the same few networks every day */
    FORM_ROAMER,        /* Grazer adult: many different networks */
    FORM_SCAVENGER,     /* Grazer adult: open networks and hotspots */
    FORM_SINGER,        /* Hunter adult: speakers and earbuds */
    FORM_RACER,         /* Hunter adult: cars */
    FORM_GLOWBUG,       /* Hunter adult: phones and wearables */
    FORM_PRISM,         /* special: balanced across everything */
    FORM_COUNT,
} pet_form_t;

/* The food groups that evolution weighs against each other. */
typedef enum {
    GROUP_NETWORKS,     /* secured, WPA3, hidden, printers */
    GROUP_SCAVENGE,     /* open networks, hotspots */
    GROUP_MUSIC,        /* speakers, earbuds */
    GROUP_CARS,         /* cars, over Bluetooth or by their Wi-Fi */
    GROUP_GADGETS,      /* phones, watches */
    GROUP_OTHER,        /* crumbs, trackers, TVs: food, but no form of their own */
    GROUP_COUNT,
} pet_group_t;

/*
 * One day in brief: what the diet chart and the journal are drawn from.
 * Numbers only, like everything else that is kept.
 */
typedef struct {
    uint32_t day;                       /* day number (local seconds / 86400); 0 = empty */
    uint16_t foods;                     /* things eaten */
    uint16_t meals;                     /* eating moments */
    uint16_t group_pts[GROUP_COUNT];    /* points per food group, times 10 */
    uint8_t best;                       /* food_type_t of the best find; FOOD_TYPE_COUNT if none */
    uint8_t types;                      /* different food types eaten */
    uint8_t new_foods;                  /* types tasted for the first time ever */
    uint8_t flags;                      /* DAY_* */
} pet_day_t;

#define DAY_HUNGRY  0x01    /* went hungry at some point */
#define DAY_QUEASY  0x02    /* too much junk food */
#define DAY_CRAVING 0x04    /* a craving was satisfied */
#define DAY_GREW    0x08    /* hatched, grew or took its adult form */
#define DAY_NEW_PLACE 0x10  /* settled somewhere it had never been */
#define DAY_OUT     0x20    /* spent time somewhere that is not home */
#define DAY_BADGE_PLACE 0x40 /* earned a place badge */
#define DAY_RODE    0x80    /* rode in a car, a bus or a train */

#define PET_HISTORY_DAYS 7  /* finished days kept, newest first */
#define PET_SECRET_BYTES 16
#define PET_NAME_MAX     13 /* with the ending zero */

/* Badges. Display only. The order is part of the save format: add new ones at the end. */
typedef enum {
    BADGE_FIRST_BITE,
    BADGE_HATCHED,
    BADGE_GROWN_UP,
    BADGE_FIRST_CAR,
    BADGE_LUCKY_FIND,       /* a hidden network worth 5 */
    BADGE_RARE_3,           /* three rare foods in one day */
    BADGE_FEAST,            /* 50 foods in one day */
    BADGE_BALANCED_DAY,     /* five of the six food groups in one day */
    BADGE_WELL_FED_WEEK,    /* seven days running without going hungry */
    BADGE_PHONES_100,
    BADGE_MEALS_100,
    BADGE_MEALS_1000,
    BADGE_ALL_WIFI,
    BADGE_ALL_BLUETOOTH,
    BADGE_FULL_DEX,
    BADGE_RETIRED,          /* a pet raised to retirement */
    BADGE_PRISM,            /* a pet grown into a Prism */
    BADGE_ALL_FORMS,        /* each of the six ordinary adult forms retired */
    BADGE_GEN_5,            /* the fifth generation */
    BADGE_COUNT,
} pet_badge_t;

/*
 * The farm: where pets go when they retire. It belongs to the owner, not
 * to one pet, so it is carried from each pet to the egg it leaves.
 */
#define FARM_MAX 12

typedef struct {
    char name[PET_NAME_MAX];    /* "" if it was never named */
    uint8_t form;               /* pet_form_t it retired as */
    uint8_t traits;
    uint8_t favourite;          /* pet_group_t it ate most of; GROUP_COUNT if it never ate */
    uint16_t generation;
    uint16_t days;              /* how long it lived, in pet days */
    uint32_t retired_day;       /* the day it retired (days since 1970) */
    uint32_t meals;
    uint32_t id;                /* the id it had as a pet: tells two residents apart when farms are joined */
} pet_resident_t;

typedef struct {
    pet_resident_t resident[FARM_MAX];  /* oldest first */
    uint8_t count;
    uint16_t moved_on;          /* retired before these, and no longer shown */
    uint16_t forms;             /* bit per pet_form_t that has ever retired */
} pet_farm_t;

typedef struct {
    pet_tuning_t tune;
    uint32_t now;
    uint32_t rng;

    uint32_t born;              /* when this egg was laid */
    uint8_t traits;             /* bit per pet_trait_t */
    /* How busy the surroundings are: foods found lately, fading over ~10 minutes. */
    float activity;

    float stomach;              /* 0..stomach_max */
    float mood;                 /* 0..100 */

    /* Today */
    uint32_t day;
    uint16_t eaten_today[FOOD_TYPE_COUNT];
    int junk_today;
    uint32_t queasy_until;

    /* Lifetime (feeds evolution in build stage 4) */
    float points[FOOD_TYPE_COUNT];
    uint32_t tasted[FOOD_TYPE_COUNT];
    uint32_t meals;             /* eating moments */

    /* Batch waiting for the next eating moment */
    pet_meal_t pending;
    uint32_t last_meal;

    /* Craving */
    food_type_t craving;        /* PET_NO_CRAVING when there is none */
    uint32_t craving_until;
    uint32_t next_craving;

    /* Static (underfed) */
    uint32_t empty_since;       /* 0 when not near empty */
    bool is_static;

    /* Growth */
    pet_stage_t stage;
    pet_form_t form;
    uint32_t stage_started;
    uint32_t stage_meal_base;   /* value of `meals` when this stage began */
    bool retire_ready;          /* adult life complete (retirement is a later build stage) */
    bool evolved;               /* stage or form just changed; see pet_take_evolved() */

    /* Days: the one in progress and the last week, for the diet chart and journal. */
    pet_day_t today;
    float today_best_value;     /* table value of today's best find */
    uint8_t rare_today;         /* rare foods eaten today */
    pet_day_t history[PET_HISTORY_DAYS];

    char name[PET_NAME_MAX];    /* given by the owner; "" until named */
    uint32_t badges;            /* bit per pet_badge_t */
    uint32_t new_badges;        /* earned since pet_take_new_badges() was last called */

    /* Habitats and place badges. */
    pet_places_t places;

    /* The line it comes from: which generation this is (1 = the first egg), and the farm. */
    uint16_t generation;
    pet_farm_t farm;

    /*
     * The food the owner has said is its favourite: 0 for none, otherwise a
     * pet_group_t plus one (never GROUP_OTHER). Food of that group counts
     * for more when the pet's form is decided, and for nothing else, so an
     * owner in a place that is nearly all Wi-Fi or all Bluetooth can still
     * steer what it grows into.
     */
    uint8_t favourite;

    /*
     * Pastures. A pet lives on one device at a time and can be moved to
     * another (see "Moving between devices" below). `away` is a device with
     * no pet on it: an empty pasture. It still learns places and earns
     * place badges for the owner, and eats nothing.
     */
    bool away;
    uint32_t id;                /* this pet's own number, given when its egg is laid; never 0 */
    /*
     * Known only to the devices this pet has lived on: it travels inside
     * the parcel and is never said aloud. A device proves with it that it
     * holds this pet (pet_link.h). All zero = none, and nothing is proved.
     * pet_init() makes one from its seed; a host with a real source of
     * random numbers should put a better one in its place for each new egg.
     */
    uint8_t secret[PET_SECRET_BYTES];
    uint16_t hops;              /* how many times it has moved to another device */
    uint32_t left_id;           /* the pet that last left this device (0 = none), and the */
    uint16_t left_hops;         /* hops it left with, to know it if it comes back */
    /*
     * How far this pet has travelled, in metres, where a platform can tell:
     * while riding, and otherwise (on foot, mostly). Numbers only; nothing
     * about where. They go with the pet when it moves.
     */
    uint32_t rode_m, walked_m;
} pet_t;

/* More than this in one scan is a mistake, not a journey: an airliner for a quarter of an hour is 225 km. */
#define PET_MOVED_MAX_M 300000.0f

/*
 * What a device that holds the pet answers to a question (nonce) about it:
 * a keyed hash of the question with the pet's hops and meals. 0 if the pet
 * has no secret.
 */
uint64_t pet_proof(const pet_t *pet, uint64_t nonce, uint32_t hops, uint32_t meals);

#define PET_FAVOURITE_MAX GROUP_OTHER   /* the largest value pet_t.favourite takes */

void pet_init(pet_t *pet, const pet_tuning_t *tune, uint32_t now, uint32_t seed);

/* True once an adult has lived its full life: it may retire whenever the owner chooses. */
bool pet_can_retire(const pet_t *pet);

/*
 * Retires the pet to the farm and puts the egg it leaves in its place.
 * The egg is the next generation; it takes one of its parent's traits and
 * draws the other afresh. The farm, the badges, and the habitats and place
 * badges stay; everything else starts over. If the farm is full the oldest
 * resident moves on. Returns false, changing nothing, if the pet cannot
 * retire yet.
 */
bool pet_retire(pet_t *pet, uint32_t seed);

/*
 * A food was found. It joins the batch for the next eating moment.
 * base_value is the food's table value (food_value()). Returns what it is
 * worth after diminishing returns and any craving bonus.
 */
float pet_feed(pet_t *pet, food_type_t type, float base_value);

/*
 * Moves time forward: drains the stomach, rolls the day over, settles the
 * mood, picks cravings. Call it at least once a minute. Returns true when
 * an eating moment happened, with its details in *meal.
 */
bool pet_tick(pet_t *pet, uint32_t now, pet_meal_t *meal);

/*
 * What one scan heard around the pet (see pet_places.h). Returns
 * PLACE_EV_* bits for what changed; *badge_kind goes with PLACE_EV_BADGE.
 */
uint32_t pet_scan(pet_t *pet, const pet_scan_t *scan, place_kind_t *badge_kind);

/* A tap on the pet: small mood boost. The caller applies the cooldown. */
void pet_pat(pet_t *pet);

pet_mood_t pet_mood(const pet_t *pet);

/* "wants something musical", or "" when there is no craving. */
const char *pet_craving_text(const pet_t *pet);

bool pet_is_night(const pet_t *pet);
bool pet_is_queasy(const pet_t *pet);

static inline bool pet_has_trait(const pet_t *pet, pet_trait_t t)
{
    return (pet->traits >> t) & 1;
}
const char *pet_trait_name(pet_trait_t t);

/* ---- Growth and evolution ---------------------------------------------- */

const char *pet_stage_name(pet_stage_t stage);
const char *pet_form_name(pet_form_t form);

/* True once after each hatch, growth or change of form (clears the flag). */
bool pet_take_evolved(pet_t *pet);

/*
 * What the pet would become if it evolved right now: Grazer or Hunter for
 * an egg or hatchling, an adult form for a juvenile, its own form for an
 * adult. This is the "drifting toward" hint.
 */
pet_form_t pet_form_leaning(const pet_t *pet);

/* Points eaten per food group over the pet's life. */
void pet_group_points(const pet_t *pet, float points[GROUP_COUNT]);

/* How far through the current stage, 0..1 each. Both must reach 1 to move on. */
void pet_growth_progress(const pet_t *pet, float *time_done, float *meals_done);

/* Which group a food belongs to, and the group's short name ("music"). */
pet_group_t pet_food_group(food_type_t type);
const char *pet_group_name(pet_group_t group);

/*
 * Changes the speed of growth without ageing the pet: its age and its
 * progress through the current stage stay what they were, and only time
 * and meals from now on count at the new speed.
 */
void pet_set_growth_speed(pet_t *pet, float speed);

/* How many days and meals the current stage asks for, and how many are done. */
/*
 * Seconds by the clock until the pet has spent long enough in its stage,
 * at the speed it is living at. 0 once it has, or for an egg.
 */
uint32_t pet_growth_seconds_left(const pet_t *pet);

void pet_growth_counts(const pet_t *pet, int *days_done, int *days_needed,
                       int *meals_done, int *meals_needed);

/* ---- Badges ------------------------------------------------------------ */

const char *pet_badge_name(pet_badge_t badge);
/* How it is earned: "eat 100 phones". */
const char *pet_badge_how(pet_badge_t badge);

static inline bool pet_has_badge(const pet_t *pet, pet_badge_t b)
{
    return (pet->badges >> b) & 1;
}

/* Badges earned since the last call, as bits (clears them). */
uint32_t pet_take_new_badges(pet_t *pet);

/* ---- Journal ----------------------------------------------------------- */

/*
 * A short entry in the pet's voice for one day, built from that day's
 * numbers. The same day always reads the same. in_progress is for today.
 */
void pet_journal_entry(const pet_t *pet, const pet_day_t *day, bool in_progress,
                       char *out, size_t cap);

/*
 * The journal's sentences are in lists, one for each thing a day can be.
 * The last six follow pet_trait_t, in its order.
 */
typedef enum {
    JOURNAL_NOTHING_YET,    /* today so far, with nothing eaten */
    JOURNAL_NOTHING,
    JOURNAL_QUIET,
    JOURNAL_NORMAL,
    JOURNAL_BUSY,
    JOURNAL_GREW,
    JOURNAL_NEW_PLACE,
    JOURNAL_DROVE,
    JOURNAL_NEW_FOOD,       /* names a food */
    JOURNAL_BEST,           /* names a food */
    JOURNAL_WENT_OUT,
    JOURNAL_MOSTLY,         /* names a kind of food */
    JOURNAL_QUEASY,
    JOURNAL_HUNGRY,
    JOURNAL_CRAVING,
    JOURNAL_LAZY,           /* the closing line, by trait */
    JOURNAL_GREEDY,
    JOURNAL_CURIOUS,
    JOURNAL_PLAYFUL,
    JOURNAL_SHY,
    JOURNAL_CHEERFUL,
    JOURNAL_LIST_COUNT,
} pet_journal_list_t;

#define JOURNAL_BLANK        "{food}"   /* where a sentence that names a food says it */
#define JOURNAL_SENTENCE_MAX 60         /* letters in one sentence */

const char *pet_journal_list_key(pet_journal_list_t list);      /* "quiet": its name in a settings file */
const char *pet_journal_list_when(pet_journal_list_t list);     /* when it is used, in a few words */
bool pet_journal_list_names_food(pet_journal_list_t list);      /* its sentences hold JOURNAL_BLANK once */

/* The built-in sentences of a list. Returns how many. */
int pet_journal_builtin(pet_journal_list_t list, const char *const **lines);

/*
 * Replaces a list's sentences. They (and their text) must stay alive.
 * NULL or a count of 0 goes back to the built-in ones.
 */
void pet_journal_set(pet_journal_list_t list, const char *const *lines, int count);

/* ---- Saving ----------------------------------------------------------- */

/*
 * The save holds the pet's numbers only: stomach, mood, traits, per-food
 * counts and points, and for habitats their scrambled prints and whatever
 * the owner called them. No network names, no addresses. It is versioned and
 * checksummed, so a damaged or outdated save is refused rather than
 * loaded wrong.
 */
#define PET_SAVE_MAX 4096

/* Writes the pet into buf. Returns the number of bytes, or 0 if cap is too small. */
size_t pet_save(const pet_t *pet, uint8_t *buf, size_t cap);

/*
 * Restores a pet. Time is treated as paused while the device was off: the
 * pet wakes exactly as it was, however long that was, with its timers
 * carried forward to now. Only the per-day counters reset if the calendar
 * day has changed. If tune asks for a different growth speed than the pet
 * was saved at, it keeps its age (see pet_set_growth_speed()). Returns
 * false, leaving *pet untouched, if the data is not a valid save.
 */
bool pet_load(pet_t *pet, const pet_tuning_t *tune, const uint8_t *buf, size_t len, uint32_t now);

/* ---- Moving between devices ------------------------------------------- */

/*
 * A parcel is what travels from one device to another: the owner's
 * collection (the farm, the badges, the place badges and off the grid),
 * and with it, or not, the pet. Habitats never travel: their prints are
 * scrambled with a number of the device's own, and stay there.
 *
 * It is separate from the save, with a format number of its own, a marker
 * saying whether a release or a test build made it, and a checksum.
 */
#define PET_PARCEL_MAX 6144

typedef enum {
    PET_REALM_RELEASE,      /* the owner's real pets and farm */
    PET_REALM_TEST,         /* a test build or the simulator: never trades with the real ones */
} pet_realm_t;

typedef enum {
    PET_IMPORT_OK,          /* the collections are joined, and the pet has arrived if one was sent */
    PET_IMPORT_DAMAGED,     /* not a parcel, or it did not arrive whole */
    PET_IMPORT_NEWER,       /* made by a newer build than this one: update this device first */
    PET_IMPORT_WRONG_REALM, /* a test parcel for a release build, or the other way about */
    PET_IMPORT_OCCUPIED,    /* a hatched pet lives here: move or retire it first */
    PET_IMPORT_SAME_PET,    /* the pet in the parcel is the one already living here */
} pet_import_t;

/*
 * Writes a parcel into buf: the collection, and the pet too if with_pet.
 * Returns the number of bytes; 0 if cap is too small, or if the pet was
 * asked for and the pasture is empty. Nothing changes: the pet is still
 * here until pet_leave() says it has gone, so a sender can keep it until
 * the other side has it whole.
 */
size_t pet_export(const pet_t *pet, pet_realm_t realm, bool with_pet, uint8_t *buf, size_t cap);

/* The pet has gone to another device: this pasture is empty. Nothing happens if it already is. */
void pet_leave(pet_t *pet);

/* Starts a first-generation egg in an empty pasture. False, changing nothing, if a pet lives here. */
bool pet_new_egg(pet_t *pet, uint32_t seed);

/*
 * Takes a parcel in. The collection is joined with this device's: the
 * same parcel any number of times, or two devices' in either order, give
 * the same farm. A pet in the parcel moves in if the pasture is empty or
 * holds an unhatched egg, which gives way. Time stands still for the pet
 * in the crossing, as it does while a device is off, so the two clocks
 * need not agree. Anything but PET_IMPORT_OK changes nothing at all.
 * *pet_arrived (may be NULL) says whether a pet moved in.
 */
pet_import_t pet_import(pet_t *pet, pet_realm_t realm, const uint8_t *buf, size_t len,
                        uint32_t now, bool *pet_arrived);

#ifdef __cplusplus
}
#endif
