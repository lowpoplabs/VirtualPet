/*
 * board.h implemented on Waveshare's board support packages.
 *
 * The 2.06 and 2.16 packages expose the same bsp_* calls (display, touch,
 * card, audio, I2C), so one file serves both; which package is linked is
 * decided in idf_component.yml. Their LVGL glue differs underneath
 * (esp_lvgl_port vs esp_lvgl_adapter), so only bsp_* calls are used here.
 */
#include "board.h"

#include <math.h>
#include <stdlib.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "sdmmc_cmd.h"
#include "sdkconfig.h"

#include "axp2101.h"
#include "pcf85063.h"
#include "qmi8658.h"

#define BEEP_SAMPLE_RATE 22050
#define BEEP_LEAD_MS     60
#define BEEP_TAIL_MS     250

static esp_codec_dev_handle_t s_speaker;

const char *board_name(void)
{
#if CONFIG_PET_BOARD_WATCH_206
    return "watch-2.06";
#else
    return "square-2.16";
#endif
}

/*
 * The board package draws into PSRAM, and the SPI driver then has to find
 * a block of internal RAM for every strip it sends. Once both radios have
 * chopped internal RAM up, the big strips of a full-screen redraw fail to
 * find one and are dropped, leaving parts of the old screen behind. So the
 * drawing buffer is replaced with a permanent internal one the driver can
 * send from directly, claimed here before the radios start.
 *
 * The board package tells LVGL a strip is done as soon as it is queued,
 * which was safe only because of those copies. Sending straight from the
 * one buffer, LVGL must not draw the next strip until this one has left.
 * The panel driver finishes all queued pixel data before it sends any
 * command, so re-sending the current brightness after each strip is the
 * wait.
 */
#define DRAW_STRIP_LINES 24

static int s_brightness = 100;

#if CONFIG_PET_BOARD_WATCH_206
static void strip_sent_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    bsp_display_brightness_set(s_brightness);
}
#endif

/* LVGL's clock, read when asked. */
static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void board_touch_poll(void)
{
    lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, NULL);
}

/*
 * Reading the touch panel. The board package's own reader stops the whole
 * program if the panel does not answer, and an untouched panel dozes and
 * does not answer. That never showed while it was only read after its
 * interrupt; asked on a schedule (board_touch_poll) it happens constantly.
 * Here no answer simply means "not touched".
 *
 * The first fields of the package's private record for a touch device,
 * which it hangs on the LVGL input device (esp_lvgl_port_touch.c).
 */
typedef struct {
    esp_lcd_touch_handle_t handle;
    lv_indev_t *indev;
    struct {
        float x, y;
    } scale;
} port_touch_t;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    const port_touch_t *t = lv_indev_get_driver_data(indev);
    esp_lcd_touch_point_data_t p[1] = {0};
    uint8_t n = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    if (t && t->handle && esp_lcd_touch_read_data(t->handle) == ESP_OK &&
        esp_lcd_touch_get_data(t->handle, p, &n, 1) == ESP_OK && n > 0) {
        data->point.x = (int32_t)(t->scale.x * p[0].x);
        data->point.y = (int32_t)(t->scale.y * p[0].y);
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

static void touch_be_forgiving(void)
{
    for (lv_indev_t *in = lv_indev_get_next(NULL); in; in = lv_indev_get_next(in)) {
        if (lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER && lv_indev_get_driver_data(in)) {
            lv_indev_set_read_cb(in, touch_read_cb);
        }
    }
    /* A dozing panel is not an error worth a line ten times a second. */
    esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_NONE);
    esp_log_level_set("FT5x06", ESP_LOG_NONE);
}

esp_err_t board_display_start(void)
{
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        return ESP_FAIL;
    }
    /*
     * The board package keeps LVGL's clock with a timer that fires every
     * 5 ms, which would wake a sleeping chip 200 times a second. LVGL reads
     * the clock itself instead, and that timer is stopped (stopping it also
     * switches LVGL's own timers off, so those are switched back on).
     */
    if (bsp_display_lock(1000)) {
        lv_tick_set_cb(lvgl_tick_cb);
        lvgl_port_stop();
        lv_timer_enable(true);
        touch_be_forgiving();
        bsp_display_unlock();
    }
#if CONFIG_PET_BOARD_WATCH_206
    uint32_t bytes = (uint32_t)BSP_LCD_H_RES * DRAW_STRIP_LINES * 2;
    void *buf = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf && bsp_display_lock(1000)) {
        lv_display_set_buffers(disp, buf, NULL, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp, strip_sent_cb, LV_EVENT_FLUSH_FINISH, NULL);
        bsp_display_unlock();
    } else {
        free(buf);
        return ESP_ERR_NO_MEM;
    }
#endif
    return ESP_OK;
}

bool board_lvgl_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void board_lvgl_unlock(void)
{
    bsp_display_unlock();
}

esp_err_t board_set_brightness(int percent)
{
    s_brightness = percent;
    return bsp_display_brightness_set(percent);
}

esp_err_t board_sdcard_mount(void)
{
    return bsp_sdcard_mount();
}

/*
 * Lending the card to a computer.
 *
 * The chip has one USB port. Normally it is the serial port used for
 * flashing and logs; while the card is lent it becomes a USB drive
 * instead, and the pet must not touch the card. Taking it back restarts
 * the device, which is the simple, sure way to return the port and the
 * card to how they were.
 */
#if CONFIG_PET_BOARD_WATCH_206
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/rtc_cntl_struct.h"

/*
 * Which side of the chip owns the USB pins is kept in a register that a
 * restart does not clear, so after lending the card the port would stay
 * dead. This points the pins back at the serial side, and drops off the
 * bus for a moment so the computer notices the change.
 */
static void usb_port_to_serial(void)
{
    const usb_serial_jtag_pull_override_vals_t off = {0};
    usb_serial_jtag_ll_phy_enable_pull_override(&off);
    usb_serial_jtag_ll_phy_enable_external(false);
    vTaskDelay(pdMS_TO_TICKS(300));
    usb_serial_jtag_ll_phy_disable_pull_override();
    USB_SERIAL_JTAG.conf0.dp_pullup = 1;    /* as it is after power-on */
}

static bool s_card_lent;

bool board_card_can_lend(void)
{
    return true;
}

bool board_card_lend(void)
{
    static sdmmc_card_t card;
    if (s_card_lent) {
        return true;
    }
    /* The board package's unmount also shuts the card down, so it is started again here. */
    bsp_sdcard_unmount();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = BSP_SD_CLK;
    slot.cmd = BSP_SD_CMD;
    slot.d0 = BSP_SD_D0;
    slot.width = 1;
    esp_err_t err = host.init();
    if (err == ESP_OK) {
        err = sdmmc_host_init_slot(host.slot, &slot);
    }
    if (err == ESP_OK) {
        err = sdmmc_card_init(&host, &card);
    }
    if (err == ESP_OK) {
        const tinyusb_msc_sdmmc_config_t msc = {.card = &card};
        err = tinyusb_msc_storage_init_sdmmc(&msc);
    }
    if (err == ESP_OK) {
        const tinyusb_config_t usb = {0};
        err = tinyusb_driver_install(&usb);
    }
    if (err != ESP_OK) {
        ESP_LOGW("board", "card not lent: %s", esp_err_to_name(err));
        tinyusb_msc_storage_deinit();
        sdmmc_host_deinit();
        bsp_sdcard_mount();
        return false;
    }
    s_card_lent = true;
    return true;
}

void board_card_take_back(void)
{
    if (s_card_lent) {
        /*
         * The USB driver is not shut down first: its uninstall can hang, and
         * the restart clears it anyway. Moving the pins is all that matters.
         */
        usb_port_to_serial();
        esp_restart();
    }
}
#else
bool board_card_can_lend(void) { return false; }
bool board_card_lend(void) { return false; }
void board_card_take_back(void) {}
#endif

static bool s_motion_ok;

esp_err_t board_power_init(void)
{
#if CONFIG_PET_BOARD_WATCH_206
    /* Restarted some other way while the card was lent (a crash, the reset button). */
    if (RTCCNTL.usb_conf.sw_usb_phy_sel) {
        usb_port_to_serial();
    }
#endif
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        return err;
    }
    /* The clock chip shares the bus; it failing is reported by board_clock_read. */
    pcf85063_init(bsp_i2c_get_handle());
    /* So does the motion sensor; without it the pet just does not know how it is carried. */
    s_motion_ok = qmi8658_init(bsp_i2c_get_handle()) == ESP_OK;
    return axp2101_init(bsp_i2c_get_handle());
}

bool board_motion_read(int mg[3])
{
    if (!s_motion_ok || qmi8658_read_mg(mg) != ESP_OK) {
        return false;
    }
    /* The chip sits with its z axis pointing out of the back; the pet wants it out of the screen. */
    mg[2] = -mg[2];
    return true;
}

esp_err_t board_clock_read(struct tm *out)
{
    return pcf85063_get(out);
}

esp_err_t board_clock_set(const struct tm *in)
{
    return pcf85063_set(in);
}

int board_battery_percent(void)
{
    return axp2101_battery_percent();
}

bool board_usb_powered(void)
{
    return axp2101_vbus_present();
}

/*
 * Buttons. BOOT is GPIO0 on both boards, pressed = low. On the watch the
 * PWR key also reaches the chip, on GPIO10, pressed = high (from
 * Waveshare's factory ButtonTest); it has no internal pull, so none is
 * enabled here. The square board's second key is not confirmed, so only
 * BOOT is used there.
 */
#define BUTTON_BOOT_GPIO GPIO_NUM_0
#if CONFIG_PET_BOARD_WATCH_206
#define BUTTON_PWR_GPIO  GPIO_NUM_10
#endif

void board_buttons_init(void)
{
    gpio_config_t boot = {
        .pin_bit_mask = 1ULL << BUTTON_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot);
#ifdef BUTTON_PWR_GPIO
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << BUTTON_PWR_GPIO,
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&pwr);
#endif
}

bool board_button_down(void)
{
    if (gpio_get_level(BUTTON_BOOT_GPIO) == 0) {
        return true;
    }
#ifdef BUTTON_PWR_GPIO
    if (gpio_get_level(BUTTON_PWR_GPIO) == 1) {
        return true;
    }
#endif
    return false;
}

#if CONFIG_PET_BOARD_WATCH_206
/*
 * The speaker, set up here rather than by the board package. The package
 * opens a microphone channel alongside it and leaves that running for good,
 * and a running audio channel keeps the chip from ever sleeping. The pet
 * only plays, so only the playing half is made, and it runs just while a
 * sound does (the codec library starts and stops it).
 */
static esp_codec_dev_handle_t speaker_init(void)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &tx, NULL) != ESP_OK) {
        return NULL;
    }
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BEEP_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = GPIO_NUM_NC,
        },
    };
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK) {
        i2s_del_channel(tx);
        return NULL;
    }
    const audio_codec_i2s_cfg_t i2s_cfg = {.port = CONFIG_BSP_I2S_NUM, .tx_handle = tx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!data_if || !ctrl_if) {
        return NULL;
    }
    const es8311_codec_cfg_t es8311 = {
        .ctrl_if = ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .use_mclk = true,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es8311);
    if (!codec) {
        return NULL;
    }
    const esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data_if};
    return esp_codec_dev_new(&dev);
}
#else
#define speaker_init bsp_audio_codec_speaker_init
#endif

esp_err_t board_play(const pet_sound_t *sound)
{
    if (!s_speaker) {
        s_speaker = speaker_init();
        if (!s_speaker) {
            return ESP_FAIL;
        }
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BEEP_SAMPLE_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    esp_codec_dev_set_out_vol(s_speaker, 75);

    /*
     * The write returns once samples are queued, not once they are heard,
     * and closing mutes the amplifier at once. So the tone is wrapped in
     * silence: a lead-in while the amplifier wakes, and a tail long enough
     * to push the whole sound out of the I2S buffers before the close.
     */
    int lead = BEEP_SAMPLE_RATE * BEEP_LEAD_MS / 1000;
    int tail = BEEP_SAMPLE_RATE * BEEP_TAIL_MS / 1000;
    int body = 0;
    for (int i = 0; i < sound->count; i++) {
        body += BEEP_SAMPLE_RATE * sound->notes[i].ms / 1000;
    }
    int n = lead + body + tail;
    int16_t *buf = heap_caps_calloc(n, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!buf) {
        esp_codec_dev_close(s_speaker);
        return ESP_ERR_NO_MEM;
    }
    float amp = 200.0f * sound->volume;
    int at = lead;
    for (int i = 0; i < sound->count; i++) {
        int len = BEEP_SAMPLE_RATE * sound->notes[i].ms / 1000;
        int hz = sound->notes[i].hz;
        for (int k = 0; k < len && hz; k++) {
            /* Short fade at each end of a note so it does not click. */
            int edge = k < len - k ? k : len - k;
            float env = edge < 80 ? edge / 80.0f : 1.0f;
            buf[at + k] = (int16_t)(amp * env * sinf(2.0f * (float)M_PI * hz * k / BEEP_SAMPLE_RATE));
        }
        at += len;
    }
    int rc = esp_codec_dev_write(s_speaker, buf, n * sizeof(int16_t));
    free(buf);
    esp_codec_dev_close(s_speaker);
    return rc == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
