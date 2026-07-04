#include "drivers/esp8266_at.h"

#include "config/hardware_config.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace hydro {
namespace {

constexpr uint32_t ESP8266_FIRST_AT_TIMEOUT_MS = 500;
constexpr uint32_t ESP8266_AT_TIMEOUT_MS = 800;
constexpr uint32_t ESP8266_GMR_TIMEOUT_MS = 1200;
constexpr uint32_t ESP8266_AP_TIMEOUT_MS = 2500;
constexpr uint32_t ESP8266_DRAIN_TIMEOUT_MS = 50;
constexpr uint32_t ESP8266_COMMON_BAUDRATES[] = {115200, 9600, 57600, 38400, 19200};

}

Esp8266At::Esp8266At(uart_inst_t *uart, uint tx_pin, uint rx_pin)
    : uart_(uart), tx_pin_(tx_pin), rx_pin_(rx_pin) {}

void Esp8266At::init(uint32_t baudrate) {
    baudrate_ = uart_init(uart_, baudrate);
    gpio_set_function(tx_pin_, GPIO_FUNC_UART);
    gpio_set_function(rx_pin_, GPIO_FUNC_UART);
    gpio_pull_up(rx_pin_);
    drain_rx();
}

Esp8266Diagnostic Esp8266At::run_startup_diagnostic() {
    Esp8266Diagnostic diagnostic = {
        true,
        false,
        false,
        false,
        false,
        0,
        Esp8266Status::not_checked
    };

    bool at_ok = false;
    for (uint32_t baudrate : ESP8266_COMMON_BAUDRATES) {
        if (probe_at(baudrate)) {
            at_ok = true;
            diagnostic.baudrate = baudrate_;
            break;
        }
    }

    if (!at_ok) {
        last_status_ = Esp8266Status::uart_timeout;
        diagnostic.status = last_status_;
        return diagnostic;
    }

    diagnostic.module_present = true;
    last_status_ = Esp8266Status::at_ok;
    diagnostic.status = last_status_;

    if (send_command("ATE0", "OK", ESP8266_AT_TIMEOUT_MS)) {
        diagnostic.echo_disabled = true;
        last_status_ = Esp8266Status::echo_off_ok;
        diagnostic.status = last_status_;
    }

    if (send_command("AT+GMR", "OK", ESP8266_GMR_TIMEOUT_MS)) {
        diagnostic.firmware_queried = true;
        last_status_ = Esp8266Status::firmware_ok;
        diagnostic.status = last_status_;
    }

    if (config::ENABLE_ESP8266_SOFT_AP &&
        configure_open_access_point(config::ESP8266_AP_SSID, config::ESP8266_AP_CHANNEL)) {
        diagnostic.access_point_enabled = true;
        last_status_ = Esp8266Status::ap_ready;
        diagnostic.status = last_status_;
    }

    return diagnostic;
}

bool Esp8266At::probe_at(uint32_t baudrate) {
    baudrate_ = uart_set_baudrate(uart_, baudrate);
    drain_rx();

    bool at_ok = send_command("AT", "OK", ESP8266_FIRST_AT_TIMEOUT_MS);
    if (at_ok) {
        return true;
    }

    sleep_ms(150);
    return send_command("AT", "OK", ESP8266_AT_TIMEOUT_MS);
}

bool Esp8266At::configure_open_access_point(const char *ssid, uint8_t channel) {
    if (!send_command("AT+CWMODE=2", "OK", ESP8266_AP_TIMEOUT_MS)) {
        return false;
    }

    last_status_ = Esp8266Status::ap_mode_ok;

    char command[96];
    int length = std::snprintf(
        command,
        sizeof(command),
        "AT+CWSAP=\"%s\",\"\",%u,0",
        ssid,
        channel
    );

    if (length <= 0 || (size_t)length >= sizeof(command)) {
        return false;
    }

    return send_command(command, "OK", ESP8266_AP_TIMEOUT_MS);
}

bool Esp8266At::send_command(const char *command, const char *expected, uint32_t timeout_ms) {
    drain_rx();
    write_command(command);
    return read_until(expected, timeout_ms);
}

bool Esp8266At::read_until(const char *expected, uint32_t timeout_ms) {
    char response[256] = {0};
    size_t used = 0;
    uint64_t deadline_ms = to_ms_since_boot(get_absolute_time()) + timeout_ms;

    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        while (to_ms_since_boot(get_absolute_time()) < deadline_ms && uart_is_readable(uart_)) {
            char value = (char)uart_getc(uart_);
            if (used < sizeof(response) - 1) {
                response[used++] = value;
                response[used] = '\0';
            }

            if (std::strstr(response, expected) != nullptr) {
                return true;
            }

            if (std::strstr(response, "ERROR") != nullptr || std::strstr(response, "FAIL") != nullptr) {
                return false;
            }
        }

        tight_loop_contents();
    }

    return false;
}

void Esp8266At::write_command(const char *command) {
    uart_puts(uart_, command);
    uart_puts(uart_, "\r\n");
}

void Esp8266At::drain_rx() {
    uint64_t deadline_ms = to_ms_since_boot(get_absolute_time()) + ESP8266_DRAIN_TIMEOUT_MS;

    while (to_ms_since_boot(get_absolute_time()) < deadline_ms && uart_is_readable(uart_)) {
        (void)uart_getc(uart_);
    }
}

}
