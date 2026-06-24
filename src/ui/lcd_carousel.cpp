#include "ui/lcd_carousel.h"

#include "config/hardware_config.h"

#include <stdio.h>

namespace hydro {

LcdCarousel::LcdCarousel(Lcd1602I2c *lcd) : lcd_(lcd) {}

void LcdCarousel::format_temperature_line(char *line, size_t line_size, const DhtReading &dht) {
    if (!dht.valid) {
        snprintf(line, line_size, "Temp: attesa");
        return;
    }

    snprintf(line, line_size, "Temp: %.1f C", dht.temperature_c);
}

void LcdCarousel::format_humidity_line(char *line, size_t line_size, const DhtReading &dht) {
    if (!dht.valid) {
        snprintf(line, line_size, "Umid: attesa");
        return;
    }

    snprintf(line, line_size, "Umid: %.1f %%", dht.humidity_percent);
}

void LcdCarousel::format_water_temperature_line(char *line, size_t line_size, const WaterTemperatureReading &water_temperature) {
    if (!water_temperature.valid) {
        switch (water_temperature.status) {
            case WaterTemperatureStatus::not_read:
                snprintf(line, line_size, config::ENABLE_WATER_TEMPERATURE_SENSOR ? "Acqua: attesa" : "Acqua: OFF");
                break;
            case WaterTemperatureStatus::bus_stuck_low:
                snprintf(line, line_size, "Acqua: DAT LOW");
                break;
            case WaterTemperatureStatus::device_missing:
                snprintf(line, line_size, "Acqua: no pres");
                break;
            case WaterTemperatureStatus::conversion_started:
                snprintf(line, line_size, "Acqua: misura");
                break;
            case WaterTemperatureStatus::crc_error:
                snprintf(line, line_size, "Acqua: CRC err");
                break;
            case WaterTemperatureStatus::invalid_data:
                snprintf(line, line_size, "Acqua: dati err");
                break;
            case WaterTemperatureStatus::valid:
                snprintf(line, line_size, "Acqua: errore");
                break;
        }
        return;
    }

    snprintf(line, line_size, "Acqua: %.1f C", water_temperature.temperature_c);
}

void LcdCarousel::show(
    uint8_t page,
    const DhtReading &dht,
    const WaterTemperatureReading &water_temperature,
    const VemlReading &veml,
    const TdsReading &tds,
    bool level_present,
    bool flow_detected,
    float liters_per_minute,
    float total_liters
) {
    char line1[17];
    char line2[17];

    switch (page) {
        case 0:
            format_temperature_line(line1, sizeof(line1), dht);
            format_humidity_line(line2, sizeof(line2), dht);
            break;

        case 1:
            format_water_temperature_line(line1, sizeof(line1), water_temperature);
            snprintf(line2, sizeof(line2), "Sonda DS18B20");
            break;

        case 2:
            snprintf(line1, sizeof(line1), "Livello: %s", level_present ? "OK" : "BASSO");
            snprintf(line2, sizeof(line2), "Flusso: %s", flow_detected ? "SI" : "NO");
            break;

        case 3:
            if (veml.valid) {
                snprintf(line1, sizeof(line1), "Luce: %.0f lux", veml.lux);
                snprintf(line2, sizeof(line2), "Raw ALS: %u", veml.raw_als);
            } else {
                snprintf(line1, sizeof(line1), "Luce: attesa");
                snprintf(line2, sizeof(line2), "VEML7700 ERR");
            }
            break;

        case 4:
            if (tds.valid) {
                snprintf(line1, sizeof(line1), "TDS: %.0f ppm", tds.ppm);
                snprintf(line2, sizeof(line2), "Volt: %.2f V", tds.voltage);
            } else {
                snprintf(line1, sizeof(line1), "TDS: attesa");
                snprintf(line2, sizeof(line2), "ADC ERR");
            }
            break;

        default:
            snprintf(line1, sizeof(line1), "Flow: %.1f L/m", liters_per_minute);
            snprintf(line2, sizeof(line2), "Tot: %.3f L", total_liters);
            break;
    }

    lcd_->show_lines(line1, line2);
}

}
