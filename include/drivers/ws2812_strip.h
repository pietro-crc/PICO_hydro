#pragma once

#include "hardware/pio.h"
#include "pico/types.h"

#include <cstdint>

namespace hydro {

uint32_t ws2812_color(uint8_t red, uint8_t green, uint8_t blue);

class Ws2812Strip {
public:
    void init(PIO pio, uint pin, uint led_count);
    void set_all(uint32_t color);
    void set_flow_and_level(uint32_t flow_color, uint32_t level_color);
    void test_startup();

private:
    void put_pixel(uint32_t pixel_grb);

    PIO pio_ = nullptr;
    uint sm_ = 0;
    uint led_count_ = 0;
};

}
