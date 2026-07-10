#pragma once

#include "pico/types.h"

namespace hydro {

class Relay {
public:
    Relay(uint pin, bool active_low);

    void init();
    void on();
    void off();

private:
    bool output_level_for(bool enabled) const;

    uint pin_;
    bool active_low_;
};

}
