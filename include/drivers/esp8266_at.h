#pragma once

#include "hardware/uart.h"
#include "pico/types.h"

#include <cstdint>

namespace hydro {

enum class Esp8266Status : uint8_t {
    disabled,
    not_checked,
    uart_timeout,
    at_ok,
    echo_off_ok,
    firmware_ok,
    ap_mode_ok,
    ap_ready
};

struct Esp8266Diagnostic {
    bool enabled;
    bool module_present;
    bool echo_disabled;
    bool firmware_queried;
    bool access_point_enabled;
    uint32_t baudrate;
    Esp8266Status status;
};

class Esp8266At {
public:
    Esp8266At(uart_inst_t *uart, uint tx_pin, uint rx_pin);

    void init(uint32_t baudrate);
    Esp8266Diagnostic run_startup_diagnostic();

private:
    bool probe_at(uint32_t baudrate);
    bool configure_open_access_point(const char *ssid, uint8_t channel);
    bool send_command(const char *command, const char *expected, uint32_t timeout_ms);
    bool read_until(const char *expected, uint32_t timeout_ms);
    void write_command(const char *command);
    void drain_rx();

    uart_inst_t *uart_;
    uint tx_pin_;
    uint rx_pin_;
    uint32_t baudrate_ = 0;
    Esp8266Status last_status_ = Esp8266Status::not_checked;
};

}
