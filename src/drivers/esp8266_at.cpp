#include "drivers/esp8266_at.h"

#include "config/hardware_config.h"
#include "pico/stdlib.h"
#include "runtime/runtime_watchdog.h"

#if __has_include("config/tls_trust_store.h")
#include "config/tls_trust_store.h"
#define HYDRO_HAS_TLS_TRUST_STORE 1
#else
#define HYDRO_HAS_TLS_TRUST_STORE 0
#endif

#if HYDRO_HAS_TLS_TRUST_STORE
constexpr const char *GOOGLE_SCRIPT_TRUSTED_CA_PEM = hydro::trust::GOOGLE_SCRIPT_TRUSTED_CA_PEM;
#else
constexpr const char *GOOGLE_SCRIPT_TRUSTED_CA_PEM = "";
#endif

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/ssl.h"
#include "mbedtls/timing.h"
#include "mbedtls/x509_crt.h"

#include <cstdio>
#include <ctime>
#include <cstring>
#include <sys/time.h>

extern "C" mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

extern "C" unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *timer, int reset) {
    if (timer == nullptr) {
        return 0;
    }

    uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (reset != 0) {
        timer->start_ms = now_ms;
        return 0;
    }

    return (unsigned long)(now_ms - timer->start_ms);
}

extern "C" void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms) {
    mbedtls_timing_delay_context *context = static_cast<mbedtls_timing_delay_context *>(data);
    if (context == nullptr) {
        return;
    }

    context->int_ms = int_ms;
    context->fin_ms = fin_ms;
    (void)mbedtls_timing_get_timer(&context->timer, 1);
}

extern "C" int mbedtls_timing_get_delay(void *data) {
    mbedtls_timing_delay_context *context = static_cast<mbedtls_timing_delay_context *>(data);
    if (context == nullptr || context->fin_ms == 0) {
        return -1;
    }

    unsigned long elapsed_ms = mbedtls_timing_get_timer(&context->timer, 0);
    if (elapsed_ms >= context->fin_ms) {
        return 2;
    }
    if (elapsed_ms >= context->int_ms) {
        return 1;
    }

    return 0;
}

extern "C" uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context *data) {
    return data == nullptr ? 0 : data->fin_ms;
}

namespace hydro {
namespace {

constexpr uint32_t ESP8266_FIRST_AT_TIMEOUT_MS = 500;
constexpr uint32_t ESP8266_AT_TIMEOUT_MS = 800;
constexpr uint32_t ESP8266_GMR_TIMEOUT_MS = 1200;
constexpr uint32_t ESP8266_WIFI_COMMAND_TIMEOUT_MS = 2500;
constexpr uint32_t ESP8266_WIFI_JOIN_TIMEOUT_MS = 25000;
constexpr uint32_t ESP8266_SEND_TIMEOUT_MS = 7000;
constexpr uint32_t ESP8266_TCP_TIMEOUT_MS = 12000;
constexpr uint32_t ESP8266_TCP_QUERY_TIMEOUT_MS = 2500;
constexpr uint32_t ESP8266_TLS_HANDSHAKE_TIMEOUT_MS = 35000;
constexpr uint32_t ESP8266_TLS_RESPONSE_TIMEOUT_MS = 18000;
constexpr uint32_t ESP8266_SNTP_COMMAND_TIMEOUT_MS = 2500;
constexpr uint32_t ESP8266_SNTP_QUERY_TIMEOUT_MS = 2500;
constexpr uint32_t ESP8266_SNTP_RETRY_DELAY_MS = 1000;
constexpr uint8_t ESP8266_SNTP_MAX_ATTEMPTS = 8;
constexpr uint32_t ESP8266_CLOCK_RESYNC_INTERVAL_MS = 6U * 60U * 60U * 1000U;
constexpr uint32_t ESP8266_DRAIN_TIMEOUT_MS = 50;
constexpr size_t ESP8266_TCP_SEND_CHUNK_SIZE = 768;
constexpr size_t ESP8266_TCP_RECEIVE_CHUNK_SIZE = 768;
constexpr uint32_t ESP8266_COMMON_BAUDRATES[] = {115200, 9600, 57600, 38400, 19200};

void append_tail(char *tail, size_t tail_size, size_t &tail_used, char value) {
    if (tail == nullptr || tail_size < 2) {
        return;
    }

    if (tail_used < tail_size - 1) {
        tail[tail_used++] = value;
        tail[tail_used] = '\0';
        return;
    }

    std::memmove(tail, tail + 1, tail_size - 2);
    tail[tail_size - 2] = value;
    tail[tail_size - 1] = '\0';
    tail_used = tail_size - 1;
}

bool contains_text(const char *text, const char *needle) {
    return text != nullptr && needle != nullptr && std::strstr(text, needle) != nullptr;
}

bool response_has_status(const char *response, const char *status) {
    return contains_text(response, status);
}

bool response_is_redirect(const char *response) {
    return response_has_status(response, "HTTP/1.1 301") ||
        response_has_status(response, "HTTP/1.1 302") ||
        response_has_status(response, "HTTP/1.1 303") ||
        response_has_status(response, "HTTP/1.1 307") ||
        response_has_status(response, "HTTP/1.1 308");
}

bool response_is_google_success(const char *response) {
    if (!response_has_status(response, "HTTP/1.1 200") &&
        !response_has_status(response, "HTTP/1.0 200")) {
        return false;
    }

    const char *body = std::strstr(response, "\r\n\r\n");
    if (body == nullptr) {
        body = std::strstr(response, "\n\n");
        if (body != nullptr) {
            body += 2;
        }
    } else {
        body += 4;
    }

    if (body == nullptr) {
        return false;
    }

    return contains_text(body, "\"ok\":true") ||
        contains_text(body, "\"ok\": true");
}

bool response_is_google_application_error(const char *response) {
    const char *body = std::strstr(response, "\r\n\r\n");
    if (body == nullptr) {
        body = std::strstr(response, "\n\n");
        if (body != nullptr) {
            body += 2;
        }
    } else {
        body += 4;
    }

    return body != nullptr &&
        (contains_text(body, "\"ok\":false") ||
            contains_text(body, "\"ok\": false") ||
            contains_text(body, "\"error\""));
}

bool response_is_google_unauthorized(const char *response) {
    return response_has_status(response, "HTTP/1.1 401");
}

const char *http_response_body(const char *response) {
    if (response == nullptr) {
        return nullptr;
    }

    const char *body = std::strstr(response, "\r\n\r\n");
    if (body != nullptr) {
        return body + 4;
    }

    body = std::strstr(response, "\n\n");
    return body == nullptr ? nullptr : body + 2;
}

bool is_allowed_google_redirect_host(const char *host) {
    return host != nullptr &&
        (std::strcmp(host, "script.google.com") == 0 ||
            std::strcmp(host, "script.googleusercontent.com") == 0);
}

bool wifi_query_reports_connection(const char *response, const char *expected_ssid) {
    if (response == nullptr) {
        return false;
    }

    const char *connection = std::strstr(response, "+CWJAP:");
    if (connection == nullptr || expected_ssid == nullptr) {
        return connection != nullptr;
    }

    const char *ssid_start = std::strchr(connection, '"');
    if (ssid_start == nullptr) {
        return false;
    }
    ssid_start++;

    const char *ssid_end = std::strchr(ssid_start, '"');
    if (ssid_end == nullptr) {
        return false;
    }

    const size_t ssid_length = (size_t)(ssid_end - ssid_start);
    return ssid_length == std::strlen(expected_ssid) &&
        std::strncmp(ssid_start, expected_ssid, ssid_length) == 0;
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

bool calendar_to_epoch(
    const char *month_name,
    int day,
    int hour,
    int minute,
    int second,
    int year,
    time_t *epoch) {
    if (month_name == nullptr || epoch == nullptr) {
        return false;
    }

    static constexpr const char *MONTHS[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int month = -1;
    for (int index = 0; index < 12; ++index) {
        if (std::strncmp(month_name, MONTHS[index], 3) == 0) {
            month = index;
            break;
        }
    }

    static constexpr int DAYS_PER_MONTH[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (month < 0 || year < 2024 || year > 2100 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    int days_in_month = DAYS_PER_MONTH[month];
    if (month == 1 && is_leap_year(year)) {
        days_in_month++;
    }
    if (day < 1 || day > days_in_month) {
        return false;
    }

    uint64_t days_since_epoch = 0;
    for (int current_year = 1970; current_year < year; ++current_year) {
        days_since_epoch += is_leap_year(current_year) ? 366U : 365U;
    }
    for (int current_month = 0; current_month < month; ++current_month) {
        days_since_epoch += DAYS_PER_MONTH[current_month];
        if (current_month == 1 && is_leap_year(year)) {
            days_since_epoch++;
        }
    }
    days_since_epoch += (uint64_t)(day - 1);

    uint64_t seconds_since_epoch = days_since_epoch * 86400U +
        (uint64_t)hour * 3600U + (uint64_t)minute * 60U + (uint64_t)second;
    *epoch = (time_t)seconds_since_epoch;
    return *epoch > 0 && (uint64_t)*epoch == seconds_since_epoch;
}

bool parse_esp_sntp_epoch(const char *response, time_t *epoch) {
    if (response == nullptr || epoch == nullptr) {
        return false;
    }

    const char *time_value = std::strstr(response, "+CIPSNTPTIME:");
    if (time_value == nullptr) {
        return false;
    }
    time_value += std::strlen("+CIPSNTPTIME:");

    char month_name[4] = {};
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int year = 0;
    if (std::sscanf(time_value, "%*3s %3s %d %d:%d:%d %d", month_name, &day, &hour, &minute, &second, &year) != 6) {
        return false;
    }

    return calendar_to_epoch(month_name, day, hour, minute, second, year, epoch);
}

bool build_timestamp_epoch(time_t *epoch) {
    char month_name[4] = {};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(__DATE__, "%3s %d %d", month_name, &day, &year) != 3 ||
        std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return false;
    }

    return calendar_to_epoch(month_name, day, hour, minute, second, year, epoch);
}

bool extract_redirect_location(const char *response, char *host, size_t host_size, char *path, size_t path_size) {
    if (response == nullptr || host == nullptr || path == nullptr || host_size == 0 || path_size == 0) {
        return false;
    }

    const char *location = std::strstr(response, "\r\nLocation:");
    if (location == nullptr) {
        location = std::strstr(response, "\nLocation:");
    }
    if (location == nullptr) {
        location = std::strstr(response, "\r\nlocation:");
    }
    if (location == nullptr) {
        location = std::strstr(response, "\nlocation:");
    }
    if (location == nullptr) {
        return false;
    }

    location = std::strchr(location, ':');
    if (location == nullptr) {
        return false;
    }
    location++;
    while (*location == ' ') {
        location++;
    }

    constexpr const char *https_prefix = "https://";
    size_t prefix_length = std::strlen(https_prefix);
    if (std::strncmp(location, https_prefix, prefix_length) != 0) {
        return false;
    }

    const char *host_start = location + prefix_length;
    const char *path_start = std::strchr(host_start, '/');
    if (path_start == nullptr || path_start == host_start) {
        return false;
    }

    size_t parsed_host_length = (size_t)(path_start - host_start);
    if (parsed_host_length == 0 || parsed_host_length >= host_size) {
        return false;
    }

    const char *line_end = path_start;
    while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') {
        line_end++;
    }

    size_t parsed_path_length = (size_t)(line_end - path_start);
    if (parsed_path_length == 0 || parsed_path_length >= path_size) {
        return false;
    }

    std::memcpy(host, host_start, parsed_host_length);
    host[parsed_host_length] = '\0';
    std::memcpy(path, path_start, parsed_path_length);
    path[parsed_path_length] = '\0';
    return true;
}

const char *transport_error_text(Esp8266TransportError error) {
    switch (error) {
    case Esp8266TransportError::none:
        return "none";
    case Esp8266TransportError::invalid_argument:
        return "invalid_argument";
    case Esp8266TransportError::tcp_setup_failed:
        return "tcp_setup_failed";
    case Esp8266TransportError::tcp_open_failed:
        return "tcp_open_failed";
    case Esp8266TransportError::send_prompt_failed:
        return "send_prompt_failed";
    case Esp8266TransportError::send_confirm_failed:
        return "send_confirm_failed";
    case Esp8266TransportError::receive_length_query_failed:
        return "receive_length_query_failed";
    case Esp8266TransportError::receive_prefix_timeout:
        return "receive_prefix_timeout";
    case Esp8266TransportError::receive_header_invalid:
        return "receive_header_invalid";
    case Esp8266TransportError::receive_payload_timeout:
        return "receive_payload_timeout";
    case Esp8266TransportError::receive_status_failed:
        return "receive_status_failed";
    }

    return "unknown";
}

int16_t parse_prefixed_int(const char *response, const char *prefix) {
    if (response == nullptr || prefix == nullptr) {
        return -1;
    }

    const char *start = std::strstr(response, prefix);
    if (start == nullptr) {
        return -1;
    }

    start += std::strlen(prefix);
    int value = 0;
    bool found_digit = false;
    while (*start >= '0' && *start <= '9') {
        found_digit = true;
        value = (value * 10) + (*start - '0');
        start++;
    }

    return found_digit ? (int16_t)value : -1;
}

size_t parse_prefixed_size(const char *response, const char *prefix) {
    if (response == nullptr || prefix == nullptr) {
        return 0;
    }

    const char *start = std::strstr(response, prefix);
    if (start == nullptr) {
        return 0;
    }

    start += std::strlen(prefix);
    size_t value = 0;
    bool found_digit = false;
    while (*start >= '0' && *start <= '9') {
        found_digit = true;
        value = (value * 10U) + (size_t)(*start - '0');
        start++;
    }

    return found_digit ? value : 0;
}

bool match_prefix_token(uart_inst_t *uart, const char *token, uint64_t deadline_ms, char *tail, size_t tail_size, size_t &tail_used) {
    size_t matched = 0;

    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        if (!uart_is_readable(uart)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        char value = (char)uart_getc(uart);
        append_tail(tail, tail_size, tail_used, value);
        if (value == token[matched]) {
            matched++;
            if (token[matched] == '\0') {
                return true;
            }
        } else {
            matched = value == token[0] ? 1U : 0U;
        }

        if (contains_text(tail, "ERROR") || contains_text(tail, "FAIL")) {
            return false;
        }
    }

    return false;
}

}

Esp8266At::Esp8266At(uart_inst_t *uart, uint tx_pin, uint rx_pin)
    : uart_(uart), tx_pin_(tx_pin), rx_pin_(rx_pin) {}

void Esp8266At::init(uint32_t baudrate) {
    baudrate_ = uart_init(uart_, baudrate);
    clock_synchronized_ = false;
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
        0,
        -1,
        -1,
        false,
        false,
        false,
        -1,
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

    char firmware_response[256];
    if (send_command_capture("AT+GMR", "OK", ESP8266_GMR_TIMEOUT_MS, firmware_response, sizeof(firmware_response))) {
        diagnostic.firmware_queried = true;
        last_status_ = Esp8266Status::firmware_ok;
        diagnostic.status = last_status_;
        if (config::ENABLE_SERIAL_LOG) {
            printf("ESP8266 firmware response: %.180s\n", firmware_response);
        }
    }

    (void)send_command_allow_error("AT+SYSSTORE=0", "OK", ESP8266_AT_TIMEOUT_MS);
    (void)send_command_allow_error("AT+SYSLOG=0", "OK", ESP8266_AT_TIMEOUT_MS);

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

bool Esp8266At::join_wifi(const char *ssid, const char *password) {
    last_wifi_join_error_code_ = -1;
    last_wifi_state_ = -1;
    last_wifi_ap_seen_ = false;

    if (ssid == nullptr || password == nullptr || ssid[0] == '\0') {
        last_status_ = Esp8266Status::wifi_join_failed;
        return false;
    }

    if (!send_command("AT+CWMODE=1", "OK", ESP8266_WIFI_COMMAND_TIMEOUT_MS)) {
        last_status_ = Esp8266Status::wifi_join_failed;
        return false;
    }

    if (!send_command("AT+CIPMUX=0", "OK", ESP8266_AT_TIMEOUT_MS)) {
        last_status_ = Esp8266Status::wifi_join_failed;
        return false;
    }

    (void)send_command_allow_error("AT+CIPMODE=0", "OK", ESP8266_AT_TIMEOUT_MS);
    (void)send_command_allow_error("AT+CIPDINFO=0", "OK", ESP8266_AT_TIMEOUT_MS);
    (void)send_command_allow_error("AT+CIPRECVMODE=0", "OK", ESP8266_AT_TIMEOUT_MS);
    (void)send_command_allow_error("AT+CWDHCP=1,1", "OK", ESP8266_AT_TIMEOUT_MS);

    char joined_response[192];
    if (send_command_capture("AT+CWJAP?", "OK", ESP8266_AT_TIMEOUT_MS, joined_response, sizeof(joined_response)) &&
        wifi_query_reports_connection(joined_response, ssid)) {
        last_wifi_state_ = 2;
        last_wifi_ap_seen_ = true;
        last_status_ = Esp8266Status::wifi_join_ok;
        return true;
    }

    char command[160];
    int length = std::snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
    if (length <= 0 || (size_t)length >= sizeof(command)) {
        last_status_ = Esp8266Status::wifi_join_failed;
        return false;
    }

    char response[256];
    if (!send_command_capture(command, "OK", ESP8266_WIFI_JOIN_TIMEOUT_MS, response, sizeof(response))) {
        last_wifi_join_error_code_ = parse_prefixed_int(response, "+CWJAP:");
        if (wifi_connected()) {
            last_wifi_ap_seen_ = true;
            last_status_ = Esp8266Status::wifi_join_ok;
            clock_synchronized_ = false;
            return true;
        }
        last_status_ = Esp8266Status::wifi_join_failed;
        return false;
    }

    last_status_ = Esp8266Status::wifi_join_ok;
    clock_synchronized_ = false;
    return true;
}

bool Esp8266At::wifi_connected() {
    char response[128];
    if (send_command_capture("AT+CWSTATE?", "OK", ESP8266_AT_TIMEOUT_MS, response, sizeof(response))) {
        last_wifi_state_ = parse_prefixed_int(response, "+CWSTATE:");
        if (last_wifi_state_ == 2) {
            return true;
        }
    }

    char joined_response[192];
    if (send_command_capture("AT+CWJAP?", "OK", ESP8266_AT_TIMEOUT_MS, joined_response, sizeof(joined_response)) &&
        wifi_query_reports_connection(joined_response, nullptr)) {
        last_wifi_state_ = 2;
        return true;
    }

    return false;
}

bool Esp8266At::synchronize_utc_clock() {
    const uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (clock_synchronized_ &&
        now_ms - last_clock_sync_ms_ < ESP8266_CLOCK_RESYNC_INTERVAL_MS) {
        return true;
    }

    static constexpr const char *SNTP_CONFIG_COMMANDS[] = {
        "AT+CIPSNTPCFG=1,0,\"time.google.com\",\"pool.ntp.org\"",
        "AT+CIPSNTPCFG=1,0,\"time.google.com\"",
        "AT+CIPSNTPCFG=1,0"
    };
    bool sntp_configured = false;
    for (const char *command : SNTP_CONFIG_COMMANDS) {
        if (send_command(command, "OK", ESP8266_SNTP_COMMAND_TIMEOUT_MS)) {
            sntp_configured = true;
            break;
        }
    }

    if (sntp_configured) {
        for (uint8_t attempt = 0; attempt < ESP8266_SNTP_MAX_ATTEMPTS; ++attempt) {
            char response[160];
            if (send_command_capture(
                    "AT+CIPSNTPTIME?",
                    "OK",
                    ESP8266_SNTP_QUERY_TIMEOUT_MS,
                    response,
                    sizeof(response))) {
                time_t epoch = 0;
                if (parse_esp_sntp_epoch(response, &epoch)) {
                    timeval clock_time = {epoch, 0};
                    if (settimeofday(&clock_time, nullptr) == 0) {
                        clock_synchronized_ = true;
                        last_clock_sync_ms_ = now_ms;
                        return true;
                    }
                }
            }

            if (attempt + 1 < ESP8266_SNTP_MAX_ATTEMPTS) {
                runtime::sleep_ms_guarded(ESP8266_SNTP_RETRY_DELAY_MS);
            }
        }
    }

    time_t build_epoch = 0;
    if (build_timestamp_epoch(&build_epoch)) {
        timeval clock_time = {build_epoch, 0};
        if (settimeofday(&clock_time, nullptr) == 0) {
            clock_synchronized_ = true;
            if (config::ENABLE_SERIAL_LOG) {
                std::printf("Clock SNTP non disponibile: uso timestamp build per TLS\n");
            }
            last_clock_sync_ms_ = now_ms;
            return true;
        }
    }

    if (clock_synchronized_) {
        return true;
    }

    last_status_ = Esp8266Status::clock_sync_failed;
    return false;
}

int16_t Esp8266At::last_wifi_join_error_code() const {
    return last_wifi_join_error_code_;
}

int16_t Esp8266At::last_wifi_state() const {
    return last_wifi_state_;
}

bool Esp8266At::last_wifi_ap_seen() const {
    return last_wifi_ap_seen_;
}

bool Esp8266At::last_ssl_dns_ok() const {
    return last_ssl_dns_ok_;
}

bool Esp8266At::last_ssl_sni_ok() const {
    return last_ssl_sni_ok_;
}

int16_t Esp8266At::last_ssl_errno() const {
    return last_ssl_errno_;
}

const char *Esp8266At::last_transport_error_text() const {
    return transport_error_text(last_transport_error_);
}

uint32_t Esp8266At::last_transport_detail() const {
    return last_transport_detail_;
}

Esp8266Status Esp8266At::last_status() const {
    return last_status_;
}

bool Esp8266At::post_https_json(const char *host, const char *path, const char *payload) {
    if (host == nullptr || path == nullptr || payload == nullptr) {
        last_status_ = Esp8266Status::http_send_failed;
        return false;
    }

    return post_https_json_pico_tls(host, path, payload);
}

bool Esp8266At::post_https_json_pico_tls(const char *host, const char *path, const char *payload) {
    if (host == nullptr || path == nullptr || payload == nullptr) {
        last_status_ = Esp8266Status::http_send_failed;
        return false;
    }

#if HYDRO_HAS_TLS_TRUST_STORE
    if (GOOGLE_SCRIPT_TRUSTED_CA_PEM[0] == '\0') {
        last_status_ = Esp8266Status::tls_trust_store_missing;
        return false;
    }
#else
    last_status_ = Esp8266Status::tls_trust_store_missing;
    return false;
#endif

    if (!synchronize_utc_clock()) {
        return false;
    }

    static char request[1400];
    int request_length = std::snprintf(
        request,
        sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: PICO-Hydro/1.0\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        path,
        host,
        (unsigned)std::strlen(payload),
        payload
    );

    if (request_length <= 0 || (size_t)request_length >= sizeof(request)) {
        last_status_ = Esp8266Status::http_send_failed;
        return false;
    }

    static char response[2048];
    response[0] = '\0';
    if (!send_pico_tls_http_request(host, request, (size_t)request_length, response, sizeof(response))) {
        return false;
    }

    if (response_is_google_success(response)) {
        last_status_ = Esp8266Status::http_send_ok;
        return true;
    }

    if (response_is_redirect(response)) {
        static char redirect_host[80];
        static char redirect_path[768];
        if (extract_redirect_location(response, redirect_host, sizeof(redirect_host), redirect_path, sizeof(redirect_path)) &&
            is_allowed_google_redirect_host(redirect_host)) {
            static char redirect_request[1000];
            int redirect_request_length = std::snprintf(
                redirect_request,
                sizeof(redirect_request),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: PICO-Hydro/1.0\r\n"
                "Accept: application/json\r\n"
                "Accept-Encoding: identity\r\n"
                "Connection: close\r\n"
                "\r\n",
                redirect_path,
                redirect_host
            );

            if (redirect_request_length > 0 && (size_t)redirect_request_length < sizeof(redirect_request)) {
                static char redirect_response[2048];
                redirect_response[0] = '\0';
                if (send_pico_tls_http_request(
                        redirect_host,
                        redirect_request,
                        (size_t)redirect_request_length,
                        redirect_response,
                        sizeof(redirect_response)) &&
                    response_is_google_success(redirect_response)) {
                    last_status_ = Esp8266Status::http_send_ok;
                    return true;
                }

                if (config::ENABLE_SERIAL_LOG) {
                    const char *body = http_response_body(redirect_response);
                    printf(
                        "Pico TLS Google redirect response body: %.180s\n",
                        body == nullptr ? "<missing>" : body
                    );
                }
            }
        } else if (config::ENABLE_SERIAL_LOG) {
            printf("Pico TLS redirect without parsable Location: %.180s\n", response);
        }
    }

    if (config::ENABLE_SERIAL_LOG) {
        if (response_is_google_unauthorized(response)) {
            printf("Pico TLS Google unauthorized: controlla deploy Apps Script access=Anyone e URL /exec\n");
        }
        printf("Pico TLS HTTP response fragment: %.180s\n", response);
    }

    last_status_ = Esp8266Status::http_send_failed;
    return false;
}

bool Esp8266At::send_pico_tls_http_request(
    const char *host,
    const char *request,
    size_t request_length,
    char *response,
    size_t response_size
) {
    if (host == nullptr || request == nullptr || response == nullptr || response_size == 0) {
        last_status_ = Esp8266Status::http_send_failed;
        set_transport_error(Esp8266TransportError::invalid_argument, 0);
        return false;
    }

    response[0] = '\0';
    size_t response_used = 0;
    bool connected = false;
    bool handshake_done = false;
    bool received_http = false;
    int ret = 0;
    TlsStreamContext stream = {
        this,
        0
    };
    set_transport_error(Esp8266TransportError::none, 0);

    static mbedtls_ssl_context ssl;
    static mbedtls_ssl_config conf;
    static mbedtls_ctr_drbg_context ctr_drbg;
    static mbedtls_entropy_context entropy;
    static mbedtls_x509_crt trusted_ca;
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&trusted_ca);

    if (!open_tcp(host, 443)) {
        goto cleanup;
    }
    connected = true;

    {
        static constexpr const char *personalization = "pico-hydro";
        ret = mbedtls_ctr_drbg_seed(
            &ctr_drbg,
            mbedtls_entropy_func,
            &entropy,
            reinterpret_cast<const unsigned char *>(personalization),
            std::strlen(personalization)
        );
        if (ret != 0) {
            last_ssl_errno_ = (int16_t)ret;
            last_status_ = Esp8266Status::ssl_failed;
            goto cleanup;
        }
    }

    ret = mbedtls_ssl_config_defaults(
        &conf,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT
    );
    if (ret != 0) {
        last_ssl_errno_ = (int16_t)ret;
        last_status_ = Esp8266Status::ssl_failed;
        goto cleanup;
    }

    ret = mbedtls_x509_crt_parse(
        &trusted_ca,
        reinterpret_cast<const unsigned char *>(GOOGLE_SCRIPT_TRUSTED_CA_PEM),
        std::strlen(GOOGLE_SCRIPT_TRUSTED_CA_PEM) + 1U
    );
    if (ret != 0) {
        last_ssl_errno_ = (int16_t)ret;
        last_status_ = Esp8266Status::ssl_failed;
        goto cleanup;
    }

    mbedtls_ssl_conf_ca_chain(&conf, &trusted_ca, nullptr);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_min_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) {
        last_ssl_errno_ = (int16_t)ret;
        last_status_ = Esp8266Status::ssl_failed;
        goto cleanup;
    }

    ret = mbedtls_ssl_set_hostname(&ssl, host);
    if (ret != 0) {
        last_ssl_errno_ = (int16_t)ret;
        last_status_ = Esp8266Status::ssl_failed;
        goto cleanup;
    }

    {
        stream.receive_deadline_ms = to_ms_since_boot(get_absolute_time()) + ESP8266_TLS_HANDSHAKE_TIMEOUT_MS;
        mbedtls_ssl_set_bio(&ssl, &stream, tls_send_callback, tls_recv_callback, nullptr);

        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                break;
            }
            if (to_ms_since_boot(get_absolute_time()) >= stream.receive_deadline_ms) {
                ret = MBEDTLS_ERR_SSL_TIMEOUT;
                break;
            }
            runtime::feed_watchdog();
            sleep_us(100);
        }

        if (ret != 0) {
            last_ssl_errno_ = (int16_t)ret;
            last_status_ = Esp8266Status::ssl_failed;
            goto cleanup;
        }

        if (mbedtls_ssl_get_verify_result(&ssl) != 0) {
            ret = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            last_ssl_errno_ = (int16_t)ret;
            last_status_ = Esp8266Status::ssl_failed;
            goto cleanup;
        }

        last_ssl_sni_ok_ = true;
        handshake_done = true;
        last_status_ = Esp8266Status::ssl_open_ok;

        size_t written = 0;
        while (written < request_length) {
            ret = mbedtls_ssl_write(
                &ssl,
                reinterpret_cast<const unsigned char *>(request + written),
                request_length - written
            );
            if (ret > 0) {
                written += (size_t)ret;
                continue;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                runtime::feed_watchdog();
                sleep_us(100);
                continue;
            }

            last_ssl_errno_ = (int16_t)ret;
            last_status_ = Esp8266Status::http_send_failed;
            goto cleanup;
        }

        uint64_t response_deadline_ms = to_ms_since_boot(get_absolute_time()) + ESP8266_TLS_RESPONSE_TIMEOUT_MS;
        while (to_ms_since_boot(get_absolute_time()) < response_deadline_ms) {
            unsigned char chunk[256];
            stream.receive_deadline_ms = response_deadline_ms;
            ret = mbedtls_ssl_read(&ssl, chunk, sizeof(chunk));
            if (ret > 0) {
                received_http = true;
                size_t copy_length = (size_t)ret;
                if (copy_length > response_size - 1 - response_used) {
                    copy_length = response_size - 1 - response_used;
                }
                if (copy_length > 0) {
                    std::memcpy(response + response_used, chunk, copy_length);
                    response_used += copy_length;
                    response[response_used] = '\0';
                }

                if (response_is_google_success(response) ||
                    response_is_google_application_error(response) ||
                    (response_is_redirect(response) && contains_text(response, "\r\n\r\n"))) {
                    break;
                }
                continue;
            }

            if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                break;
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                runtime::feed_watchdog();
                sleep_us(100);
                continue;
            }

            last_ssl_errno_ = (int16_t)ret;
            break;
        }
    }

cleanup:
    if (config::ENABLE_SERIAL_LOG &&
        ret != 0 &&
        (last_status_ == Esp8266Status::ssl_failed ||
            last_status_ == Esp8266Status::http_send_failed ||
            last_transport_error_ != Esp8266TransportError::none)) {
        char error_text[96];
        mbedtls_strerror(ret, error_text, sizeof(error_text));
        printf(
            "Pico TLS/HTTP failed host=\"%s\" err=%d %s transport=%s detail=%lu\n",
            host,
            ret,
            error_text,
            last_transport_error_text(),
            (unsigned long)last_transport_detail_
        );
    }

    if (handshake_done) {
        (void)mbedtls_ssl_close_notify(&ssl);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&trusted_ca);

    if (connected) {
        close_connection();
    }
    (void)send_command_allow_error("AT+CIPRECVMODE=0", "OK", ESP8266_AT_TIMEOUT_MS);

    if (!received_http) {
        if (last_status_ != Esp8266Status::ssl_failed) {
            last_status_ = Esp8266Status::http_send_failed;
        }
        return false;
    }

    set_transport_error(Esp8266TransportError::none, 0);
    last_status_ = Esp8266Status::http_send_ok;
    return true;
}

bool Esp8266At::open_tcp(const char *host, uint16_t port) {
    if (host == nullptr || host[0] == '\0') {
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }

    last_ssl_dns_ok_ = false;
    last_ssl_sni_ok_ = false;
    last_ssl_errno_ = -1;
    set_transport_error(Esp8266TransportError::none, 0);

    (void)send_command_allow_error("AT+CIPCLOSE", "OK", ESP8266_AT_TIMEOUT_MS);
    if (!send_command("AT+CIPMUX=0", "OK", ESP8266_AT_TIMEOUT_MS)) {
        set_transport_error(Esp8266TransportError::tcp_setup_failed, 1);
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }
    if (!send_command("AT+CIPMODE=0", "OK", ESP8266_AT_TIMEOUT_MS)) {
        set_transport_error(Esp8266TransportError::tcp_setup_failed, 2);
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }
    if (!send_command("AT+CIPDINFO=0", "OK", ESP8266_AT_TIMEOUT_MS)) {
        set_transport_error(Esp8266TransportError::tcp_setup_failed, 3);
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }
    if (!send_command("AT+CIPRECVMODE=1", "OK", ESP8266_AT_TIMEOUT_MS)) {
        set_transport_error(Esp8266TransportError::tcp_setup_failed, 4);
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }

    char command[160];
    int length = std::snprintf(command, sizeof(command), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, (unsigned)port);
    if (length <= 0 || (size_t)length >= sizeof(command)) {
        set_transport_error(Esp8266TransportError::invalid_argument, 1);
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }

    char response[256];
    bool opened = send_command_capture(command, "OK", ESP8266_TCP_TIMEOUT_MS, response, sizeof(response));
    if (!opened) {
        last_ssl_errno_ = parse_prefixed_int(response, "+ERRNO:");
        set_transport_error(Esp8266TransportError::tcp_open_failed, last_ssl_errno_ < 0 ? 0 : (uint32_t)last_ssl_errno_);
        if (config::ENABLE_SERIAL_LOG) {
            printf("ESP8266 TCP open failed host=\"%s\" response: %.140s\n", host, response);
        }
        last_status_ = Esp8266Status::ssl_failed;
        return false;
    }

    last_ssl_dns_ok_ = true;
    last_status_ = Esp8266Status::ssl_open_ok;
    if (config::ENABLE_SERIAL_LOG) {
        printf("ESP8266 TCP tunnel OK host=\"%s\" port=%u\n", host, (unsigned)port);
    }
    return true;
}

bool Esp8266At::tcp_send_bytes(const unsigned char *data, size_t length) {
    if (data == nullptr && length > 0) {
        set_transport_error(Esp8266TransportError::invalid_argument, 2);
        return false;
    }

    size_t offset = 0;
    while (offset < length) {
        size_t chunk_length = length - offset;
        if (chunk_length > ESP8266_TCP_SEND_CHUNK_SIZE) {
            chunk_length = ESP8266_TCP_SEND_CHUNK_SIZE;
        }

        char command[32];
        int command_length = std::snprintf(command, sizeof(command), "AT+CIPSEND=%u", (unsigned)chunk_length);
        if (command_length <= 0 || (size_t)command_length >= sizeof(command)) {
            set_transport_error(Esp8266TransportError::invalid_argument, 3);
            return false;
        }

        if (!send_command(command, ">", ESP8266_SEND_TIMEOUT_MS)) {
            set_transport_error(Esp8266TransportError::send_prompt_failed, (uint32_t)chunk_length);
            return false;
        }

        for (size_t i = 0; i < chunk_length; ++i) {
            uart_putc_raw(uart_, (char)data[offset + i]);
        }

        if (!read_until("SEND OK", ESP8266_SEND_TIMEOUT_MS)) {
            set_transport_error(Esp8266TransportError::send_confirm_failed, (uint32_t)chunk_length);
            return false;
        }

        offset += chunk_length;
    }

    return true;
}

int Esp8266At::tcp_recv_bytes(unsigned char *buffer, size_t length, uint64_t deadline_ms) {
    if (buffer == nullptr || length == 0) {
        set_transport_error(Esp8266TransportError::invalid_argument, 4);
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        runtime::feed_watchdog();
        size_t available = tcp_available_bytes();
        if (available > 0) {
            size_t request_length = available;
            if (request_length > length) {
                request_length = length;
            }
            if (request_length > ESP8266_TCP_RECEIVE_CHUNK_SIZE) {
                request_length = ESP8266_TCP_RECEIVE_CHUNK_SIZE;
            }

            size_t received = 0;
            if (!tcp_receive_passive(buffer, request_length, &received)) {
                return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
            }
            if (received > 0) {
                return (int)received;
            }
        }

        runtime::feed_watchdog();
        sleep_ms(10);
    }

    return MBEDTLS_ERR_SSL_WANT_READ;
}

size_t Esp8266At::tcp_available_bytes() {
    char response[96];
    if (!send_command_capture("AT+CIPRECVLEN?", "OK", ESP8266_TCP_QUERY_TIMEOUT_MS, response, sizeof(response))) {
        set_transport_error(Esp8266TransportError::receive_length_query_failed, 0);
        return 0;
    }

    size_t available = parse_prefixed_size(response, "+CIPRECVLEN:");
    if (available == 0) {
        available = parse_prefixed_size(response, "+CIPRECVLEN,");
    }

    return available;
}

bool Esp8266At::tcp_receive_passive(unsigned char *buffer, size_t max_length, size_t *received) {
    if (received == nullptr || buffer == nullptr || max_length == 0) {
        set_transport_error(Esp8266TransportError::invalid_argument, 5);
        return false;
    }

    char command[32];
    int command_length = std::snprintf(command, sizeof(command), "AT+CIPRECVDATA=%u", (unsigned)max_length);
    if (command_length <= 0 || (size_t)command_length >= sizeof(command)) {
        set_transport_error(Esp8266TransportError::invalid_argument, 6);
        return false;
    }

    if (tcp_receive_passive_with_command(command, buffer, max_length, received, 1)) {
        return true;
    }

    command_length = std::snprintf(command, sizeof(command), "AT+CIPRECVDATA=0,%u", (unsigned)max_length);
    if (command_length <= 0 || (size_t)command_length >= sizeof(command)) {
        set_transport_error(Esp8266TransportError::invalid_argument, 7);
        return false;
    }

    return tcp_receive_passive_with_command(command, buffer, max_length, received, 2);
}

bool Esp8266At::tcp_receive_passive_with_command(
    const char *command,
    unsigned char *buffer,
    size_t max_length,
    size_t *received,
    uint32_t command_variant
) {
    if (received == nullptr || buffer == nullptr || command == nullptr || max_length == 0) {
        set_transport_error(Esp8266TransportError::invalid_argument, command_variant);
        return false;
    }

    *received = 0;
    drain_rx();
    write_command(command);

    uint64_t deadline_ms = to_ms_since_boot(get_absolute_time()) + ESP8266_TCP_QUERY_TIMEOUT_MS;
    char tail[24] = {0};
    size_t tail_used = 0;

    if (!match_prefix_token(uart_, "+CIPRECVDATA", deadline_ms, tail, sizeof(tail), tail_used)) {
        if (config::ENABLE_SERIAL_LOG) {
            printf(
                "ESP8266 passive receive prefix failed variant=%lu tail=\"%.20s\"\n",
                (unsigned long)command_variant,
                tail
            );
        }
        set_transport_error(
            Esp8266TransportError::receive_prefix_timeout,
            command_variant * 10U + (contains_text(tail, "ERROR") || contains_text(tail, "FAIL") ? 1U : 0U)
        );
        return false;
    }

    bool prefix_separator_seen = false;
    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        if (!uart_is_readable(uart_)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        char value = (char)uart_getc(uart_);
        append_tail(tail, sizeof(tail), tail_used, value);
        if (value == ':' || value == ',') {
            prefix_separator_seen = true;
            break;
        }

        if (contains_text(tail, "ERROR") || contains_text(tail, "FAIL")) {
            set_transport_error(Esp8266TransportError::receive_prefix_timeout, command_variant * 10U + 1U);
            return false;
        }
    }

    if (!prefix_separator_seen) {
        set_transport_error(Esp8266TransportError::receive_prefix_timeout, command_variant * 10U);
        return false;
    }

    size_t actual_length = 0;
    bool found_digit = false;
    bool header_done = false;
    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        if (!uart_is_readable(uart_)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        char value = (char)uart_getc(uart_);
        if (value >= '0' && value <= '9') {
            found_digit = true;
            actual_length = (actual_length * 10U) + (size_t)(value - '0');
            continue;
        }

        if (value == ',' || value == ':' || (found_digit && actual_length == 0 && (value == '\r' || value == '\n'))) {
            header_done = true;
            break;
        }

        set_transport_error(Esp8266TransportError::receive_header_invalid, command_variant * 1000U + (uint32_t)(unsigned char)value);
        return false;
    }

    if (!found_digit || !header_done) {
        set_transport_error(Esp8266TransportError::receive_header_invalid, command_variant * 10U + (found_digit ? 1U : 0U));
        return false;
    }

    size_t copy_length = actual_length;
    if (copy_length > max_length) {
        copy_length = max_length;
    }

    size_t copied = 0;
    while (copied < copy_length && to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        if (!uart_is_readable(uart_)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        buffer[copied++] = (unsigned char)uart_getc(uart_);
    }

    if (copied != copy_length) {
        set_transport_error(Esp8266TransportError::receive_payload_timeout, command_variant * 100000U + (uint32_t)copy_length);
        return false;
    }

    size_t discarded = copied;
    while (discarded < actual_length && to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        if (!uart_is_readable(uart_)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        (void)uart_getc(uart_);
        discarded++;
    }

    *received = copied;
    tail[0] = '\0';
    tail_used = 0;
    uint64_t ok_deadline_ms = to_ms_since_boot(get_absolute_time()) + ESP8266_TCP_QUERY_TIMEOUT_MS;
    while (to_ms_since_boot(get_absolute_time()) < ok_deadline_ms) {
        if (!uart_is_readable(uart_)) {
            runtime::feed_watchdog();
            sleep_us(100);
            continue;
        }

        char value = (char)uart_getc(uart_);
        append_tail(tail, sizeof(tail), tail_used, value);
        if (contains_text(tail, "OK")) {
            return true;
        }
        if (contains_text(tail, "ERROR") || contains_text(tail, "FAIL")) {
            set_transport_error(Esp8266TransportError::receive_status_failed, command_variant * 10U + 1U);
            return false;
        }
    }

    if (copied == 0 && actual_length > 0) {
        set_transport_error(Esp8266TransportError::receive_payload_timeout, command_variant * 100000U + (uint32_t)actual_length);
        return false;
    }

    return copied > 0 || actual_length == 0;
}

int Esp8266At::tls_send_callback(void *context, const unsigned char *buffer, size_t length) {
    TlsStreamContext *stream = static_cast<TlsStreamContext *>(context);
    if (stream == nullptr || stream->driver == nullptr) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    if (stream->driver->tcp_send_bytes(buffer, length)) {
        return (int)length;
    }

    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

int Esp8266At::tls_recv_callback(void *context, unsigned char *buffer, size_t length) {
    TlsStreamContext *stream = static_cast<TlsStreamContext *>(context);
    if (stream == nullptr || stream->driver == nullptr) {
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }

    return stream->driver->tcp_recv_bytes(buffer, length, stream->receive_deadline_ms);
}

void Esp8266At::close_connection() {
    (void)send_command_allow_error("AT+CIPCLOSE", "OK", ESP8266_AT_TIMEOUT_MS);
}

bool Esp8266At::send_command(const char *command, const char *expected, uint32_t timeout_ms) {
    drain_rx();
    write_command(command);
    return read_until(expected, timeout_ms);
}

bool Esp8266At::send_command_allow_error(const char *command, const char *expected, uint32_t timeout_ms) {
    drain_rx();
    write_command(command);

    char response[128];
    return read_until_any(expected, "ERROR", timeout_ms, response, sizeof(response));
}

bool Esp8266At::send_command_capture(const char *command, const char *expected, uint32_t timeout_ms, char *response, size_t response_size) {
    drain_rx();
    write_command(command);
    return read_until_any(expected, nullptr, timeout_ms, response, response_size);
}

bool Esp8266At::read_until(const char *expected, uint32_t timeout_ms) {
    char response[256];
    return read_until_any(expected, nullptr, timeout_ms, response, sizeof(response));
}

bool Esp8266At::read_until_any(const char *expected_a, const char *expected_b, uint32_t timeout_ms, char *response, size_t response_size) {
    if (response == nullptr || response_size == 0 || expected_a == nullptr) {
        return false;
    }

    response[0] = '\0';
    size_t used = 0;
    char tail[96] = {0};
    size_t tail_used = 0;
    uint64_t deadline_ms = to_ms_since_boot(get_absolute_time()) + timeout_ms;

    while (to_ms_since_boot(get_absolute_time()) < deadline_ms) {
        while (to_ms_since_boot(get_absolute_time()) < deadline_ms && uart_is_readable(uart_)) {
            char value = (char)uart_getc(uart_);
            append_tail(tail, sizeof(tail), tail_used, value);
            if (used < response_size - 1) {
                response[used++] = value;
                response[used] = '\0';
            }

            if (std::strstr(response, expected_a) != nullptr || std::strstr(tail, expected_a) != nullptr) {
                return true;
            }

            if (expected_b != nullptr &&
                (std::strstr(response, expected_b) != nullptr || std::strstr(tail, expected_b) != nullptr)) {
                return true;
            }

            if (std::strstr(response, "ERROR") != nullptr ||
                std::strstr(response, "FAIL") != nullptr ||
                std::strstr(tail, "ERROR") != nullptr ||
                std::strstr(tail, "FAIL") != nullptr) {
                return false;
            }
        }

        runtime::feed_watchdog();
        sleep_us(100);
    }

    return false;
}

void Esp8266At::set_transport_error(Esp8266TransportError error, uint32_t detail) {
    last_transport_error_ = error;
    last_transport_detail_ = detail;
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
