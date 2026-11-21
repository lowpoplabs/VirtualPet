#include "qmi8658.h"

#define REG_WHO_AM_I 0x00
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03   /* accelerometer range and rate */
#define REG_CTRL7    0x08   /* which sensors run */
#define REG_AX_L     0x35   /* then AX_H, AY_L, AY_H, AZ_L, AZ_H */

#define WHO_AM_I_VALUE   0x05
#define CTRL1_AUTO_INC   0x40   /* register address steps on during a read */
/* +-4 g, and the low-power 21 Hz rate: the gyroscope stays off, which is most of the chip's draw. */
#define CTRL2_4G_LP_21HZ 0x1D
#define CTRL7_ACCEL_ONLY 0x01
#define COUNTS_PER_G     8192   /* at +-4 g */

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t w[2] = {reg, value};
    return i2c_master_transmit(s_dev, w, sizeof(w), 100);
}

esp_err_t qmi8658_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    /* The address depends on how one pin is tied; Waveshare uses the first of these. */
    static const uint8_t ADDRS[] = {0x6B, 0x6A};
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < sizeof(ADDRS) && err != ESP_OK; i++) {
        if (i2c_master_probe(bus, ADDRS[i], 100) != ESP_OK) {
            continue;
        }
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = ADDRS[i],
            .scl_speed_hz = 400000,
        };
        err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    }
    if (err != ESP_OK) {
        return err;
    }
    uint8_t reg = REG_WHO_AM_I, id = 0;
    err = i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, 100);
    if (err == ESP_OK && id != WHO_AM_I_VALUE) {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err == ESP_OK) {
        err = write_reg(REG_CTRL1, CTRL1_AUTO_INC);
    }
    if (err == ESP_OK) {
        err = write_reg(REG_CTRL2, CTRL2_4G_LP_21HZ);
    }
    if (err == ESP_OK) {
        err = write_reg(REG_CTRL7, CTRL7_ACCEL_ONLY);
    }
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    return err;
}

esp_err_t qmi8658_read_mg(int mg[3])
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = REG_AX_L, r[6];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, r, sizeof(r), 100);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 3; i++) {
        int16_t raw = (int16_t)(r[2 * i] | (r[2 * i + 1] << 8));
        mg[i] = raw * 1000 / COUNTS_PER_G;
    }
    return ESP_OK;
}
