/*
 * The one interface the rest of the firmware sees. Each board has its own
 * implementation (board_watch_206.c, board_square_216.c); the differences
 * between them (panel size, touch chip) stop here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"
#include "pet_sound.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Short name for logs, e.g. "watch-2.06". */
const char *board_name(void);

/* Brings up the display, touch and LVGL task. Must be called first. */
esp_err_t board_display_start(void);

/* LVGL is not thread-safe: hold this around any lv_* call outside LVGL's task. */
bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

/* 0-100 */
esp_err_t board_set_brightness(int percent);

/*
 * Asks the screen's task to read the touch panel now. Touch is normally
 * announced by an interrupt, which a sleeping chip does not hear; while
 * sleep is allowed the panel is asked instead.
 */
void board_touch_poll(void);

/* Mounts the microSD card at /sdcard. Fails cleanly when no card is in. */
esp_err_t board_sdcard_mount(void);

/*
 * Lends the card to a computer as a USB drive. The USB port stops being
 * the serial port until board_card_take_back, which restarts the device.
 * Close every file on the card first. False if the board cannot do it or
 * no card is in; the card is then mounted again as before.
 */
bool board_card_can_lend(void);
bool board_card_lend(void);
void board_card_take_back(void);

/* Power chip. battery_percent returns -1 when it cannot be read. */
esp_err_t board_power_init(void);
int board_battery_percent(void);
bool board_usb_powered(void);

/*
 * Clock chip. board_clock_read fails with ESP_ERR_INVALID_STATE when the
 * chip lost power and its time cannot be trusted. Call after board_power_init.
 */
esp_err_t board_clock_read(struct tm *out);
esp_err_t board_clock_set(const struct tm *in);

/*
 * Motion sensor. board_motion_read gives acceleration on three axes in
 * thousandths of g, and is false when the board has no sensor or it did
 * not answer. Call after board_power_init.
 */
bool board_motion_read(int mg[3]);

/*
 * Side buttons. board_buttons_init sets the pins up; board_button_down is
 * true while any of them is held.
 */
void board_buttons_init(void);
bool board_button_down(void);

/* Plays a sound through the speaker. Blocks until it has finished. */
esp_err_t board_play(const pet_sound_t *sound);

#ifdef __cplusplus
}
#endif
