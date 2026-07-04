#pragma once

#include "drivers/lcd1602_i2c.h"
#include "drivers/esp8266_at.h"
#include "sensors/sensor_readings.h"

#include <cstddef>
#include <cstdint>

namespace hydro {

class LcdCarousel {
public:
    explicit LcdCarousel(Lcd1602I2c *lcd);

    void show(
        uint8_t page,
        const DhtReading &dht,
        const WaterTemperatureReading *water_temperatures,
        uint8_t water_temperature_count,
        uint8_t water_temperature_index,
        const VemlReading &veml,
        const TdsReading &tds,
        bool level_present,
        bool flow_detected,
        const Esp8266Diagnostic &wifi,
        float liters_per_minute,
        float total_liters
    );

private:
    static void format_temperature_line(char *line, size_t line_size, const DhtReading &dht);
    static void format_humidity_line(char *line, size_t line_size, const DhtReading &dht);
    static void format_water_temperature_line(char *line, size_t line_size, const WaterTemperatureReading &water_temperature, uint8_t index);
    static void format_wifi_line(char *line, size_t line_size, const Esp8266Diagnostic &wifi);

    Lcd1602I2c *lcd_;
};

}
