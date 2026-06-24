#pragma once

#include "sensors/sensor_readings.h"
#include "pico/types.h"

#include <cstdint>

namespace hydro {

class Ds18b20Sensor {
public:
    explicit Ds18b20Sensor(uint pin);

    void init();
    bool detect_device();
    bool start_temperature_conversion();
    WaterTemperatureStatus last_status() const;
    WaterTemperatureReading read_converted_temperature_c();
    WaterTemperatureReading read_temperature_c();

private:
    bool reset_bus();
    void write_bit(bool value);
    bool read_bit();
    void write_byte(uint8_t value);
    uint8_t read_byte();
    static uint8_t crc8(const uint8_t *data, uint8_t length);

    uint pin_;
    WaterTemperatureStatus last_status_ = WaterTemperatureStatus::not_read;
};

}
