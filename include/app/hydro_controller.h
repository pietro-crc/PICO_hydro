#pragma once

#include "cloud/google_sheets_logger.h"
#include "config/hardware_config.h"
#include "drivers/lcd1602_i2c.h"
#include "drivers/esp8266_at.h"
#include "drivers/ws2812_strip.h"
#include "sensors/ds18b20_sensor.h"
#include "sensors/dht_sensor.h"
#include "sensors/flow_meter.h"
#include "sensors/level_sensor.h"
#include "sensors/sensor_readings.h"
#include "sensors/tds_sensor.h"
#include "sensors/veml7700_sensor.h"
#include "ui/lcd_carousel.h"

#include <cstdint>

namespace hydro {

class HydroController {
public:
    HydroController();

    void init();
    [[noreturn]] void run_forever();

private:
    struct StartupDiagnostics {
        bool lcd_ok;
        bool veml_init_ok;
        bool veml_read_ok;
        bool dht_read_ok;
        bool level_present;
        TdsReading tds;
        VemlReading veml;
        DhtReading dht;
        WaterTemperatureReading water_temperature;
        uint8_t water_temperature_sensor_count;
        Esp8266Diagnostic wifi;
    };

    void init_gpio();
    void init_i2c();
    void init_display();
    void init_strip();
    StartupDiagnostics run_startup_diagnostics(bool lcd_ok, bool veml_ok);
    void show_startup_progress(uint8_t percent, const char *status);
    void show_startup_diagnostics(const StartupDiagnostics &diagnostics);
    void log_startup(const StartupDiagnostics &diagnostics) const;
    void log_health(uint64_t now_ms, bool level_present) const;
    void update_strip(bool flow_detected, bool level_present);
    float tds_compensation_temperature_c() const;
    void invalidate_water_temperature_cache(WaterTemperatureStatus status);
    void log_sample(bool level_present, bool flow_detected, float liters_per_minute) const;
    bool send_remote_log(uint64_t now_ms, bool level_present);
    void expire_stale_readings(uint64_t now_ms);
    void blink_onboard_led_startup();

    Lcd1602I2c lcd_;
    Esp8266At wifi_module_;
    Veml7700Sensor veml_sensor_;
    TdsSensor tds_sensor_;
    Ds18b20Sensor water_temperature_sensor_;
    DhtSensor dht_sensor_;
    FlowMeter flow_meter_;
    LevelSensor level_sensor_;
    Ws2812Strip strip_;
    LcdCarousel carousel_;
    GoogleSheetsLogger google_logger_;

    DhtReading dht_ = {false, 0.0f, 0.0f};
    WaterTemperatureReading water_temperature_ = {false, false, WaterTemperatureStatus::not_read, 0.0f};
    WaterTemperatureReading water_temperatures_[config::MAX_WATER_TEMPERATURE_SENSORS] = {};
    uint8_t water_temperature_count_ = 0;
    uint8_t water_temperature_lcd_index_ = 0;
    VemlReading veml_ = {false, 0, 0.0f};
    TdsReading tds_ = {false, 0, 0.0f, 0.0f};
    Esp8266Diagnostic wifi_ = {
        config::ENABLE_ESP8266_WIFI,
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

    float total_liters_ = 0.0f;
    float liters_per_minute_ = 0.0f;
    uint32_t dht_failures_ = 0;
    uint32_t veml_failures_ = 0;
    uint32_t water_temperature_failures_ = 0;
    uint32_t wifi_failures_ = 0;
    uint64_t dht_last_ok_ms_ = 0;
    uint64_t veml_last_ok_ms_ = 0;
    uint64_t water_temperature_last_ok_ms_ = 0;
    uint64_t flow_sample_started_ms_ = 0;
    uint64_t next_remote_log_ms_ = config::GOOGLE_SHEETS_LOG_INTERVAL_MS;
    uint64_t next_wifi_reprobe_ms_ = config::ESP8266_REPROBE_INTERVAL_MS;
    uint32_t remote_log_failure_streak_ = 0;
    bool heartbeat_state_ = false;
    bool flow_detected_ = false;
    bool water_temperature_conversion_pending_ = false;
    uint64_t water_temperature_ready_ms_ = 0;
    uint8_t lcd_page_ = 0;
};

}
