#include "app/hydro_controller.h"
#include "config/hardware_config.h"
#include "drivers/relay.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

namespace {

hydro::Relay pump_relay(
    hydro::config::RELAY_PIN,
    hydro::config::RELAY_ACTIVE_LOW
);

void relay_on() {
    pump_relay.on();
}

void relay_off() {
    pump_relay.off();
}

void init_status_led() {
    gpio_init(hydro::config::LED_PIN);
    gpio_set_dir(hydro::config::LED_PIN, GPIO_OUT);
    gpio_put(hydro::config::LED_PIN, 0);
}

void set_status_led(bool enabled) {
    gpio_put(hydro::config::LED_PIN, enabled ? 1 : 0);
}

}

int main() {
    if (hydro::config::ENABLE_RELAY_TEST_LOOP) {
        stdio_init_all();
        init_status_led();
        pump_relay.init();

        while (true) {
            relay_on();
            set_status_led(true);
            sleep_ms(5000);

            relay_off();
            set_status_led(false);
            sleep_ms(10000);
        }
    }

    hydro::HydroController controller;
    controller.init();
    controller.run_forever();
}
