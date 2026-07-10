#pragma once

#include "sensors/sensor_readings.h"
#include "pico/types.h"

namespace hydro {

class TdsSensor {
public:
    TdsSensor(uint adc_pin, uint adc_input);

    void init();
    TdsReading read(float water_temperature_c);

private:
    uint adc_pin_;
    uint adc_input_;
};

}
