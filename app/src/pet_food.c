#include "pet_food.h"

#include <ctype.h>
#include <string.h>

/* ---- Name lists -------------------------------------------------------
 * Case-insensitive "contains" matches. These are starter guesses, to be
 * tuned from a week of real sightings; the owner can replace any of them
 * from a file on the card (pet_config.h).
 */

static const char *const WIFI_CAR_NAMES[] = {
    "SYNC_", "Uconnect", "MyChevrolet", "MyBuick", "MyGMC", "MyCadillac",
    "OnStar", "Tesla", "Toyota", "Honda", "HondaLink", "Subaru", "Audi_MMI",
    "BMW", "My VW", "VW_WLAN", "Jeep", "Chrysler", "FordPass", "Hyundai",
    "Kia", "Mazda", "Volvo", "Porsche", "Lexus", "Nissan", "CarPlay",
    "Motive Hotspot",   /* fleet truck gateway */
    NULL,
};
static const char *const WIFI_HOTSPOT_NAMES[] = {
    "iPhone", "Galaxy", "Pixel", "AndroidAP", "Android_", "OnePlus", "moto ",
    "Redmi", "Xiaomi", "HUAWEI P", "HUAWEI Mate", "'s phone", "hotspot",
    NULL,
};
static const char *const WIFI_PRINTER_NAMES[] = {
    /*
     * "DIRECT-" on its own is not here: TVs and streaming sticks use Wi-Fi
     * Direct too (day 1 log: a Roku was eaten as a printer 22 times).
     */
    "HP-Print", "HP-Setup", "-HP ", "EPSON", "Canon", "Brother", "printer",
    "OfficeJet", "LaserJet", "DeskJet", "ENVY",
    NULL,
};

static const char *const BLE_CAR_NAMES[] = {
    "SYNC", "Uconnect", "MyLink", "Tesla", "Toyota", "Honda", "Subaru",
    "BMW", "Audi", "VW ", "Jeep", "Ford", "Chevrolet", "Hyundai", "Kia",
    "Mazda", "Volvo", "Lexus", "Nissan", "CAR MULTIMEDIA", "CarPlay",
    "Handsfree", "Hands-Free",
    NULL,
};
static const char *const BLE_EARBUD_NAMES[] = {
    "AirPods", "Buds", "WF-", "WH-", "Beats", "Jabra", "earbud", "EarFun",
    "Tozo", "Powerbeats", "QC ", "QuietComfort", "LinkBuds", "Tune ", "Live ",
    "Endurance", "headphone", "headset",
    NULL,
};
static const char *const BLE_SPEAKER_NAMES[] = {
    "JBL", "Bose", "Sonos", "BOOM", "Soundcore", "SoundLink", "Marshall",
    "Flip", "Charge", "Xtreme", "speaker", "Echo", "HomePod", "soundbar",
    NULL,
};
static const char *const BLE_WATCH_NAMES[] = {
    "Watch", "Fitbit", "Garmin", "Band", "Amazfit", "WHOOP", "Forerunner",
    "fenix", "vivo", "Venu", "Charge ", "Versa", "Inspire", "Oura", "Polar",
    "Suunto", "COROS",
    NULL,
};
static const char *const BLE_TV_NAMES[] = {
    "TV", "Roku", "Chromecast", "BRAVIA", "[LG]", "webOS", "Shield", "Fire",
    "Apple TV", "Vizio", "TCL", "Hisense",
    NULL,
};

const char *const *food_names_default(food_type_t type)
{
    switch (type) {
    case FOOD_WIFI_CAR:     return WIFI_CAR_NAMES;
    case FOOD_WIFI_HOTSPOT: return WIFI_HOTSPOT_NAMES;
    case FOOD_WIFI_PRINTER: return WIFI_PRINTER_NAMES;
    case FOOD_BLE_CAR:      return BLE_CAR_NAMES;
    case FOOD_BLE_EARBUDS:  return BLE_EARBUD_NAMES;
    case FOOD_BLE_SPEAKER:  return BLE_SPEAKER_NAMES;
    case FOOD_BLE_WATCH:    return BLE_WATCH_NAMES;
    case FOOD_BLE_TV:       return BLE_TV_NAMES;
    default:                return NULL;
    }
}

static const char *const *s_names[FOOD_TYPE_COUNT];

const char *const *food_names(food_type_t type)
{
    if ((int)type < 0 || type >= FOOD_TYPE_COUNT) {
        return NULL;
    }
    return s_names[type] ? s_names[type] : food_names_default(type);
}

void food_names_set(food_type_t type, const char *const *list)
{
    if ((int)type >= 0 && type < FOOD_TYPE_COUNT && food_names_default(type)) {
        s_names[type] = list;
    }
}

/* Bluetooth company ids (manufacturer data). */
#define COMPANY_APPLE   0x004C
#define COMPANY_SAMSUNG 0x0075
#define COMPANY_GOOGLE  0x00E0
#define COMPANY_HARMAN  0x0057   /* JBL, Harman Kardon */
#define COMPANY_GARMIN  0x0087
#define COMPANY_BOSE    0x009E
#define COMPANY_SONOS   0x05A7

/*
 * Apple manufacturer-data message types (first payload byte). Apple's id
 * is on everything Apple makes plus third-party beacons, so only the types
 * that phones, tablets and laptops send count as "phone".
 */
#define APPLE_TYPE_AIRDROP  0x05
#define APPLE_TYPE_AIRPODS  0x07   /* proximity pairing: AirPods and Beats */
#define APPLE_TYPE_AIRPLAY  0x09   /* AirPlay target: Apple TV, HomePod */
#define APPLE_TYPE_HANDOFF  0x0C
#define APPLE_TYPE_ACTION   0x0F   /* nearby action */
#define APPLE_TYPE_NEARBY   0x10   /* nearby info */
#define APPLE_TYPE_FINDMY   0x12   /* offline finding: AirTag and Find My items */

/* 16-bit service UUIDs used by tracker tags. */
static const uint16_t TRACKER_UUIDS[] = {
    0xFEED, 0xFEEC,   /* Tile */
    0xFD5A,           /* Samsung SmartTag */
};

/* Other 16-bit service UUIDs that give the device away. */
#define UUID_SONOS         0xFE07
#define UUID_BOSE          0xFEBE
#define UUID_FAST_PAIR     0xFE2C   /* Google Fast Pair: mostly earbuds and headphones */
#define UUID_NEARBY_SHARE  0xFEF3   /* Android Quick Share */

static bool has_uuid(const ble_sighting_t *s, uint16_t uuid)
{
    for (size_t i = 0; i < s->num_uuids16; i++) {
        if (s->uuids16[i] == uuid) {
            return true;
        }
    }
    return false;
}

/* Appearance categories: the top 10 bits of the appearance value. */
#define APPEARANCE_CATEGORY(a) ((a) >> 6)
#define CAT_PHONE          0x01
#define CAT_WATCH          0x03
#define CAT_DISPLAY        0x05
#define CAT_MEDIA_PLAYER   0x0A
#define CAT_WEARABLE       0x11   /* sports bands and activity trackers */
#define CAT_AUDIO_SINK     0x21   /* speakers and soundbars */
#define CAT_WEARABLE_AUDIO 0x25   /* earbuds, headsets, headphones */

static bool contains_nocase(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == n) {
            return true;
        }
    }
    return false;
}

static bool name_in(const char *name, const char *const *list)
{
    if (!name || !*name) {
        return false;
    }
    for (; *list; list++) {
        if (contains_nocase(name, *list)) {
            return true;
        }
    }
    return false;
}

food_type_t food_classify_wifi(const wifi_sighting_t *s)
{
    if (!s->ssid || !*s->ssid) {
        return FOOD_WIFI_HIDDEN;
    }
    /* Named kinds first: a car hotspot is a car whatever its security. */
    if (name_in(s->ssid, food_names(FOOD_WIFI_CAR)))     return FOOD_WIFI_CAR;
    if (name_in(s->ssid, food_names(FOOD_WIFI_HOTSPOT))) return FOOD_WIFI_HOTSPOT;
    if (name_in(s->ssid, food_names(FOOD_WIFI_PRINTER))) return FOOD_WIFI_PRINTER;

    switch (s->sec) {
    case WIFI_SEC_OPEN: return FOOD_WIFI_OPEN;
    case WIFI_SEC_WPA3: return FOOD_WIFI_WPA3;
    default:            return FOOD_WIFI_SECURED;
    }
}

food_type_t food_classify_ble(const ble_sighting_t *s)
{
    /*
     * Order matters: most specific signal first. Apple's company id covers
     * phones, earbuds, laptops and tags alike, so "phone by company id" is
     * the last resort before crumbs.
     */
    for (size_t i = 0; i < s->num_uuids16; i++) {
        for (size_t t = 0; t < sizeof(TRACKER_UUIDS) / sizeof(TRACKER_UUIDS[0]); t++) {
            if (s->uuids16[i] == TRACKER_UUIDS[t]) {
                return FOOD_BLE_TRACKER;
            }
        }
    }
    bool apple = s->has_company && s->company_id == COMPANY_APPLE;
    uint8_t apple_type = (apple && s->mfg_payload_len > 0) ? s->mfg_payload[0] : 0;
    if (apple_type == APPLE_TYPE_FINDMY)  return FOOD_BLE_TRACKER;
    if (apple_type == APPLE_TYPE_AIRPODS) return FOOD_BLE_EARBUDS;
    if (apple_type == APPLE_TYPE_AIRPLAY) return FOOD_BLE_TV;

    if (s->has_appearance) {
        switch (APPEARANCE_CATEGORY(s->appearance)) {
        case CAT_WEARABLE_AUDIO: return FOOD_BLE_EARBUDS;
        case CAT_AUDIO_SINK:     return FOOD_BLE_SPEAKER;
        case CAT_WATCH:
        case CAT_WEARABLE:       return FOOD_BLE_WATCH;
        case CAT_DISPLAY:
        case CAT_MEDIA_PLAYER:   return FOOD_BLE_TV;
        default: break;
        }
    }

    /* Cars before speakers and earbuds: "Ford SYNC" must not lose to a brand overlap. */
    if (name_in(s->name, food_names(FOOD_BLE_CAR)))     return FOOD_BLE_CAR;
    if (name_in(s->name, food_names(FOOD_BLE_EARBUDS))) return FOOD_BLE_EARBUDS;
    if (name_in(s->name, food_names(FOOD_BLE_SPEAKER))) return FOOD_BLE_SPEAKER;
    if (name_in(s->name, food_names(FOOD_BLE_WATCH)))   return FOOD_BLE_WATCH;
    if (name_in(s->name, food_names(FOOD_BLE_TV)))      return FOOD_BLE_TV;

    /*
     * Passive scans rarely carry a name (day 1: 67 of 538), so brand ids
     * are the next best signal. These are coarser guesses than a name:
     * Bose makes earbuds as well as speakers.
     */
    if (has_uuid(s, UUID_SONOS) || has_uuid(s, UUID_BOSE)) return FOOD_BLE_SPEAKER;
    if (has_uuid(s, UUID_FAST_PAIR))                       return FOOD_BLE_EARBUDS;
    if (s->has_company) {
        switch (s->company_id) {
        case COMPANY_SONOS:
        case COMPANY_BOSE:
        case COMPANY_HARMAN: return FOOD_BLE_SPEAKER;
        case COMPANY_GARMIN: return FOOD_BLE_WATCH;
        default: break;
        }
    }

    if (s->has_appearance && APPEARANCE_CATEGORY(s->appearance) == CAT_PHONE) {
        return FOOD_BLE_PHONE;
    }
    if (has_uuid(s, UUID_NEARBY_SHARE)) {
        return FOOD_BLE_PHONE;
    }
    if (apple) {
        switch (apple_type) {
        case APPLE_TYPE_AIRDROP:
        case APPLE_TYPE_HANDOFF:
        case APPLE_TYPE_ACTION:
        case APPLE_TYPE_NEARBY: return FOOD_BLE_PHONE;
        default:                return FOOD_BLE_UNKNOWN;   /* beacons, AirPrint... */
        }
    }
    if (s->has_company && (s->company_id == COMPANY_SAMSUNG ||
                           s->company_id == COMPANY_GOOGLE)) {
        return FOOD_BLE_PHONE;
    }
    return FOOD_BLE_UNKNOWN;
}

const char *food_label(food_type_t type)
{
    switch (type) {
    case FOOD_WIFI_SECURED: return "network";
    case FOOD_WIFI_WPA3:    return "wpa3 network";
    case FOOD_WIFI_OPEN:    return "open network";
    case FOOD_WIFI_PRINTER: return "printer";
    case FOOD_WIFI_HOTSPOT: return "hotspot";
    case FOOD_WIFI_HIDDEN:  return "hidden network";
    case FOOD_WIFI_CAR:     return "car hotspot";
    case FOOD_BLE_UNKNOWN:  return "crumbs";
    case FOOD_BLE_PHONE:    return "phone";
    case FOOD_BLE_WATCH:    return "watch";
    case FOOD_BLE_EARBUDS:  return "earbuds";
    case FOOD_BLE_TRACKER:  return "tracker";
    case FOOD_BLE_TV:       return "tv";
    case FOOD_BLE_SPEAKER:  return "speaker";
    case FOOD_BLE_CAR:      return "car";
    default:                return "?";
    }
}

food_type_t food_from_label(const char *label)
{
    for (int i = 0; i < FOOD_TYPE_COUNT; i++) {
        if (strcmp(label, food_label((food_type_t)i)) == 0) {
            return (food_type_t)i;
        }
    }
    return FOOD_TYPE_COUNT;
}

food_values_t food_values_default(void)
{
    return (food_values_t){
        .value = {
            [FOOD_WIFI_SECURED] = 1,
            [FOOD_WIFI_WPA3] = 2,
            [FOOD_WIFI_OPEN] = 2,
            [FOOD_WIFI_PRINTER] = 2,
            [FOOD_WIFI_HOTSPOT] = 3,
            [FOOD_WIFI_HIDDEN] = 1,
            [FOOD_WIFI_CAR] = 5,
            [FOOD_BLE_UNKNOWN] = 0.5f,
            [FOOD_BLE_PHONE] = 1,
            [FOOD_BLE_WATCH] = 2,
            [FOOD_BLE_EARBUDS] = 2,
            [FOOD_BLE_TRACKER] = 2,
            [FOOD_BLE_TV] = 2,
            [FOOD_BLE_SPEAKER] = 3,
            [FOOD_BLE_CAR] = 5,
        },
        /* Day 1 log: hidden networks are half of all Wi-Fi, so most are plain; 1 in 20 is a treat. */
        .hidden_treat = 5,
        .hidden_treat_one_in = 20,
    };
}

static food_values_t s_values;
static bool s_values_set;

void food_values_set(const food_values_t *values)
{
    s_values = *values;
    s_values_set = true;
}

float food_value(food_type_t type, uint32_t random)
{
    if (!s_values_set) {
        s_values = food_values_default();
        s_values_set = true;
    }
    if ((int)type < 0 || type >= FOOD_TYPE_COUNT) {
        return 0;
    }
    if (type == FOOD_WIFI_HIDDEN && s_values.hidden_treat_one_in > 0 &&
        random % (uint32_t)s_values.hidden_treat_one_in == 0) {
        return s_values.hidden_treat;
    }
    return s_values.value[type];
}

/* ---- Seen list ------------------------------------------------------- */

void seen_init(seen_list_t *list, uint32_t salt)
{
    memset(list, 0, sizeof(*list));
    list->salt = salt;
}

/* FNV-1a, seeded with the per-boot salt. */
static uint32_t salted_hash(uint32_t salt, const uint8_t *id, size_t len)
{
    uint32_t h = 2166136261u ^ salt;
    for (size_t i = 0; i < len; i++) {
        h = (h ^ id[i]) * 16777619u;
    }
    return h;
}

bool seen_recently(seen_list_t *list, const uint8_t *id, size_t id_len,
                   uint32_t now_s, uint32_t regrow_s)
{
    uint32_t h = salted_hash(list->salt, id, id_len);
    size_t oldest = 0;

    for (size_t i = 0; i < SEEN_CAPACITY; i++) {
        if (list->slot[i].last_s != 0 && list->slot[i].hash == h) {
            if (now_s - list->slot[i].last_s < regrow_s) {
                return true;
            }
            list->slot[i].last_s = now_s;
            list->slot[i].rssi = SEEN_NO_RSSI;
            return false;
        }
        if (list->slot[i].last_s < list->slot[oldest].last_s) {
            oldest = i;
        }
    }
    /* New: take an empty slot, or push out whatever was eaten longest ago. */
    list->slot[oldest].hash = h;
    list->slot[oldest].last_s = now_s;
    list->slot[oldest].rssi = SEEN_NO_RSSI;
    return false;
}

int seen_note_rssi(seen_list_t *list, const uint8_t *id, size_t id_len, int rssi)
{
    uint32_t h = salted_hash(list->salt, id, id_len);
    for (size_t i = 0; i < SEEN_CAPACITY; i++) {
        if (list->slot[i].last_s != 0 && list->slot[i].hash == h) {
            int before = list->slot[i].rssi;
            int clamped = rssi < -127 ? -127 : (rssi > 127 ? 127 : rssi);
            if (before == SEEN_NO_RSSI || clamped > before) {
                list->slot[i].rssi = (int8_t)clamped;
            }
            return before;
        }
    }
    return SEEN_NO_RSSI;
}
