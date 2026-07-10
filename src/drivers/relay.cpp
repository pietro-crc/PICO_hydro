#include "drivers/relay.h"

#include "hardware/gpio.h"

namespace hydro {

Relay::Relay(uint pin, bool active_low) : pin_(pin), active_low_(active_low) {}

void Relay::init() {
    gpio_init(pin_);
    off();
    gpio_set_dir(pin_, GPIO_OUT);
}

void Relay::on() {
    gpio_put(pin_, output_level_for(true));
}

void Relay::off() {
    gpio_put(pin_, output_level_for(false));
}

bool Relay::output_level_for(bool enabled) const {
    return active_low_ ? !enabled : enabled;
}

}
