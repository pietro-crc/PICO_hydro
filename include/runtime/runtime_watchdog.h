#pragma once

#include "config/hardware_config.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

namespace hydro::runtime {

inline void enable_watchdog() {
    if (config::ENABLE_HARDWARE_WATCHDOG) {
        watchdog_enable(config::HARDWARE_WATCHDOG_TIMEOUT_MS, true);
    }
}

inline void feed_watchdog() {
    if (config::ENABLE_HARDWARE_WATCHDOG) {
        watchdog_update();
    }
}

inline void sleep_ms_guarded(uint32_t duration_ms) {
    const uint64_t deadline_ms = to_ms_since_boot(get_absolute_time()) + duration_ms;

    while (true) {
        feed_watchdog();

        const uint64_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms >= deadline_ms) {
            return;
        }

        const uint64_t remaining_ms = deadline_ms - now_ms;
        const uint32_t sleep_slice_ms = remaining_ms > 250
            ? 250
            : (uint32_t)remaining_ms;
        sleep_ms(sleep_slice_ms == 0 ? 1 : sleep_slice_ms);
    }
}

}
