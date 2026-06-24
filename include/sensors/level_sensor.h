#pragma once

#include "pico/types.h"

namespace hydro {

class LevelSensor {
public:
    explicit LevelSensor(uint pin);

    void init();
    bool water_present() const;

private:
    uint pin_;
};

}
