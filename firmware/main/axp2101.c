#include "axp2101.h"

#define AXP2101_ADDR        0x34
#define REG_STATUS1         0x00   /* bit5: VBUS good, bit3: battery present */
#define REG_BAT_PERCENT     0xA4   /* fuel gauge, 0-100 */
#define STATUS1_VBUS_GOOD   (1 << 5)
#define STATUS1_BAT_PRESENT (1 << 3)

static i2c_master_dev_handle_t s_dev;

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

esp_err_t axp2101_init(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = i2c_master_probe(bus, AXP2101_ADDR, 100);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

int axp2101_battery_percent(void)
{
    uint8_t status, pct;
    if (read_reg(REG_STATUS1, &status) != ESP_OK || !(status & STATUS1_BAT_PRESENT)) {
        return -1;
    }
    if (read_reg(REG_BAT_PERCENT, &pct) != ESP_OK || pct > 100) {
        return -1;
    }
    return pct;
}

bool axp2101_vbus_present(void)
{
    uint8_t status;
    return read_reg(REG_STATUS1, &status) == ESP_OK && (status & STATUS1_VBUS_GOOD);
}
