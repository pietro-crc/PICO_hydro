#include "sensors/veml7700_sensor.h"

#include "config/hardware_config.h"

namespace hydro {

Veml7700Sensor::Veml7700Sensor(i2c_inst_t *i2c) : i2c_(i2c) {}

bool Veml7700Sensor::write_register(uint8_t reg, uint16_t value) {
    uint8_t buffer[3] = {
        reg,
        (uint8_t)(value & 0xFF),
        (uint8_t)((value >> 8) & 0xFF)
    };

    int result = i2c_write_blocking(i2c_, config::VEML7700_ADDRESS, buffer, sizeof(buffer), false);
    return result == (int)sizeof(buffer);
}

bool Veml7700Sensor::read_register(uint8_t reg, uint16_t *value) {
    if (value == nullptr) {
        return false;
    }

    int write_result = i2c_write_blocking(i2c_, config::VEML7700_ADDRESS, &reg, 1, true);
    if (write_result != 1) {
        return false;
    }

    uint8_t buffer[2] = {0, 0};
    int read_result = i2c_read_blocking(i2c_, config::VEML7700_ADDRESS, buffer, sizeof(buffer), false);
    if (read_result != (int)sizeof(buffer)) {
        return false;
    }

    *value = (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
    return true;
}

bool Veml7700Sensor::init() {
    return write_register(config::VEML7700_REG_ALS_CONF, 0x0000);
}

bool Veml7700Sensor::read(VemlReading *reading) {
    if (reading == nullptr) {
        return false;
    }

    uint16_t raw = 0;
    if (!read_register(config::VEML7700_REG_ALS_DATA, &raw)) {
        reading->valid = false;
        return false;
    }

    reading->raw_als = raw;
    reading->lux = raw * config::VEML7700_LUX_PER_COUNT_GAIN_1_IT_100MS;
    reading->valid = true;
    return true;
}

}
