#include "sensors/level_sensor.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"

namespace hydro {

LevelSensor::LevelSensor(uint pin) : pin_(pin) {}

void LevelSensor::init() {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
}

bool LevelSensor::water_present() const {
    bool level_high = gpio_get(pin_);
    return config::LEVEL_WATER_PRESENT_WHEN_HIGH ? level_high : !level_high;
}

}
