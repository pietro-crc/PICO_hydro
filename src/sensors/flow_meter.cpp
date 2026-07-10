#include "sensors/flow_meter.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace hydro {
namespace {

volatile uint32_t flow_pulse_count = 0;
volatile uint32_t last_pulse_us = 0;
uint active_flow_pin = 0;

void flow_sensor_callback(uint gpio, uint32_t events) {
    if (gpio != active_flow_pin) {
        return;
    }

    if ((events & GPIO_IRQ_EDGE_RISE) == 0) {
        return;
    }

    uint32_t now_us = time_us_32();
    if ((uint32_t)(now_us - last_pulse_us) >= config::MIN_PULSE_DISTANCE_US) {
        flow_pulse_count++;
        last_pulse_us = now_us;
    }
}

}

FlowMeter::FlowMeter(uint pin) : pin_(pin) {}

void FlowMeter::init() {
    uint32_t interrupt_state = save_and_disable_interrupts();
    flow_pulse_count = 0;
    last_pulse_us = 0;
    restore_interrupts(interrupt_state);

    active_flow_pin = pin_;
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    gpio_disable_pulls(pin_);
    gpio_set_irq_enabled_with_callback(pin_, GPIO_IRQ_EDGE_RISE, true, &flow_sensor_callback);
}

uint32_t FlowMeter::get_and_reset_pulses() {
    uint32_t interrupt_state = save_and_disable_interrupts();
    uint32_t pulses = flow_pulse_count;
    flow_pulse_count = 0;
    restore_interrupts(interrupt_state);
    return pulses;
}

}
