#pragma once

#include <cstdint>

namespace hydro {

enum class WaterTemperatureStatus : uint8_t {
    not_read,
    bus_stuck_low,
    device_missing,
    conversion_started,
    crc_error,
    invalid_data,
    valid
};

struct DhtReading {
    bool valid;
    float temperature_c;
    float humidity_percent;
};

struct VemlReading {
    bool valid;
    uint16_t raw_als;
    float lux;
};

struct TdsReading {
    bool valid;
    uint16_t raw_adc;
    float voltage;
    float ppm;
};

struct WaterTemperatureReading {
    bool valid;
    bool device_present;
    WaterTemperatureStatus status;
    float temperature_c;
};

}
