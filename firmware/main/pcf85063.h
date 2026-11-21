/* Minimal PCF85063 clock chip access on the board's shared I2C bus. */
#pragma once

#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus);

/* ESP_ERR_INVALID_STATE means the chip lost power and its time is not valid. */
esp_err_t pcf85063_get(struct tm *out);
esp_err_t pcf85063_set(const struct tm *in);
