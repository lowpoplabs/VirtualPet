/* Desktop checks for the food classifier and seen list. Exit code 0 = pass. */
#include <stdio.h>
#include <string.h>

#include "pet_food.h"

static int failures;

#define CHECK(cond, what)                                   \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("FAIL: %s (line %d)\n", what, __LINE__); \
            failures++;                                     \
        }                                                   \
    } while (0)

static food_type_t wifi(const char *ssid, wifi_sec_t sec)
{
    wifi_sighting_t s = {.ssid = ssid, .sec = sec};
    return food_classify_wifi(&s);
}

static food_type_t ble_name(const char *name)
{
    ble_sighting_t s = {.name = name};
    return food_classify_ble(&s);
}

int main(void)
{
    /* Wi-Fi */
    CHECK(wifi("HomeNet", WIFI_SEC_SECURED) == FOOD_WIFI_SECURED, "wpa2 network");
    CHECK(wifi("HomeNet", WIFI_SEC_WPA3) == FOOD_WIFI_WPA3, "wpa3 network");
    CHECK(wifi("CoffeeShop Guest", WIFI_SEC_OPEN) == FOOD_WIFI_OPEN, "open network");
    CHECK(wifi("", WIFI_SEC_SECURED) == FOOD_WIFI_HIDDEN, "hidden network");
    CHECK(wifi(NULL, WIFI_SEC_OPEN) == FOOD_WIFI_HIDDEN, "hidden network, null name");
    CHECK(wifi("HP-Print-4A-ENVY", WIFI_SEC_SECURED) == FOOD_WIFI_PRINTER, "hp printer");
    CHECK(wifi("DIRECT-xy-EPSON", WIFI_SEC_SECURED) == FOOD_WIFI_PRINTER, "wifi direct printer");
    CHECK(wifi("Sam's iPhone", WIFI_SEC_WPA3) == FOOD_WIFI_HOTSPOT, "iphone hotspot");
    CHECK(wifi("galaxy s24", WIFI_SEC_SECURED) == FOOD_WIFI_HOTSPOT, "hotspot, case-insensitive");
    CHECK(wifi("SYNC_1A2B3C", WIFI_SEC_SECURED) == FOOD_WIFI_CAR, "ford car hotspot");
    CHECK(wifi("Uconnect 4C", WIFI_SEC_OPEN) == FOOD_WIFI_CAR, "open car hotspot is still a car");

    /* Bluetooth: names */
    CHECK(ble_name("") == FOOD_BLE_UNKNOWN, "nameless device is crumbs");
    CHECK(ble_name(NULL) == FOOD_BLE_UNKNOWN, "null name is crumbs");
    CHECK(ble_name("JBL Flip 6") == FOOD_BLE_SPEAKER, "jbl speaker");
    CHECK(ble_name("Ford SYNC") == FOOD_BLE_CAR, "ford sync");
    CHECK(ble_name("AirPods Pro") == FOOD_BLE_EARBUDS, "airpods by name");
    CHECK(ble_name("Galaxy Watch6") == FOOD_BLE_WATCH, "watch by name");
    CHECK(ble_name("[TV] Samsung 7 Series") == FOOD_BLE_TV, "tv by name");

    /* Bluetooth: appearance beats name guesses */
    {
        ble_sighting_t s = {.name = "LE-Bose QC", .has_appearance = true, .appearance = 0x0943};
        CHECK(food_classify_ble(&s) == FOOD_BLE_EARBUDS, "headphones by appearance");
        s.appearance = 0x0841;
        CHECK(food_classify_ble(&s) == FOOD_BLE_SPEAKER, "speaker by appearance");
        s.name = "";
        s.appearance = 0x00C1;
        CHECK(food_classify_ble(&s) == FOOD_BLE_WATCH, "watch by appearance");
        s.appearance = 0x0040;
        CHECK(food_classify_ble(&s) == FOOD_BLE_PHONE, "phone by appearance");
    }

    /* Bluetooth: Apple's company id is not automatically a phone */
    {
        static const uint8_t nearby[] = {0x10, 0x05, 0x01};
        static const uint8_t airpods[] = {0x07, 0x19, 0x01};
        static const uint8_t findmy[] = {0x12, 0x19, 0x10};
        ble_sighting_t s = {.has_company = true, .company_id = 0x004C,
                            .mfg_payload = nearby, .mfg_payload_len = sizeof(nearby)};
        CHECK(food_classify_ble(&s) == FOOD_BLE_PHONE, "apple nearby = phone");
        s.mfg_payload = airpods;
        CHECK(food_classify_ble(&s) == FOOD_BLE_EARBUDS, "apple proximity pairing = earbuds");
        s.mfg_payload = findmy;
        CHECK(food_classify_ble(&s) == FOOD_BLE_TRACKER, "apple find my = tracker");

        ble_sighting_t ms = {.has_company = true, .company_id = 0x0006};
        CHECK(food_classify_ble(&ms) == FOOD_BLE_UNKNOWN, "microsoft id is crumbs, not a phone");
    }
    {
        static const uint16_t tile[] = {0x180F, 0xFEED};
        ble_sighting_t s = {.name = "Tile", .uuids16 = tile, .num_uuids16 = 2};
        CHECK(food_classify_ble(&s) == FOOD_BLE_TRACKER, "tile by service id");
    }

    /* Fixes from the day 1 log */
    CHECK(wifi("DIRECT-roku-123-A1B2C3", WIFI_SEC_SECURED) == FOOD_WIFI_SECURED,
          "roku wifi direct is not a printer");
    CHECK(wifi("DIRECT-1A-HP ENVY Photo 7800", WIFI_SEC_SECURED) == FOOD_WIFI_PRINTER,
          "hp wifi direct is still a printer");
    CHECK(wifi("DIRECT-2B-HP M102 LaserJet", WIFI_SEC_SECURED) == FOOD_WIFI_PRINTER, "hp laserjet direct");
    CHECK(wifi("Motive Hotspot ADBV52", WIFI_SEC_WPA3) == FOOD_WIFI_CAR, "fleet truck gateway");
    {
        static const uint8_t ibeacon[] = {0x02, 0x15}, airplay[] = {0x09, 0x06}, airprint[] = {0x03, 0x01};
        ble_sighting_t s = {.has_company = true, .company_id = 0x004C,
                            .mfg_payload = ibeacon, .mfg_payload_len = 2};
        CHECK(food_classify_ble(&s) == FOOD_BLE_UNKNOWN, "ibeacon is not a phone");
        s.mfg_payload = airprint;
        CHECK(food_classify_ble(&s) == FOOD_BLE_UNKNOWN, "airprint is not a phone");
        s.mfg_payload = airplay;
        CHECK(food_classify_ble(&s) == FOOD_BLE_TV, "airplay target = tv");

        static const uint16_t share[] = {0xFEF3}, sonos_u[] = {0xFE07}, fastpair[] = {0xFE2C};
        ble_sighting_t a = {.uuids16 = share, .num_uuids16 = 1};
        CHECK(food_classify_ble(&a) == FOOD_BLE_PHONE, "android quick share = phone");
        ble_sighting_t so = {.has_company = true, .company_id = 0x05A7, .uuids16 = sonos_u, .num_uuids16 = 1};
        CHECK(food_classify_ble(&so) == FOOD_BLE_SPEAKER, "sonos by id");
        ble_sighting_t fp = {.uuids16 = fastpair, .num_uuids16 = 1};
        CHECK(food_classify_ble(&fp) == FOOD_BLE_EARBUDS, "fast pair = earbuds");
        ble_sighting_t jbl = {.has_company = true, .company_id = 0x0057};
        CHECK(food_classify_ble(&jbl) == FOOD_BLE_SPEAKER, "harman id = speaker");
        ble_sighting_t bose = {.name = "Bose QC Earbuds", .has_company = true, .company_id = 0x009E};
        CHECK(food_classify_ble(&bose) == FOOD_BLE_EARBUDS, "a name beats the brand id");
    }

    /* Values */
    CHECK(food_value(FOOD_BLE_UNKNOWN, 0) == 0.5f, "crumbs value");
    CHECK(food_value(FOOD_WIFI_CAR, 0) == 5, "car value");
    {
        int treats = 0;
        for (uint32_t r = 0; r < 100; r++) {
            float v = food_value(FOOD_WIFI_HIDDEN, r);
            CHECK(v == 1 || v == 5, "hidden value is 1 or 5");
            treats += v == 5;
        }
        CHECK(treats == 5, "1 hidden network in 20 is a treat");
    }
    CHECK(food_from_label("car hotspot") == FOOD_WIFI_CAR, "label lookup");
    CHECK(food_from_label("nonsense") == FOOD_TYPE_COUNT, "unknown label");
    CHECK(food_is_ble(FOOD_BLE_CAR) && !food_is_ble(FOOD_WIFI_CAR), "group split");

    /* Seen list */
    {
        static seen_list_t list;
        const uint8_t a[6] = {1, 2, 3, 4, 5, 6}, b[6] = {1, 2, 3, 4, 5, 7};
        seen_init(&list, 0xC0FFEE);
        CHECK(!seen_recently(&list, a, 6, 100, 3600), "first sighting is new");
        CHECK(seen_recently(&list, a, 6, 130, 3600), "second sighting is a repeat");
        CHECK(!seen_recently(&list, b, 6, 130, 3600), "different id is new");
        CHECK(seen_recently(&list, a, 6, 3699, 3600), "still a repeat just before regrow");
        CHECK(!seen_recently(&list, a, 6, 3700, 3600), "edible again after regrow");

        /* How strongly it has been heard, for the log. */
        const uint8_t c[6] = {9, 9, 9, 9, 9, 9};
        CHECK(seen_note_rssi(&list, c, 6, -70) == SEEN_NO_RSSI, "not on the list: nothing to note");
        CHECK(seen_note_rssi(&list, b, 6, -90) == SEEN_NO_RSSI, "first note: no strength before");
        CHECK(seen_note_rssi(&list, b, 6, -80) == -90, "then the one before");
        CHECK(seen_note_rssi(&list, b, 6, -95) == -80 && seen_note_rssi(&list, b, 6, -60) == -80,
              "the strongest is kept, not the latest");
        CHECK(!seen_recently(&list, b, 6, 9000, 3600) && seen_note_rssi(&list, b, 6, -85) == SEEN_NO_RSSI,
              "eaten again: it starts over");
        CHECK(seen_recently(&list, a, 6, 3701, 3600), "regrow timer restarted");

        /* Overflow pushes out the oldest, not the newest. */
        seen_init(&list, 1);
        for (uint32_t i = 0; i < SEEN_CAPACITY + 10; i++) {
            uint8_t id[4];
            memcpy(id, &i, 4);
            seen_recently(&list, id, 4, 1000 + i, 3600);
        }
        uint32_t first = 0, last = SEEN_CAPACITY + 9;
        uint32_t later = 1000 + SEEN_CAPACITY + 20, day = 86400;
        CHECK(seen_recently(&list, (uint8_t *)&last, 4, later, day), "newest was kept");
        CHECK(!seen_recently(&list, (uint8_t *)&first, 4, later, day), "oldest was pushed out");

        /* A different salt gives different hashes for the same id. */
        static seen_list_t other;
        seen_init(&list, 111);
        seen_init(&other, 222);
        seen_recently(&list, a, 6, 5, 3600);
        seen_recently(&other, a, 6, 5, 3600);
        CHECK(list.slot[0].hash != other.slot[0].hash, "salt changes the stored hash");
    }

    printf(failures ? "%d check(s) failed\n" : "all food checks passed\n", failures);
    return failures ? 1 : 0;
}
