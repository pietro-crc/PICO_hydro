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

void LcdCarousel::format_water_temperature_line(char *line, size_t line_size, const WaterTemperatureReading &water_temperature, uint8_t index) {
    if (!water_temperature.valid) {
        switch (water_temperature.status) {
            case WaterTemperatureStatus::not_read:
                snprintf(line, line_size, config::ENABLE_WATER_TEMPERATURE_SENSOR ? "Acq%u: attesa" : "Acqua: OFF", index + 1);
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
            case WaterTemperatureStatus::stale:
                snprintf(line, line_size, "Acqua: scaduta");
                break;
            case WaterTemperatureStatus::valid:
                snprintf(line, line_size, "Acqua: errore");
                break;
        }
        return;
    }

    snprintf(line, line_size, "Acq%u: %.1f C", index + 1, water_temperature.temperature_c);
}

void LcdCarousel::format_wifi_line(char *line, size_t line_size, const Esp8266Diagnostic &wifi) {
    if (!wifi.enabled) {
        snprintf(line, line_size, "WiFi: OFF");
        return;
    }

    if (!wifi.module_present) {
        snprintf(line, line_size, "ESP: no AT");
        return;
    }

    switch (wifi.status) {
        case Esp8266Status::disabled:
            snprintf(line, line_size, "WiFi: OFF");
            break;
        case Esp8266Status::not_checked:
            snprintf(line, line_size, "WiFi: attesa");
            break;
        case Esp8266Status::uart_timeout:
            snprintf(line, line_size, "ESP: timeout");
            break;
        case Esp8266Status::at_ok:
            snprintf(line, line_size, "ESP: AT OK");
            break;
        case Esp8266Status::echo_off_ok:
            snprintf(line, line_size, "ESP: echo OK");
            break;
        case Esp8266Status::firmware_ok:
            snprintf(line, line_size, "ESP: fw OK");
            break;
        case Esp8266Status::wifi_join_ok:
            snprintf(line, line_size, "WiFi casa OK");
            break;
        case Esp8266Status::wifi_join_failed:
            snprintf(line, line_size, "No WiFi casa");
            break;
        case Esp8266Status::clock_sync_failed:
            snprintf(line, line_size, "NTP: errore");
            break;
        case Esp8266Status::tls_trust_store_missing:
            snprintf(line, line_size, "TLS: config");
            break;
        case Esp8266Status::ssl_open_ok:
            snprintf(line, line_size, "SSL: OK");
            break;
        case Esp8266Status::ssl_failed:
            snprintf(line, line_size, "SSL: ERR");
            break;
        case Esp8266Status::http_send_ok:
            snprintf(line, line_size, "Cloud: OK");
            break;
        case Esp8266Status::http_send_failed:
            snprintf(line, line_size, "Cloud: ERR");
            break;
    }
}

void LcdCarousel::show(
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
) {
    char line1[17];
    char line2[17];

    switch (page) {
        case 0:
            format_temperature_line(line1, sizeof(line1), dht);
            format_humidity_line(line2, sizeof(line2), dht);
            break;

        case 1:
            if (water_temperature_count == 0 || water_temperatures == nullptr) {
                WaterTemperatureReading missing = {false, false, WaterTemperatureStatus::device_missing, 0.0f};
                format_water_temperature_line(line1, sizeof(line1), missing, 0);
                snprintf(line2, sizeof(line2), "DS18B20 GP%u", config::WATER_TEMPERATURE_PIN);
            } else {
                uint8_t safe_index = water_temperature_index % water_temperature_count;
                format_water_temperature_line(line1, sizeof(line1), water_temperatures[safe_index], safe_index);
                if (water_temperature_count == 1) {
                    snprintf(line2, sizeof(line2), "1 sonda GP%u", config::WATER_TEMPERATURE_PIN);
                } else {
                    uint8_t next_index = (safe_index + 1) % water_temperature_count;
                    format_water_temperature_line(line2, sizeof(line2), water_temperatures[next_index], next_index);
                }
            }
            break;

        case 2:
            snprintf(line1, sizeof(line1), "Livello: %s", level_present ? "SI" : "BASSO");
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

        case 5:
            format_wifi_line(line1, sizeof(line1), wifi);
            if (wifi.status == Esp8266Status::wifi_join_failed) {
                snprintf(line2, sizeof(line2), "AP:%s CW:%d", wifi.wifi_ap_seen ? "SI" : "NO", (int)wifi.wifi_join_error_code);
            } else if (wifi.status == Esp8266Status::ssl_failed) {
                if (!wifi.ssl_sni_ok) {
                    snprintf(line2, sizeof(line2), "TLS SNI NO");
                } else if (!wifi.ssl_dns_ok) {
                    snprintf(line2, sizeof(line2), "TLS DNS NO");
                } else if (wifi.ssl_errno != -1) {
                    snprintf(line2, sizeof(line2), "TLS ERRNO %d", (int)wifi.ssl_errno);
                } else {
                    snprintf(line2, sizeof(line2), "TLS Google ERR");
                }
            } else if (wifi.status == Esp8266Status::http_send_failed) {
                snprintf(line2, sizeof(line2), "HTTP Google ERR");
            } else if (wifi.status == Esp8266Status::http_send_ok) {
                snprintf(line2, sizeof(line2), "Google salvato");
            } else {
                snprintf(line2, sizeof(line2), "TX%u RX%u", config::ESP8266_UART_TX_PIN, config::ESP8266_UART_RX_PIN);
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
