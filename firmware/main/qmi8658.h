/*
 * QMI8658 motion sensor, used as an accelerometer only. Just enough to
 * tell a wrist raise, being carried and lying still apart.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

/* Fails cleanly when the chip is not on the bus. */
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus);

/* Acceleration on the chip's three axes, in thousandths of g. */
esp_err_t qmi8658_read_mg(int mg[3]);
