/* Hearing: see pet_hearing.h. */
#include "pet_hearing.h"

#include <stdio.h>
#include <string.h>

/*
 * How long before the same network or device can be eaten again: a day.
 * At an hour (the first setting) one open guest network at home was eaten
 * 26 times in the day 1 log and made the pet queasy without going out.
 */
#define REGROW_S             (24 * 3600)
/*
 * Networks weaker than this are too far off to say where the pet is. It
 * was -85 until the first day at an office: in a pocket the device heard
 * the office's own networks at about that, so the habitat took hours to
 * form and then came and went all day.
 */
#define PLACE_PRINT_MIN_RSSI (-92)
/*
 * A place name this strong counts at once. A fainter one (down to the
 * second figure) counts only once it has been heard again a few minutes
 * later, which a drive past never is: see PLACE_STAY_S. It was a flat -75
 * until a day of visits showed that standing inside a shop, in a pocket,
 * reads -83 to -92.
 */
#define PLACE_NAME_NEAR_RSSI (-70)
#define PLACE_NAME_MIN_RSSI  (-92)
/* An eaten network heard this much more strongly than before is logged again, for tuning. */
#define HEARD_STEP_DB        8

#define ADDR_LEN 6

void hearing_init(pet_hearing_t *h, uint32_t place_salt, uint32_t (*random)(void))
{
    memset(h, 0, sizeof(*h));
    h->place_salt = place_salt;
    h->random = random;
    seen_init(&h->seen, random());
    h->tally_salt = random();
}

void hearing_begin(pet_hearing_t *h)
{
    memset(&h->cycle, 0, sizeof(h->cycle));
    memset(&h->scan, 0, sizeof(h->scan));
    memset(h->tally, 0, sizeof(h->tally));
}

void hearing_clean_name(char *dst, size_t dst_size, const uint8_t *src, size_t len)
{
    size_t n = len < dst_size - 1 ? len : dst_size - 1;
    for (size_t i = 0; i < n; i++) {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? (char)src[i] : '?';
    }
    dst[n] = '\0';
}

/* One network heard, for places. Called with every network, eaten or not. */
static void note_surroundings(pet_hearing_t *h, const heard_network_t *n, const char *name, food_type_t type)
{
    hearing_cycle_t *cycle = &h->cycle;
    pet_scan_t *scan = &h->scan;
    /* Phones and cars move about; only things bolted down mark a place. */
    bool fixed = type == FOOD_WIFI_SECURED || type == FOOD_WIFI_WPA3 || type == FOOD_WIFI_OPEN ||
                 type == FOOD_WIFI_PRINTER || type == FOOD_WIFI_HIDDEN;
    uint16_t print = fixed ? place_print(h->place_salt, n->addr, ADDR_LEN) : 0;
    /* For telling a ride, however faint: by the roadside they mostly are. */
    if (fixed && cycle->ride_prints < RIDE_PRINTS) {
        bool have = false;
        for (int i = 0; i < cycle->ride_prints; i++) {
            have = have || cycle->ride_print[i] == print;
        }
        if (!have) {
            cycle->ride_print[cycle->ride_prints++] = print;
        }
    }
    if (fixed && n->rssi >= PLACE_PRINT_MIN_RSSI && scan->prints < SCAN_PRINTS_MAX) {
        bool have = false;
        for (int i = 0; i < scan->prints; i++) {
            have = have || scan->print[i] == print;
        }
        if (!have) {
            scan->print[scan->prints++] = print;
        }
    }
    place_kind_t kind;
    uint16_t chain;
    if (n->rssi >= PLACE_NAME_MIN_RSSI && scan->matches < SCAN_MATCHES_MAX &&
        place_match_name(name, &kind, &chain)) {
        bool have = false;
        for (int i = 0; i < scan->matches; i++) {
            have = have || (scan->match[i].kind == kind && scan->match[i].chain == chain);
        }
        if (!have) {
            scan->match[scan->matches].kind = (uint8_t)kind;
            scan->match[scan->matches].faint = n->rssi < PLACE_NAME_NEAR_RSSI;
            scan->match[scan->matches++].chain = chain;
        }
    }
}

static void begin_event(food_event_t *ev, int rssi, const char *name)
{
    memset(ev, 0, sizeof(*ev));
    ev->rssi = (int8_t)(rssi < -128 ? -128 : (rssi > 127 ? 127 : rssi));
    snprintf(ev->name, sizeof(ev->name), "%s", name ? name : "");
}

heard_t hearing_network(pet_hearing_t *h, const heard_network_t *n, uint32_t now_s, food_event_t *ev)
{
    begin_event(ev, n->rssi, n->what.ssid);
    if (h->cycle.wifi_seen == 0 || n->rssi > h->cycle.wifi_best) {
        h->cycle.wifi_best = n->rssi;
    }
    h->cycle.wifi_seen++;

    wifi_sighting_t s = {.ssid = ev->name, .sec = n->what.sec};
    ev->type = food_classify_wifi(&s);
    note_surroundings(h, n, ev->name, ev->type);
    bool eaten_before = seen_recently(&h->seen, n->addr, ADDR_LEN, now_s, REGROW_S);
    int before = seen_note_rssi(&h->seen, n->addr, ADDR_LEN, n->rssi);
    if (eaten_before) {
        return before != SEEN_NO_RSSI && n->rssi >= before + HEARD_STEP_DB ? HEARD_NEARER : HEARD_OLD;
    }
    h->cycle.wifi_new++;
    ev->value = food_value(ev->type, h->random());
    return HEARD_EATEN;
}

/* Counts a device once a cycle, by a salted hash of its address. */
static void tally(pet_hearing_t *h, const uint8_t *addr)
{
    uint32_t hash = h->tally_salt;
    for (int i = 0; i < ADDR_LEN; i++) {
        hash = (hash ^ addr[i]) * 16777619u;
    }
    hash |= 1;
    int seen = h->cycle.ble_seen;
    for (int i = 0; i < seen && i < HEARING_DEVICES_MAX; i++) {
        if (h->tally[i] == hash) {
            return;
        }
    }
    if (seen < HEARING_DEVICES_MAX) {
        h->tally[seen] = hash;
    }
    h->cycle.ble_seen++;
}

bool hearing_device(pet_hearing_t *h, const heard_device_t *d, uint32_t now_s, food_event_t *ev)
{
    begin_event(ev, d->rssi, d->what.name);
    tally(h, d->addr);
    if (seen_recently(&h->seen, d->addr, ADDR_LEN, now_s, REGROW_S)) {
        return false;
    }
    ble_sighting_t s = d->what;
    s.name = ev->name;
    ev->type = food_classify_ble(&s);

    int len = 0;
    if (s.has_company) {
        len += snprintf(ev->detail + len, sizeof(ev->detail) - len, "co=%04X", s.company_id);
        if (s.mfg_payload_len > 0) {
            len += snprintf(ev->detail + len, sizeof(ev->detail) - len, "/%02X", s.mfg_payload[0]);
        }
    }
    if (s.has_appearance && len < (int)sizeof(ev->detail) - 1) {
        len += snprintf(ev->detail + len, sizeof(ev->detail) - len, " app=%04X", s.appearance);
    }
    for (size_t i = 0; i < s.num_uuids16 && len < (int)sizeof(ev->detail) - 8; i++) {
        len += snprintf(ev->detail + len, sizeof(ev->detail) - len, " u=%04X", s.uuids16[i]);
        if (s.has_svc_byte && s.uuids16[i] == s.svc_uuid && len < (int)sizeof(ev->detail) - 4) {
            len += snprintf(ev->detail + len, sizeof(ev->detail) - len, "/%02X", s.svc_byte);
        }
    }

    h->cycle.ble_new++;
    ev->value = food_value(ev->type, h->random());
    return true;
}

const hearing_cycle_t *hearing_end(pet_hearing_t *h, const pet_scan_t **scan)
{
    h->cycle.prints = h->scan.prints;
    h->cycle.matches = h->scan.matches;
    h->scan.wifi_heard = (uint16_t)h->cycle.wifi_seen;
    h->scan.ble_heard = (uint16_t)h->cycle.ble_seen;
    if (scan) {
        *scan = &h->scan;
    }
    return &h->cycle;
}
