#pragma once

#include "sensors/sensor_readings.h"
#include "pico/types.h"

namespace hydro {

class DhtSensor {
public:
    explicit DhtSensor(uint pin);

    void init();
    bool read(DhtReading *reading);

private:
    bool wait_for_level(bool level, uint32_t timeout_us);

    uint pin_;
};

}
