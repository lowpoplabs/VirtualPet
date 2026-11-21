/*
 * Minimal read-only AXP2101 power chip access: just what the pet needs
 * (battery level, USB present). Uses the board's shared I2C bus.
 */
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t axp2101_init(i2c_master_bus_handle_t bus);

/* 0-100, or -1 if the chip did not answer or no battery is attached. */
int axp2101_battery_percent(void);

bool axp2101_vbus_present(void);
