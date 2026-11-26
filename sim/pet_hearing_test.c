/* Desktop checks for hearing: networks and devices into food and surroundings. Exit code 0 = pass. */
#include <stdio.h>
#include <string.h>

#include "pet_hearing.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

static uint32_t s_random = 12345;

static uint32_t fake_random(void)
{
    s_random = s_random * 1664525u + 1013904223u;
    return s_random;
}

static pet_hearing_t h;
static food_event_t ev;

/* A network whose address ends in id. */
static heard_t network(uint8_t id, const char *name, wifi_sec_t sec, int rssi, uint32_t now)
{
    const uint8_t addr[6] = {0x02, 0x11, 0x22, 0x33, 0x44, id};
    const heard_network_t n = {.addr = addr, .rssi = rssi, .what = {.ssid = name, .sec = sec}};
    return hearing_network(&h, &n, now, &ev);
}

static bool device(uint8_t id, const char *name, int rssi, uint32_t now)
{
    const uint8_t addr[6] = {0x06, 0x55, 0x66, 0x77, 0x88, id};
    const heard_device_t d = {.addr = addr, .rssi = rssi, .what = {.name = name}};
    return hearing_device(&h, &d, now, &ev);
}

int main(void)
{
    const pet_scan_t *scan;
    const hearing_cycle_t *c;
    const uint32_t DAY = 24 * 3600;

    hearing_init(&h, 0xBEEF, fake_random);

    /* A network is eaten once a day. */
    hearing_begin(&h);
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -60, 100) == HEARD_EATEN, "a new network is a meal");
    CHECK(ev.type == FOOD_WIFI_SECURED && ev.rssi == -60 && !strcmp(ev.name, "HomeNet"), "with its kind, strength and name");
    CHECK(ev.value > 0 && ev.detail[0] == '\0', "it has a value, and the detail is left to the platform");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -60, 160) == HEARD_OLD, "not again a minute later");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -57, 220) == HEARD_OLD, "nor for being a little stronger");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -49, 280) == HEARD_NEARER, "much stronger is worth a line in the log");
    CHECK(ev.type == FOOD_WIFI_SECURED && ev.rssi == -49 && !strcmp(ev.name, "HomeNet") && ev.value == 0,
          "which says what and how strong, and is no meal");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -49, 340) == HEARD_OLD, "once");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -60, 100 + DAY - 1) == HEARD_OLD, "still eaten just short of a day");
    CHECK(network(1, "HomeNet", WIFI_SEC_SECURED, -60, 100 + DAY) == HEARD_EATEN, "edible again after a day");
    c = hearing_end(&h, &scan);
    CHECK(c->wifi_seen == 7 && c->wifi_new == 2 && c->wifi_best == -49, "the cycle counts heard, eaten and the strongest");
    CHECK(c->prints == 1 && scan->prints == 1, "one network, heard many times, is one print");
    CHECK(scan->wifi_heard == 7 && scan->ble_heard == 0 && scan->motion == 0, "the scan carries the counts; motion is the platform's");

    /* Surroundings: what marks a place, and how strong it must be. */
    hearing_begin(&h);
    c = hearing_end(&h, &scan);
    CHECK(c->wifi_seen == 0 && c->wifi_best == 0 && c->prints == 0 && c->ride_prints == 0 && scan->prints == 0,
          "a new cycle begins from nothing");
    hearing_begin(&h);
    network(10, "Office", WIFI_SEC_WPA3, -70, 500);
    network(11, "", WIFI_SEC_SECURED, -92, 500);
    network(12, "Far Away", WIFI_SEC_OPEN, -93, 500);
    network(13, "Sam's iPhone", WIFI_SEC_WPA3, -40, 500);
    c = hearing_end(&h, &scan);
    CHECK(scan->prints == 2, "fixed networks at -92 or better are prints; a faint one and a phone are not");
    CHECK(c->ride_prints == 3, "for a ride the faint one counts too, the phone still not");
    CHECK(scan->print[0] != 0 && scan->print[0] != scan->print[1], "prints are never 0 and differ");
    CHECK(scan->print[0] == c->ride_print[0], "the same network has the same print in both lists");
    {
        const uint8_t addr[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 10};
        CHECK(scan->print[0] == place_print(0xBEEF, addr, 6), "and it is the place print, scrambled by the salt given");
    }

    /* More networks than there is room for: the first heard are kept. */
    hearing_begin(&h);
    for (int i = 0; i < 40; i++) {
        network((uint8_t)(100 + i), "Street", WIFI_SEC_SECURED, -60 - i / 4, 600);
    }
    c = hearing_end(&h, &scan);
    CHECK(scan->prints == SCAN_PRINTS_MAX && c->ride_prints == RIDE_PRINTS, "both lists stop when full");
    CHECK(c->wifi_seen == 40 && c->wifi_new == 40 && c->wifi_best == -60, "and everything is still counted and eaten");

    /* Place names. */
    hearing_begin(&h);
    network(60, "Starbucks WiFi", WIFI_SEC_OPEN, -60, 700);
    network(61, "Starbucks WiFi", WIFI_SEC_OPEN, -80, 700);
    network(62, "Hilton Honors", WIFI_SEC_OPEN, -85, 700);
    network(63, "DUNKIN Guest", WIFI_SEC_OPEN, -93, 700);
    c = hearing_end(&h, &scan);
    CHECK(scan->matches == 2 && c->matches == 2, "a chain heard twice is one place; one too faint is none");
    CHECK(scan->match[0].kind == PLACE_COFFEE && scan->match[0].chain != 0 && scan->match[0].faint == 0,
          "a strong name counts at once");
    CHECK(scan->match[1].kind == PLACE_HOTEL && scan->match[1].faint == 1, "a fainter one must be heard again");

    /* Devices. */
    hearing_begin(&h);
    CHECK(device(1, "JBL Flip 6", -70, 800), "a new device is a meal");
    CHECK(ev.type == FOOD_BLE_SPEAKER && ev.rssi == -70 && !strcmp(ev.name, "JBL Flip 6") && ev.value > 0,
          "with its kind, strength, name and value");
    CHECK(!device(1, "JBL Flip 6", -70, 801), "not twice");
    CHECK(device(2, NULL, -90, 802) && ev.type == FOOD_BLE_UNKNOWN && ev.name[0] == '\0', "no name is crumbs");
    CHECK(!device(2, NULL, -90, 803) && !device(1, "JBL Flip 6", -70, 804), "nor these again");
    {
        const uint8_t addr[6] = {0x06, 0x55, 0x66, 0x77, 0x88, 3};
        const uint8_t payload[] = {0x12, 0x19};
        const uint16_t uuids[] = {0xFEAA, 0x180F};
        const heard_device_t d = {
            .addr = addr, .rssi = -66,
            .what = {.name = "", .has_company = true, .company_id = 0x004C, .mfg_payload = payload,
                     .mfg_payload_len = sizeof(payload), .has_appearance = true, .appearance = 0x00C1,
                     .uuids16 = uuids, .num_uuids16 = 2, .has_svc_byte = true, .svc_uuid = 0xFEAA,
                     .svc_byte = 0x40},
        };
        CHECK(hearing_device(&h, &d, 805, &ev), "a device that says a lot about itself");
        CHECK(!strcmp(ev.detail, "co=004C/12 app=00C1 u=FEAA/40 u=180F"), "has it all in the detail");
    }
    c = hearing_end(&h, &scan);
    CHECK(c->ble_seen == 3 && c->ble_new == 3 && scan->ble_heard == 3, "three devices, however often each was heard");
    hearing_begin(&h);
    device(1, "JBL Flip 6", -70, 900);
    c = hearing_end(&h, &scan);
    CHECK(c->ble_seen == 1 && c->ble_new == 0, "next cycle it is heard again, and not eaten");
    hearing_begin(&h);
    CHECK(device(1, "JBL Flip 6", -70, 800 + DAY), "but it is a day later");
    for (int i = 0; i < 300; i++) {
        const uint8_t addr[6] = {0x0A, 0, 0, 0, (uint8_t)(i >> 8), (uint8_t)i};
        const heard_device_t d = {.addr = addr, .rssi = -80};
        hearing_device(&h, &d, 1000 + DAY, &ev);
    }
    c = hearing_end(&h, &scan);
    CHECK(c->ble_seen == 301 && c->ble_new == 301, "a crowd is all counted and eaten");

    /* A network and a device with the same address bytes would be one thing; these differ, so both are eaten. */
    hearing_begin(&h);
    CHECK(network(200, "Shop", WIFI_SEC_SECURED, -60, 5 * DAY) == HEARD_EATEN && device(200, "Shop", -60, 5 * DAY),
          "networks and devices share the seen list without colliding");
    hearing_end(&h, &scan);

    /* Another start salts the seen list afresh but keeps the prints. */
    {
        static pet_hearing_t other;
        const uint8_t addr[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 10};
        const heard_network_t n = {.addr = addr, .rssi = -70, .what = {.ssid = "Office", .sec = WIFI_SEC_WPA3}};
        hearing_init(&other, 0xBEEF, fake_random);
        hearing_begin(&other);
        CHECK(hearing_network(&other, &n, 50, &ev) == HEARD_EATEN, "a fresh start has eaten nothing");
        hearing_end(&other, &scan);
        CHECK(scan->print[0] == place_print(0xBEEF, addr, 6), "and knows the same place by the same print");
    }

    /* Names off the air. */
    {
        char name[9];
        const uint8_t raw[] = {'C', 'a', 'f', 0xC3, 0xA9, ' ', 0x07, '!'};
        hearing_clean_name(name, sizeof(name), raw, sizeof(raw));
        CHECK(!strcmp(name, "Caf?? ?!"), "anything but printable ASCII becomes ?");
        hearing_clean_name(name, 5, raw, sizeof(raw));
        CHECK(!strcmp(name, "Caf?"), "and a long name is cut to fit");
        hearing_clean_name(name, sizeof(name), raw, 0);
        CHECK(name[0] == '\0', "no name is an empty one");
    }

    printf(failures ? "%d check(s) failed\n" : "all hearing checks passed\n", failures);
    return failures ? 1 : 0;
}
