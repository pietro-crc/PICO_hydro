#pragma once

#include <cstdint>

namespace hydro {

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

}
