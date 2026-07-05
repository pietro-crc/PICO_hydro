#include "cloud/google_sheets_logger.h"

#include "config/hardware_config.h"

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#define HYDRO_HAS_GOOGLE_SECRETS 1
#else
#define HYDRO_HAS_GOOGLE_SECRETS 0
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace hydro {
namespace {

constexpr size_t TELEMETRY_PAYLOAD_SIZE = 1024;

size_t append_text(char *buffer, size_t buffer_size, size_t offset, const char *text) {
    if (offset >= buffer_size) {
        return offset;
    }

    int written = std::snprintf(buffer + offset, buffer_size - offset, "%s", text);
    if (written <= 0) {
        return offset;
    }

    return offset + (size_t)written;
}

size_t append_format(char *buffer, size_t buffer_size, size_t offset, const char *format, ...) {
    if (offset >= buffer_size) {
        return offset;
    }

    va_list args;
    va_start(args, format);
    int written = std::vsnprintf(buffer + offset, buffer_size - offset, format, args);
    va_end(args);

    if (written <= 0) {
        return offset;
    }

    return offset + (size_t)written;
}

}

GoogleSheetsLogger::GoogleSheetsLogger(Esp8266At *wifi) : wifi_(wifi) {}

bool GoogleSheetsLogger::available() const {
#if HYDRO_HAS_GOOGLE_SECRETS
    return config::ENABLE_GOOGLE_SHEETS_LOGGING && wifi_ != nullptr;
#else
    return false;
#endif
}

bool GoogleSheetsLogger::init() {
    if (!available()) {
        return false;
    }

#if HYDRO_HAS_GOOGLE_SECRETS
    initialized_ = wifi_->join_wifi(secrets::WIFI_SSID, secrets::WIFI_PASSWORD);
    return initialized_;
#else
    return false;
#endif
}

bool GoogleSheetsLogger::send(const HydroTelemetry &telemetry) {
    if (!available()) {
        return false;
    }

#if HYDRO_HAS_GOOGLE_SECRETS
    if (!initialized_ && !init()) {
        return false;
    }

    static char payload[TELEMETRY_PAYLOAD_SIZE];
    if (!build_payload(telemetry, payload, sizeof(payload))) {
        return false;
    }

    bool sent = wifi_->post_https_json(
        secrets::GOOGLE_SCRIPT_HOST,
        secrets::GOOGLE_SCRIPT_PATH,
        payload
    );

    if (!sent) {
        initialized_ = wifi_->last_status() != Esp8266Status::wifi_join_failed &&
            wifi_->last_status() != Esp8266Status::uart_timeout;
    }

    return sent;
#else
    (void)telemetry;
    return false;
#endif
}

size_t GoogleSheetsLogger::append_json_float(char *buffer, size_t buffer_size, size_t offset, const char *key, bool valid, float value) const {
    offset = append_format(buffer, buffer_size, offset, "\"%s\":", key);
    if (!valid) {
        return append_text(buffer, buffer_size, offset, "null");
    }

    return append_format(buffer, buffer_size, offset, "%.3f", value);
}

size_t GoogleSheetsLogger::append_json_uint(char *buffer, size_t buffer_size, size_t offset, const char *key, bool valid, uint32_t value) const {
    offset = append_format(buffer, buffer_size, offset, "\"%s\":", key);
    if (!valid) {
        return append_text(buffer, buffer_size, offset, "null");
    }

    return append_format(buffer, buffer_size, offset, "%lu", (unsigned long)value);
}

bool GoogleSheetsLogger::build_payload(const HydroTelemetry &telemetry, char *buffer, size_t buffer_size) const {
#if HYDRO_HAS_GOOGLE_SECRETS
    size_t offset = 0;
    offset = append_text(buffer, buffer_size, offset, "{");
    offset = append_format(buffer, buffer_size, offset, "\"token\":\"%s\",", secrets::GOOGLE_SCRIPT_TOKEN);
    offset = append_format(buffer, buffer_size, offset, "\"firmware\":\"%s\",", config::FIRMWARE_VERSION);
    offset = append_format(buffer, buffer_size, offset, "\"device_uptime_s\":%llu,", (unsigned long long)(telemetry.uptime_ms / 1000));

    offset = append_json_float(buffer, buffer_size, offset, "air_temp_c", telemetry.dht != nullptr && telemetry.dht->valid, telemetry.dht != nullptr ? telemetry.dht->temperature_c : 0.0f);
    offset = append_text(buffer, buffer_size, offset, ",");
    offset = append_json_float(buffer, buffer_size, offset, "humidity_percent", telemetry.dht != nullptr && telemetry.dht->valid, telemetry.dht != nullptr ? telemetry.dht->humidity_percent : 0.0f);
    offset = append_text(buffer, buffer_size, offset, ",");

    for (uint8_t i = 0; i < config::MAX_WATER_TEMPERATURE_SENSORS; ++i) {
        bool valid = telemetry.water_temperatures != nullptr &&
            i < telemetry.water_temperature_count &&
            telemetry.water_temperatures[i].valid;
        float value = valid ? telemetry.water_temperatures[i].temperature_c : 0.0f;
        char key[16];
        std::snprintf(key, sizeof(key), "water_%u_c", (unsigned)(i + 1));
        offset = append_json_float(buffer, buffer_size, offset, key, valid, value);
        offset = append_text(buffer, buffer_size, offset, ",");
    }

    offset = append_json_float(buffer, buffer_size, offset, "tds_ppm", telemetry.tds != nullptr && telemetry.tds->valid, telemetry.tds != nullptr ? telemetry.tds->ppm : 0.0f);
    offset = append_text(buffer, buffer_size, offset, ",");
    offset = append_json_float(buffer, buffer_size, offset, "tds_voltage", telemetry.tds != nullptr && telemetry.tds->valid, telemetry.tds != nullptr ? telemetry.tds->voltage : 0.0f);
    offset = append_text(buffer, buffer_size, offset, ",");
    offset = append_json_uint(buffer, buffer_size, offset, "tds_raw", telemetry.tds != nullptr && telemetry.tds->valid, telemetry.tds != nullptr ? telemetry.tds->raw_adc : 0);
    offset = append_text(buffer, buffer_size, offset, ",");

    offset = append_json_float(buffer, buffer_size, offset, "light_lux", telemetry.veml != nullptr && telemetry.veml->valid, telemetry.veml != nullptr ? telemetry.veml->lux : 0.0f);
    offset = append_text(buffer, buffer_size, offset, ",");
    offset = append_json_uint(buffer, buffer_size, offset, "light_raw", telemetry.veml != nullptr && telemetry.veml->valid, telemetry.veml != nullptr ? telemetry.veml->raw_als : 0);
    offset = append_text(buffer, buffer_size, offset, ",");

    offset = append_format(buffer, buffer_size, offset, "\"water_level\":%s,", telemetry.level_present ? "true" : "false");
    offset = append_format(buffer, buffer_size, offset, "\"flow_l_min\":%.3f,", telemetry.liters_per_minute);
    offset = append_format(buffer, buffer_size, offset, "\"total_liters\":%.3f,", telemetry.total_liters);
    offset = append_format(buffer, buffer_size, offset, "\"dht_failures\":%lu,", (unsigned long)telemetry.dht_failures);
    offset = append_format(buffer, buffer_size, offset, "\"veml_failures\":%lu,", (unsigned long)telemetry.veml_failures);
    offset = append_format(buffer, buffer_size, offset, "\"water_temp_failures\":%lu,", (unsigned long)telemetry.water_temperature_failures);
    offset = append_format(buffer, buffer_size, offset, "\"wifi_failures\":%lu,", (unsigned long)telemetry.wifi_failures);
    offset = append_format(buffer, buffer_size, offset, "\"device_status\":\"%s\"", telemetry.flow_detected ? "flow" : "idle");
    offset = append_text(buffer, buffer_size, offset, "}");

    return offset < buffer_size;
#else
    (void)telemetry;
    (void)buffer;
    (void)buffer_size;
    return false;
#endif
}

}
