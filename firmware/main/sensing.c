#include "sensing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "exchange.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

/* Rhythm when nobody supplies a power plan. */
#define CYCLE_PERIOD_MS      30000
/* How often the wait between scans re-reads the power plan. */
#define RECHECK_MS           5000
#define WIFI_DWELL_MS        120     /* per channel; beacons come every ~102 ms */
#define WIFI_MAX_NETWORKS    64
#define BLE_SCAN_MS          5000

static const char *TAG = "sense";

static sensing_callbacks_t s_cb;
/*
 * What is done with a network or a device once it has been heard is shared
 * with the other platforms: app/include/pet_hearing.h. In PSRAM: internal
 * RAM is scarce.
 */
static pet_hearing_t *s_hearing;
static SemaphoreHandle_t s_lock;        /* guards s_hearing and callbacks */
static SemaphoreHandle_t s_ble_synced;
static SemaphoreHandle_t s_ble_done;
static bool s_scan_ok;

static uint32_t now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000) + 1;
}

/* Caller holds s_lock. */
static void emit(const food_event_t *ev)
{
    if (s_cb.on_food) {
        s_cb.on_food(ev);
    }
}

/* ---- Wi-Fi ----------------------------------------------------------- */

static wifi_sec_t map_auth(wifi_auth_mode_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:   /* "enhanced open": encrypted, but no password */
        return WIFI_SEC_OPEN;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_EXT_PSK:
    case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
        return WIFI_SEC_WPA3;
    default:
        return WIFI_SEC_SECURED;
    }
}

static esp_err_t wifi_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    /* Nothing about networks is kept in flash. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");
    /* Station mode only so the radio can scan. It is never told to connect. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode");
    return esp_wifi_start();
}

static void wifi_scan_once(wifi_ap_record_t *records)
{
    const wifi_scan_config_t cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = WIFI_DWELL_MS,
    };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan failed: %s", esp_err_to_name(err));
        return;
    }
    uint16_t count = WIFI_MAX_NETWORKS;
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        esp_wifi_clear_ap_list();
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_ok = true;
    /* The results come strongest first, so the prints kept are the nearest networks. */
    for (int i = 0; i < count; i++) {
        const wifi_ap_record_t *ap = &records[i];
        char name[33];
        hearing_clean_name(name, sizeof(name), ap->ssid, strnlen((const char *)ap->ssid, 32));
        const heard_network_t heard = {
            .addr = ap->bssid, .rssi = ap->rssi, .what = {.ssid = name, .sec = map_auth(ap->authmode)}};
        food_event_t ev;
        switch (hearing_network(s_hearing, &heard, now_s(), &ev)) {
        case HEARD_EATEN:
            snprintf(ev.detail, sizeof(ev.detail), "auth=%d ch=%d", (int)ap->authmode, (int)ap->primary);
            emit(&ev);
            break;
        case HEARD_NEARER:
            if (s_cb.on_heard) {
                s_cb.on_heard(&ev);
            }
            break;
        default:
            break;
        }
    }
    xSemaphoreGive(s_lock);
    /* The scan results hold addresses; do not leave them lying in the buffer. */
    memset(records, 0, sizeof(wifi_ap_record_t) * WIFI_MAX_NETWORKS);
}

/* ---- Bluetooth LE ---------------------------------------------------- */

/*
 * With an active scan a device is heard in two packets: its advertisement,
 * then its scan response (usually where the name is). They are merged here
 * by address for the length of one scan, then classified together. The
 * table holds raw addresses, so it is wiped as soon as the scan is eaten.
 */
#define BLE_MAX_PER_SCAN 64
#define BLE_MAX_UUIDS    8

typedef struct {
    uint8_t addr[6];
    int8_t rssi;
    char name[33];
    bool has_company;
    uint16_t company_id;
    uint8_t payload[4];
    uint8_t payload_len;
    bool has_appearance;
    uint16_t appearance;
    uint16_t uuids[BLE_MAX_UUIDS];
    uint8_t num_uuids;
    bool has_svc_byte;      /* the first byte of one service's data: the kind of beacon */
    uint16_t svc_uuid;
    uint8_t svc_byte;
} ble_heard_t;

static ble_heard_t s_heard[BLE_MAX_PER_SCAN];
static int s_heard_count;

static void add_uuid(ble_heard_t *h, uint16_t uuid)
{
    for (int i = 0; i < h->num_uuids; i++) {
        if (h->uuids[i] == uuid) {
            return;
        }
    }
    if (h->num_uuids < BLE_MAX_UUIDS) {
        h->uuids[h->num_uuids++] = uuid;
    }
}

/* Caller holds s_lock. */
static void eat_heard(const ble_heard_t *h)
{
    const heard_device_t heard = {
        .addr = h->addr,
        .rssi = h->rssi,
        .what = {
            .name = h->name,
            .has_company = h->has_company,
            .company_id = h->company_id,
            .mfg_payload = h->payload,
            .mfg_payload_len = h->payload_len,
            .has_appearance = h->has_appearance,
            .appearance = h->appearance,
            .uuids16 = h->uuids,
            .num_uuids16 = h->num_uuids,
            .has_svc_byte = h->has_svc_byte,
            .svc_uuid = h->svc_uuid,
            .svc_byte = h->svc_byte,
        },
    };
    food_event_t ev;
    if (hearing_device(s_hearing, &heard, now_s(), &ev)) {
        emit(&ev);
    }
}

static void handle_advert(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) != 0) {
        memset(&f, 0, sizeof(f));   /* malformed: still counts as crumbs */
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    ble_heard_t *h = NULL;
    for (int i = 0; i < s_heard_count; i++) {
        if (memcmp(s_heard[i].addr, d->addr.val, 6) == 0) {
            h = &s_heard[i];
            break;
        }
    }
    ble_heard_t overflow;
    if (!h) {
        /* Table full (a very busy place): eat this one unmerged. */
        h = s_heard_count < BLE_MAX_PER_SCAN ? &s_heard[s_heard_count++] : &overflow;
        memset(h, 0, sizeof(*h));
        memcpy(h->addr, d->addr.val, 6);
        h->rssi = d->rssi;
    }

    if (f.name && !h->name[0]) {
        hearing_clean_name(h->name, sizeof(h->name), f.name, f.name_len);
    }
    if (f.mfg_data && f.mfg_data_len >= 2 && !h->has_company) {
        h->has_company = true;
        h->company_id = (uint16_t)(f.mfg_data[0] | (f.mfg_data[1] << 8));
        h->payload_len = f.mfg_data_len - 2 < (int)sizeof(h->payload)
                             ? f.mfg_data_len - 2 : sizeof(h->payload);
        memcpy(h->payload, f.mfg_data + 2, h->payload_len);
    }
    if (f.appearance_is_present) {
        h->has_appearance = true;
        h->appearance = f.appearance;
    }
    for (int i = 0; i < f.num_uuids16; i++) {
        add_uuid(h, f.uuids16[i].value);
    }
    if (f.svc_data_uuid16 && f.svc_data_uuid16_len >= 2) {
        uint16_t svc = (uint16_t)(f.svc_data_uuid16[0] | (f.svc_data_uuid16[1] << 8));
        add_uuid(h, svc);
        /* Only the byte that says what kind of beacon it is; nothing that says which one. */
        if (f.svc_data_uuid16_len >= 3 && (!h->has_svc_byte || svc == 0xFEAA)) {
            h->has_svc_byte = true;
            h->svc_uuid = svc;
            h->svc_byte = f.svc_data_uuid16[2];
        }
    }

    if (h == &overflow) {
        eat_heard(h);
        memset(&overflow, 0, sizeof(overflow));
    }
    xSemaphoreGive(s_lock);
}

/* Classifies and eats everything heard in the scan that just ended. */
static void eat_scan(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < s_heard_count; i++) {
        eat_heard(&s_heard[i]);
    }
    memset(s_heard, 0, sizeof(s_heard));
    s_heard_count = 0;
    xSemaphoreGive(s_lock);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_advert(&event->disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        xSemaphoreGive(s_ble_done);
        break;
    default:
        break;
    }
    return 0;
}

static void ble_on_sync(void)
{
    xSemaphoreGive(s_ble_synced);
}

static void ble_host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t ble_init(void)
{
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble");
    ble_hs_cfg.sync_cb = ble_on_sync;
    exchange_init();    /* the one service, for the exchange screen; nothing is offered until it is open */
    nimble_port_freertos_init(ble_host_task);
    if (xSemaphoreTake(s_ble_synced, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void ble_scan_once(void)
{
    /*
     * Active scan: asks each device for its scan response, which is where
     * most names are. That request carries our address, so a fresh random
     * one is used for every scan and the watch cannot be followed by it.
     */
    ble_addr_t addr;
    int rc = ble_hs_id_gen_rnd(1, &addr);
    if (rc == 0) {
        rc = ble_hs_id_set_rnd(addr.val);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "ble random address failed: %d", rc);
        return;
    }

    const struct ble_gap_disc_params params = {
        .passive = 0,
        .filter_duplicates = 1,
    };
    xSemaphoreTake(s_ble_done, 0);
    rc = ble_gap_disc(BLE_OWN_ADDR_RANDOM, BLE_SCAN_MS, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble scan failed: %d", rc);
        return;
    }
    if (xSemaphoreTake(s_ble_done, pdMS_TO_TICKS(BLE_SCAN_MS + 2000)) != pdTRUE) {
        ble_gap_disc_cancel();
    }
    eat_scan();
}

/* ---- Loop ------------------------------------------------------------ */

static void sensing_task(void *arg)
{
    bool ble_ok = (bool)(uintptr_t)arg;
    wifi_ap_record_t *records = calloc(WIFI_MAX_NETWORKS, sizeof(wifi_ap_record_t));
    if (!records) {
        ESP_LOGE(TAG, "no memory for scan results");
        vTaskDelete(NULL);
    }

    bool wifi_on = true;   /* started by wifi_init() */
    for (;;) {
        int64_t started = esp_timer_get_time();
        bool radio_off = false;
        uint32_t period_s = s_cb.scan_period_s ? s_cb.scan_period_s(&radio_off)
                                               : CYCLE_PERIOD_MS / 1000;

        /*
         * One radio. A Wi-Fi scan takes it for well over a second, which a
         * link cannot spare, so nothing is listened for while the exchange
         * screen is open. The pet eats again when it is closed.
         */
        if (period_s != 0 && !exchange_busy()) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            hearing_begin(s_hearing);
            s_scan_ok = false;
            xSemaphoreGive(s_lock);

            if (!wifi_on) {
                wifi_on = esp_wifi_start() == ESP_OK;
            }
            /* One radio, so the two scans take turns. */
            if (wifi_on) {
                wifi_scan_once(records);
            }
            if (ble_ok && !exchange_busy()) {    /* the screen was opened during the Wi-Fi scan */
                ble_scan_once();
            }

            xSemaphoreTake(s_lock, portMAX_DELAY);
            const pet_scan_t *scan;
            const sensing_cycle_t *cycle = hearing_end(s_hearing, &scan);
            if (s_cb.on_cycle) {
                s_cb.on_cycle(cycle);
            }
            if (s_cb.on_scan && s_scan_ok) {
                s_cb.on_scan(scan);
            }
            xSemaphoreGive(s_lock);
        }

        /*
         * An idle Wi-Fi radio still listens, and that is the largest drain
         * between scans. On battery it is switched off until the next one.
         */
        if (wifi_on && (radio_off || period_s == 0)) {
            esp_wifi_stop();
            wifi_on = false;
        }

        /*
         * Wait for the next scan, re-asking every few seconds so that
         * plugging in (or the battery recovering) takes effect promptly.
         */
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(RECHECK_MS));
            if (s_cb.scan_period_s) {
                period_s = s_cb.scan_period_s(&radio_off);
            }
            int64_t waited_s = (esp_timer_get_time() - started) / 1000000;
            if (period_s != 0 && waited_s >= period_s) {
                break;
            }
        }
    }
}

esp_err_t sensing_start(const sensing_callbacks_t *cb)
{
    s_cb = *cb;
    s_lock = xSemaphoreCreateMutex();
    s_ble_synced = xSemaphoreCreateBinary();
    s_ble_done = xSemaphoreCreateBinary();
    s_hearing = heap_caps_malloc(sizeof(pet_hearing_t), MALLOC_CAP_SPIRAM);
    if (!s_hearing || !s_lock || !s_ble_synced || !s_ble_done) {
        return ESP_ERR_NO_MEM;
    }
    hearing_init(s_hearing, s_cb.place_salt, esp_random);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    ESP_RETURN_ON_ERROR(wifi_init(), TAG, "wifi");

    /* Bluetooth failing should not stop the pet eating Wi-Fi. */
    esp_err_t ble_err = ble_init();
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "bluetooth unavailable: %s", esp_err_to_name(ble_err));
    }

    BaseType_t ok = xTaskCreate(sensing_task, "sensing", 6144,
                                (void *)(uintptr_t)(ble_err == ESP_OK), 4, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
