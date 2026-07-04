#pragma once

#include "sensors/sensor_readings.h"
#include "config/hardware_config.h"
#include "pico/types.h"

#include <cstdint>

namespace hydro {

struct Ds18b20Address {
    uint8_t bytes[8];
};

class Ds18b20Sensor {
public:
    explicit Ds18b20Sensor(uint pin);

    void init();
    uint8_t discover_devices();
    uint8_t device_count() const;
    const Ds18b20Address &device_address(uint8_t index) const;
    bool start_temperature_conversion();
    WaterTemperatureStatus last_status() const;
    WaterTemperatureReading read_converted_temperature_c(uint8_t device_index);
    WaterTemperatureReading read_converted_temperature_c();

private:
    bool reset_bus();
    void write_bit(bool value);
    bool read_bit();
    void write_byte(uint8_t value);
    uint8_t read_byte();
    void write_address(const Ds18b20Address &address);
    static uint8_t crc8(const uint8_t *data, uint8_t length);

    uint pin_;
    uint8_t device_count_ = 0;
    Ds18b20Address devices_[config::MAX_WATER_TEMPERATURE_SENSORS] = {};
    WaterTemperatureStatus last_status_ = WaterTemperatureStatus::not_read;
};

}
