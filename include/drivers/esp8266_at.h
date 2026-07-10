#pragma once

#include "hardware/uart.h"
#include "pico/types.h"

#include <cstddef>
#include <cstdint>

namespace hydro {

enum class Esp8266Status : uint8_t {
    disabled,
    not_checked,
    uart_timeout,
    at_ok,
    echo_off_ok,
    firmware_ok,
    wifi_join_ok,
    wifi_join_failed,
    clock_sync_failed,
    tls_trust_store_missing,
    ssl_open_ok,
    ssl_failed,
    http_send_ok,
    http_send_failed
};

enum class Esp8266TransportError : uint8_t {
    none,
    invalid_argument,
    tcp_setup_failed,
    tcp_open_failed,
    send_prompt_failed,
    send_confirm_failed,
    receive_length_query_failed,
    receive_prefix_timeout,
    receive_header_invalid,
    receive_payload_timeout,
    receive_status_failed
};

struct Esp8266Diagnostic {
    bool enabled;
    bool module_present;
    bool echo_disabled;
    bool firmware_queried;
    uint32_t baudrate;
    int16_t wifi_join_error_code;
    int16_t wifi_state;
    bool wifi_ap_seen;
    bool ssl_dns_ok;
    bool ssl_sni_ok;
    int16_t ssl_errno;
    Esp8266Status status;
};

class Esp8266At {
public:
    Esp8266At(uart_inst_t *uart, uint tx_pin, uint rx_pin);

    void init(uint32_t baudrate);
    Esp8266Diagnostic run_startup_diagnostic();
    bool join_wifi(const char *ssid, const char *password);
    bool wifi_connected();
    bool post_https_json(const char *host, const char *path, const char *payload);
    void close_connection();
    Esp8266Status last_status() const;
    int16_t last_wifi_join_error_code() const;
    int16_t last_wifi_state() const;
    bool last_wifi_ap_seen() const;
    bool last_ssl_dns_ok() const;
    bool last_ssl_sni_ok() const;
    int16_t last_ssl_errno() const;
    const char *last_transport_error_text() const;
    uint32_t last_transport_detail() const;

private:
    bool probe_at(uint32_t baudrate);
    bool synchronize_utc_clock();
    bool post_https_json_pico_tls(const char *host, const char *path, const char *payload);
    bool send_pico_tls_http_request(const char *host, const char *request, size_t request_length, char *response, size_t response_size);
    bool open_tcp(const char *host, uint16_t port);
    bool tcp_send_bytes(const unsigned char *data, size_t length);
    int tcp_recv_bytes(unsigned char *buffer, size_t length, uint64_t deadline_ms);
    size_t tcp_available_bytes();
    bool tcp_receive_passive(unsigned char *buffer, size_t max_length, size_t *received);
    bool tcp_receive_passive_with_command(const char *command, unsigned char *buffer, size_t max_length, size_t *received, uint32_t command_variant);
    bool send_command(const char *command, const char *expected, uint32_t timeout_ms);
    bool send_command_allow_error(const char *command, const char *expected, uint32_t timeout_ms);
    bool send_command_capture(const char *command, const char *expected, uint32_t timeout_ms, char *response, size_t response_size);
    bool read_until(const char *expected, uint32_t timeout_ms);
    bool read_until_any(const char *expected_a, const char *expected_b, uint32_t timeout_ms, char *response, size_t response_size);
    void set_transport_error(Esp8266TransportError error, uint32_t detail);
    void write_command(const char *command);
    void drain_rx();

    struct TlsStreamContext {
        Esp8266At *driver;
        uint64_t receive_deadline_ms;
    };

    static int tls_send_callback(void *context, const unsigned char *buffer, size_t length);
    static int tls_recv_callback(void *context, unsigned char *buffer, size_t length);

    uart_inst_t *uart_;
    uint tx_pin_;
    uint rx_pin_;
    uint32_t baudrate_ = 0;
    int16_t last_wifi_join_error_code_ = -1;
    int16_t last_wifi_state_ = -1;
    bool last_wifi_ap_seen_ = false;
    bool last_ssl_dns_ok_ = false;
    bool last_ssl_sni_ok_ = false;
    int16_t last_ssl_errno_ = -1;
    Esp8266TransportError last_transport_error_ = Esp8266TransportError::none;
    uint32_t last_transport_detail_ = 0;
    Esp8266Status last_status_ = Esp8266Status::not_checked;
    bool clock_synchronized_ = false;
};

}
