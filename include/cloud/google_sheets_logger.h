#pragma once

#include "drivers/esp8266_at.h"
#include "sensors/sensor_readings.h"

#include <cstddef>
#include <cstdint>

namespace hydro {

struct HydroTelemetry {
    uint64_t uptime_ms;
    const DhtReading *dht;
    const WaterTemperatureReading *water_temperatures;
    uint8_t water_temperature_count;
    const VemlReading *veml;
    const TdsReading *tds;
    bool level_present;
    bool flow_detected;
    float liters_per_minute;
    float total_liters;
    uint32_t dht_failures;
    uint32_t veml_failures;
    uint32_t water_temperature_failures;
    uint32_t wifi_failures;
};

class GoogleSheetsLogger {
public:
    explicit GoogleSheetsLogger(Esp8266At *wifi);

    bool available() const;
    bool init();
    bool send(const HydroTelemetry &telemetry);

private:
    size_t append_json_float(char *buffer, size_t buffer_size, size_t offset, const char *key, bool valid, float value) const;
    size_t append_json_uint(char *buffer, size_t buffer_size, size_t offset, const char *key, bool valid, uint32_t value) const;
    bool build_payload(const HydroTelemetry &telemetry, char *buffer, size_t buffer_size) const;

    Esp8266At *wifi_;
    bool initialized_ = false;
};

}
