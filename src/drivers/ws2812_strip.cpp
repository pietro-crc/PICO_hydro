#include "drivers/ws2812_strip.h"

#include "config/hardware_config.h"
#include "pico/stdlib.h"
#include "runtime/runtime_watchdog.h"
#include "ws2812.pio.h"

namespace hydro {

uint32_t ws2812_color(uint8_t red, uint8_t green, uint8_t blue) {
    return ((uint32_t)green << 16) | ((uint32_t)red << 8) | blue;
}

void Ws2812Strip::init(PIO pio, uint pin, uint led_count) {
    pio_ = pio;
    led_count_ = led_count;

    const uint offset = pio_add_program(pio_, &ws2812_program);
    sm_ = pio_claim_unused_sm(pio_, true);
    ws2812_program_init(pio_, sm_, offset, pin, 800000, false);
}

void Ws2812Strip::put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio_, sm_, pixel_grb << 8u);
}

void Ws2812Strip::set_all(uint32_t color) {
    for (uint i = 0; i < led_count_; ++i) {
        put_pixel(color);
    }
}

void Ws2812Strip::set_flow_and_level(uint32_t flow_color, uint32_t level_color) {
    for (uint i = 0; i < 6 && i < led_count_; ++i) {
        put_pixel(ws2812_color(0, 0, 0));
    }

    if (led_count_ >= 7) {
        put_pixel(flow_color);
    }

    if (led_count_ >= 8) {
        put_pixel(level_color);
    }
}

void Ws2812Strip::test_startup() {
    for (uint i = 0; i < 3; ++i) {
        set_all(ws2812_color(0, 0, 40));
        runtime::sleep_ms_guarded(300);
        set_all(ws2812_color(0, 0, 0));
        runtime::sleep_ms_guarded(300);
    }
}

}
