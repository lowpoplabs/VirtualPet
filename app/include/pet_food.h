/*
 * Food: turning one sighted Wi-Fi network or Bluetooth LE device into a
 * food type and value. Pure logic with no hardware or LVGL dependency, so
 * it runs in desktop tests as well as on the boards.
 *
 * Anything matched by name is a guess. The name lists are in pet_food.c
 * and will move to editable files on the card in a later build stage.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* Wi-Fi */
    FOOD_WIFI_SECURED,
    FOOD_WIFI_WPA3,
    FOOD_WIFI_OPEN,      /* junk food */
    FOOD_WIFI_PRINTER,
    FOOD_WIFI_HOTSPOT,   /* phone hotspot */
    FOOD_WIFI_HIDDEN,
    FOOD_WIFI_CAR,
    /* Bluetooth LE */
    FOOD_BLE_UNKNOWN,    /* crumbs */
    FOOD_BLE_PHONE,
    FOOD_BLE_WATCH,
    FOOD_BLE_EARBUDS,
    FOOD_BLE_TRACKER,
    FOOD_BLE_TV,
    FOOD_BLE_SPEAKER,
    FOOD_BLE_CAR,
    FOOD_TYPE_COUNT,
} food_type_t;

#define FOOD_FIRST_BLE FOOD_BLE_UNKNOWN

typedef enum {
    WIFI_SEC_OPEN,
    WIFI_SEC_SECURED,   /* WEP, WPA, WPA2 and anything else that is not WPA3 */
    WIFI_SEC_WPA3,
} wifi_sec_t;

/* What a passive Wi-Fi scan tells us about one network. */
typedef struct {
    const char *ssid;   /* "" or NULL for a hidden network */
    wifi_sec_t sec;
} wifi_sighting_t;

/* What one Bluetooth LE advertisement tells us. Absent fields are zero. */
typedef struct {
    const char *name;            /* "" or NULL when not advertised */
    bool has_company;
    uint16_t company_id;         /* from manufacturer data */
    const uint8_t *mfg_payload;  /* manufacturer data after the company id */
    size_t mfg_payload_len;
    bool has_appearance;
    uint16_t appearance;
    const uint16_t *uuids16;     /* service UUIDs and service-data UUIDs */
    size_t num_uuids16;
} ble_sighting_t;

food_type_t food_classify_wifi(const wifi_sighting_t *s);
food_type_t food_classify_ble(const ble_sighting_t *s);

/* Short lower-case label, e.g. "speaker". */
const char *food_label(food_type_t type);

static inline bool food_is_ble(food_type_t type) { return type >= FOOD_FIRST_BLE; }

/*
 * Base value from the plan's tables. A hidden network is worth 1, or now
 * and then 5, so the caller passes a random number; other foods ignore it.
 */
float food_value(food_type_t type, uint32_t random);

/*
 * The table behind food_value(). It can be replaced once at start-up, from
 * a settings file (pet_config.h), before anything is eaten.
 */
typedef struct {
    float value[FOOD_TYPE_COUNT];
    float hidden_treat;         /* now and then a hidden network is worth this instead... */
    int hidden_treat_one_in;    /* ...one time in this many */
} food_values_t;

food_values_t food_values_default(void);
void food_values_set(const food_values_t *values);

/*
 * The name lists behind the classifiers: a network or device whose name
 * contains one of a food's pieces (whatever the capitals) is that food.
 * Eight foods have a list (car hotspot, hotspot, printer, car, earbuds,
 * speaker, watch, tv); for the rest these return NULL. Lists end with NULL.
 * food_names_set() replaces one, from a settings file (pet_config.h); the
 * list must stay alive, and NULL goes back to the built-in one.
 */
const char *const *food_names(food_type_t type);
const char *const *food_names_default(food_type_t type);
void food_names_set(food_type_t type, const char *const *list);

/* Reverse of food_label(). Returns FOOD_TYPE_COUNT for an unknown label. */
food_type_t food_from_label(const char *label);

/* ---- Seen list ------------------------------------------------------- */

/*
 * Remembers what was eaten recently so the same router is not eaten on
 * every scan. It holds only salted hashes, lives only in memory, and is
 * never written to the card: addresses cannot be recovered from it and it
 * is gone at power-off.
 */
#define SEEN_CAPACITY 2048   /* a day with a couple of drives is several hundred */

typedef struct {
    uint32_t salt;
    struct {
        uint32_t hash;
        uint32_t last_s;   /* 0 = empty slot */
        int8_t rssi;       /* strongest it has been heard since it was eaten; SEEN_NO_RSSI = not noted */
    } slot[SEEN_CAPACITY];
} seen_list_t;

/* salt should be random and different on every boot. */
void seen_init(seen_list_t *list, uint32_t salt);

/*
 * Returns true if this id was already eaten within the last regrow_s
 * seconds. Otherwise records it as eaten now and returns false.
 * now_s is any steadily increasing clock in seconds, starting above 0.
 */
bool seen_recently(seen_list_t *list, const uint8_t *id, size_t id_len,
                   uint32_t now_s, uint32_t regrow_s);

#define SEEN_NO_RSSI (-128)

/*
 * Notes how strongly an id on the list was just heard, and returns the
 * strongest it had been heard before (SEEN_NO_RSSI if never noted, or if it
 * is not on the list). For tuning: a network is logged when eaten, which is
 * usually at its faintest, and this tells when it has come much closer.
 */
int seen_note_rssi(seen_list_t *list, const uint8_t *id, size_t id_len, int rssi);

#ifdef __cplusplus
}
#endif
