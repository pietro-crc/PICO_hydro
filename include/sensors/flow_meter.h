#pragma once

#include "pico/types.h"

#include <cstdint>

namespace hydro {

class FlowMeter {
public:
    explicit FlowMeter(uint pin);

    void init();
    uint32_t get_and_reset_pulses();

private:
    uint pin_;
};

}
