#include "exchange.h"

#include <string.h>

#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "board.h"
#include "pet_app.h"
#include "pet_link.h"
#include "pet_ui.h"

static const char *TAG = "exchange";

void ble_store_config_init(void);

#define NO_CONN   0xFFFF
#define TICK_US   (50 * 1000)
#define PIECE_MAX 244           /* the most one notification carries on any link */

static ble_uuid128_t s_svc_uuid, s_rx_uuid, s_tx_uuid;
static uint16_t s_tx_handle;

/*
 * Everything below is touched by three tasks: the screen's (exchange_ask),
 * the Bluetooth host's (the events) and the timer's (tick). The link and
 * the flags are only read or changed with the screen's lock held; the
 * Bluetooth calls themselves are made without it.
 */
static pet_link_t *s_link;
static esp_timer_handle_t s_timer;
static volatile bool s_want;        /* the screen is open */
static bool s_give;
static bool s_adv;                  /* waiting for a call */
static uint16_t s_conn = NO_CONN;
static ble_addr_t s_peer;
static bool s_secure, s_listening;  /* the code was right; the caller asked to hear our half */
static bool s_running;              /* pet_link has been started on this connection */
static bool s_over;                 /* this session has had its exchange: no more calls */
static uint8_t s_piece[PIECE_MAX];
static uint16_t s_piece_len;
static int s_idle_ticks;

/*
 * Between scans the Bluetooth controller dozes, and times its doze by the
 * chip's rough internal clock. That is good enough for listening and not
 * for a link, which must answer on the beat: with the doze on, a caller
 * hears nothing for five seconds and gives up. So for as long as the
 * exchange has the radio the controller, and the chip, stay awake.
 */
static esp_pm_lock_handle_t s_awake_lock;
static bool s_awake;

static void stay_awake(bool on)
{
    if (on == s_awake) {
        return;
    }
    s_awake = on;
    if (on) {
        if (s_awake_lock) {
            esp_pm_lock_acquire(s_awake_lock);
        }
        esp_bt_sleep_disable();
        esp_bt_controller_wakeup_request();
    } else {
        esp_bt_sleep_enable();
        if (s_awake_lock) {
            esp_pm_lock_release(s_awake_lock);
        }
    }
}

static void tell(pet_exchange_step_t step, uint32_t code)
{
    if (board_lvgl_lock(1000)) {
        pet_app_exchange_told(step, code, s_link);
        board_lvgl_unlock();
    }
}

/* "9a12f1ff-962b-..." to the sixteen bytes, last first, that the stack wants. */
static void parse_uuid(const char *text, ble_uuid128_t *out)
{
    out->u.type = BLE_UUID_TYPE_128;
    int at = 15;
    for (const char *p = text; *p && at >= 0; p++) {
        if (*p == '-') {
            continue;
        }
        char pair[3] = {p[0], p[1], 0};
        out->value[at--] = (uint8_t)strtoul(pair, NULL, 16);
        p++;
    }
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    /* A new random address for each time the screen is opened, so the watch cannot be followed by it. */
    ble_addr_t addr;
    int rc = ble_hs_id_gen_rnd(0, &addr);
    if (rc == 0) {
        rc = ble_hs_id_set_rnd(addr.val);
    }
    /* Only the service is said: no name, nothing about whose watch this is. */
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &s_svc_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    if (rc == 0) {
        rc = ble_gap_adv_set_fields(&fields);
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    if (rc == 0) {
        rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "cannot wait for a call: %d", rc);
        return;
    }
    s_adv = true;
    tell(PET_EXCHANGE_WAITING, 0);
}

/*
 * The exchange itself begins with the caller's first bytes, and not
 * before: the waiting side never speaks first. A caller may still be
 * making sure of the pairing when its link is ready to carry bytes, and
 * anything said to it then goes unheard. The code must have been right
 * and the caller must be listening. Called with the screen's lock held.
 */
static void start_if_ready(void)
{
    if (s_link && s_secure && s_listening && !s_running && !s_over) {
        s_running = pet_app_link_start(s_link, s_give, false);
        s_piece_len = 0;
        s_idle_ticks = 0;
        if (s_running) {
            pet_app_exchange_told(PET_EXCHANGE_MOVING, 0, s_link);
        }
    }
}

static void link_dropped(void)
{
    if (board_lvgl_lock(1000)) {
        if (s_running) {
            pet_link_lost(s_link);
            s_running = false;
            s_over = true;
            pet_app_exchange_told(PET_EXCHANGE_ENDED, 0, s_link);
        }
        board_lvgl_unlock();
    }
    s_conn = NO_CONN;
    s_secure = s_listening = false;
    ble_gap_unpair(&s_peer);    /* nothing of the pairing is kept */
    if (!s_want) {
        stay_awake(false);      /* the screen was closed first, and the hanging up is now done */
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_adv = false;
        if (event->connect.status != 0 || !s_want || s_over) {
            if (event->connect.status == 0) {
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            return 0;
        }
        s_conn = event->connect.conn_handle;
        if (ble_gap_conn_find(s_conn, &desc) == 0) {
            s_peer = desc.peer_id_addr;
        }
        ble_gap_security_initiate(s_conn);     /* nothing is said until the code has been typed */
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        link_dropped();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_adv = false;
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io io = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = esp_random() % 1000000,
            };
            tell(PET_EXCHANGE_CODE, io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        } else {
            /* Any other way of pairing would not prove the caller can see this screen. */
            ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0 && ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
            desc.sec_state.encrypted && desc.sec_state.authenticated) {
            s_secure = true;
        } else {
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            s_listening = event->subscribe.cur_notify;
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* A pairing from an earlier exchange was left behind: forget it and pair afresh. */
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

/* The caller's half of the stream arrives as writes. */
static int on_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    static uint8_t buf[520];
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &n) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (!board_lvgl_lock(2000)) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    bool ok = s_link && conn_handle == s_conn;
    if (ok) {
        start_if_ready();
        ok = s_running;
    }
    if (ok) {
        pet_app_link_in(s_link, buf, n);
        s_idle_ticks = 0;
    }
    board_lvgl_unlock();
    return ok ? 0 : BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

/* Our half goes out as notifications; there is nothing to read. */
static int on_read(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = on_write,
                /* Refused until the link is encrypted with the code. */
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = on_read,
                .val_handle = &s_tx_handle,
                .flags = BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

/* Twenty times a second while the screen is open: the Bluetooth calls, made from a task of their own. */
static void tick(void *arg)
{
    if (!s_want) {
        return;
    }
    if (!s_adv && s_conn == NO_CONN && !s_over) {
        if (ble_gap_disc_active()) {
            ble_gap_disc_cancel();      /* a listening scan was under way: the exchange comes first */
            return;
        }
        advertise();
        return;
    }
    if (s_conn == NO_CONN) {
        return;
    }

    /* Send what the exchange has to say, a piece at a time. */
    bool ended = false;
    for (int sent = 0; sent < 8; sent++) {
        if (!board_lvgl_lock(20)) {
            return;
        }
        if (s_link && s_running) {
            if (s_piece_len == 0) {
                uint16_t mtu = ble_att_mtu(s_conn);
                size_t cap = mtu > 3 ? (size_t)mtu - 3 : 20;
                s_piece_len = (uint16_t)pet_link_out(s_link, s_piece, cap < PIECE_MAX ? cap : PIECE_MAX);
            }
            ended = s_piece_len == 0 && pet_link_state(s_link) != PET_LINK_RUNNING;
        }
        bool running = s_running;
        board_lvgl_unlock();
        if (!running || s_piece_len == 0) {
            break;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_piece, s_piece_len);
        if (!om || ble_gatts_notify_custom(s_conn, s_tx_handle, om) != 0) {
            break;      /* no room just now: the same piece again next time */
        }
        s_piece_len = 0;
        s_idle_ticks = 0;
    }

    /* Over, and everything said: a moment for the last piece to cross, then hang up. */
    if (ended && ++s_idle_ticks >= 10) {
        if (board_lvgl_lock(1000)) {
            s_running = false;
            s_over = true;
            pet_app_exchange_told(PET_EXCHANGE_ENDED, 0, s_link);
            board_lvgl_unlock();
        }
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
}

void exchange_init(void)
{
    parse_uuid(PET_LINK_SERVICE_UUID, &s_svc_uuid);
    parse_uuid(PET_LINK_TO_WAITER_UUID, &s_rx_uuid);
    parse_uuid(PET_LINK_FROM_WAITER_UUID, &s_tx_uuid);

    /*
     * Pairing: the watch shows a code and the caller types it, which
     * proves the caller is looking at this screen. It is called a bond so
     * that the caller's system reports the pairing as done, but only the
     * link's own key is exchanged (nothing that names either device), it
     * is held in memory only, and it is forgotten when the link ends.
     */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Baandwidth Farms");    /* the same on every watch */
    int rc = ble_gatts_count_cfg(SERVICES);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(SERVICES);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "service not registered: %d", rc);
    }
    ble_store_config_init();

    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "exchange", &s_awake_lock);

    const esp_timer_create_args_t args = {.callback = tick, .name = "exchange"};
    esp_timer_create(&args, &s_timer);
}

/* Called from the screen's task, with its lock held. */
bool exchange_ask(int what)
{
    if (!s_timer) {
        return false;
    }
    if (what == 0) {
        if (!s_want) {
            return true;
        }
        s_want = false;
        esp_timer_stop(s_timer);
        if (s_link && s_running) {
            pet_link_lost(s_link);
        }
        s_running = false;
        heap_caps_free(s_link);
        s_link = NULL;
        /* Hanging up and going quiet take no lock of ours, so they are safe from here. */
        bool hanging_up = s_conn != NO_CONN &&
                          ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM) == 0;
        if (ble_gap_adv_active()) {
            ble_gap_adv_stop();
        }
        s_adv = false;
        if (!hanging_up) {
            stay_awake(false);  /* otherwise when the link has gone: see link_dropped() */
        }
        return true;
    }
    if (s_want) {
        return true;
    }
    s_link = heap_caps_malloc(sizeof(*s_link), MALLOC_CAP_SPIRAM);
    if (!s_link) {
        return false;
    }
    memset(s_link, 0, sizeof(*s_link));
    s_give = what == 2;
    s_over = s_running = s_secure = s_listening = false;
    s_piece_len = 0;
    s_want = true;
    stay_awake(true);
    if (esp_timer_start_periodic(s_timer, TICK_US) != ESP_OK) {
        s_want = false;
        stay_awake(false);
        heap_caps_free(s_link);
        s_link = NULL;
        return false;
    }
    return true;
}

bool exchange_busy(void)
{
    return s_want;
}
