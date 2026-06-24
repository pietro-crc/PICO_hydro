#pragma once

#include "hardware/i2c.h"
#include "sensors/sensor_readings.h"

#include <cstdint>

namespace hydro {

class Veml7700Sensor {
public:
    explicit Veml7700Sensor(i2c_inst_t *i2c);

    bool init();
    bool read(VemlReading *reading);

private:
    bool write_register(uint8_t reg, uint16_t value);
    bool read_register(uint8_t reg, uint16_t *value);

    i2c_inst_t *i2c_;
};

}
