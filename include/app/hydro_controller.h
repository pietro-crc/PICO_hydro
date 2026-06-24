#pragma once

#include "drivers/lcd1602_i2c.h"
#include "drivers/ws2812_strip.h"
#include "sensors/ds18b20_sensor.h"
#include "sensors/dht_sensor.h"
#include "sensors/flow_meter.h"
#include "sensors/level_sensor.h"
#include "sensors/sensor_readings.h"
#include "sensors/tds_sensor.h"
#include "sensors/veml7700_sensor.h"
#include "ui/lcd_carousel.h"

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
        bool water_temperature_read_ok;
        bool level_present;
        TdsReading tds;
        VemlReading veml;
        DhtReading dht;
        WaterTemperatureReading water_temperature;
    };

    void init_gpio();
    void init_i2c();
    void init_display();
    void init_strip();
    StartupDiagnostics run_startup_diagnostics(bool lcd_ok, bool veml_ok);
    void show_startup_progress(uint8_t percent, const char *status);
    void show_startup_diagnostics(const StartupDiagnostics &diagnostics);
    void log_startup(const StartupDiagnostics &diagnostics) const;
    void update_strip(bool flow_detected, bool level_present);
    void log_sample(bool level_present, bool flow_detected, float liters_per_minute) const;
    void blink_onboard_led_startup();

    Lcd1602I2c lcd_;
    Veml7700Sensor veml_sensor_;
    TdsSensor tds_sensor_;
    Ds18b20Sensor water_temperature_sensor_;
    DhtSensor dht_sensor_;
    FlowMeter flow_meter_;
    LevelSensor level_sensor_;
    Ws2812Strip strip_;
    LcdCarousel carousel_;

    DhtReading dht_ = {false, 0.0f, 0.0f};
    WaterTemperatureReading water_temperature_ = {false, false, WaterTemperatureStatus::not_read, 0.0f};
    VemlReading veml_ = {false, 0, 0.0f};
    TdsReading tds_ = {false, 0, 0.0f, 0.0f};

    float total_liters_ = 0.0f;
    float liters_per_minute_ = 0.0f;
    bool heartbeat_state_ = false;
    bool flow_detected_ = false;
    bool water_temperature_conversion_pending_ = false;
    uint64_t water_temperature_ready_ms_ = 0;
    uint8_t lcd_page_ = 0;
};

}
