#include "pcf85063.h"

#include <string.h>

#define PCF85063_ADDR 0x51
#define REG_SECONDS   0x04   /* then minutes, hours, day, weekday, month, year */
#define SECONDS_OS    0x80   /* oscillator stopped: time is not valid */

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t pcf85063_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = i2c_master_probe(bus, PCF85063_ADDR, 100);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t pcf85063_get(struct tm *out)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = REG_SECONDS, r[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) {
        return err;
    }
    if (r[0] & SECONDS_OS) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(out, 0, sizeof(*out));
    out->tm_sec = bcd2bin(r[0] & 0x7F);
    out->tm_min = bcd2bin(r[1] & 0x7F);
    out->tm_hour = bcd2bin(r[2] & 0x3F);
    out->tm_mday = bcd2bin(r[3] & 0x3F);
    out->tm_wday = r[4] & 0x07;
    out->tm_mon = bcd2bin(r[5] & 0x1F) - 1;
    out->tm_year = bcd2bin(r[6]) + 100;   /* chip stores 00-99, meaning 2000-2099 */
    out->tm_isdst = -1;
    return ESP_OK;
}

esp_err_t pcf85063_set(const struct tm *in)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Writing seconds with the top bit clear also clears the "time not valid" flag. */
    const uint8_t w[8] = {
        REG_SECONDS,
        bin2bcd(in->tm_sec),
        bin2bcd(in->tm_min),
        bin2bcd(in->tm_hour),
        bin2bcd(in->tm_mday),
        (uint8_t)(in->tm_wday & 0x07),
        bin2bcd(in->tm_mon + 1),
        bin2bcd(in->tm_year % 100),
    };
    return i2c_master_transmit(s_dev, w, sizeof(w), 100);
}
