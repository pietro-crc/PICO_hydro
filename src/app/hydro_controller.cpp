#include "app/hydro_controller.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <stdio.h>

namespace hydro {

HydroController::HydroController()
    : lcd_(i2c0),
      wifi_module_(uart0, config::ESP8266_UART_TX_PIN, config::ESP8266_UART_RX_PIN),
      veml_sensor_(i2c1),
      tds_sensor_(config::TDS_ADC_PIN, config::TDS_ADC_INPUT),
      water_temperature_sensor_(config::WATER_TEMPERATURE_PIN),
      dht_sensor_(config::DHT_PIN),
      flow_meter_(config::FLOW_SENSOR_PIN),
      level_sensor_(config::LEVEL_SENSOR_PIN),
      carousel_(&lcd_),
      google_logger_(&wifi_module_) {}

void HydroController::init_gpio() {
    gpio_init(config::LED_PIN);
    gpio_set_dir(config::LED_PIN, GPIO_OUT);
    gpio_put(config::LED_PIN, 0);

    level_sensor_.init();
    dht_sensor_.init();
    if (config::ENABLE_WATER_TEMPERATURE_SENSOR) {
        water_temperature_sensor_.init();
    }
}

void HydroController::init_i2c() {
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(config::LCD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(config::LCD_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(config::LCD_SDA_PIN);
    gpio_pull_up(config::LCD_SCL_PIN);

    i2c_init(i2c1, 100 * 1000);
    gpio_set_function(config::VEML_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(config::VEML_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(config::VEML_SDA_PIN);
    gpio_pull_up(config::VEML_SCL_PIN);
}

void HydroController::init_display() {
    if (!lcd_.available()) {
        return;
    }

    lcd_.show_lines("PICO Hydro", config::FIRMWARE_VERSION);
    sleep_ms(config::STARTUP_DIAGNOSTIC_SCREEN_MS);
    lcd_.show_lines("PICO Hydro", "Avvio...");

    if (config::ENABLE_LCD_DIAGNOSTIC) {
        lcd_.diagnostic_test();
    }
}

void HydroController::init_strip() {
    strip_.init(pio0, config::STRIP_PIN, config::STRIP_LED_COUNT);
    strip_.set_all(ws2812_color(0, 0, 0));
    if (config::ENABLE_STRIP_STARTUP_TEST) {
        strip_.test_startup();
    }
    update_strip(false, level_sensor_.water_present());
}

void HydroController::blink_onboard_led_startup() {
    for (uint i = 0; i < 5; ++i) {
        gpio_put(config::LED_PIN, 1);
        sleep_ms(150);
        gpio_put(config::LED_PIN, 0);
        sleep_ms(150);
    }
}

void HydroController::update_strip(bool flow_detected, bool level_present) {
    uint32_t flow_color = flow_detected
        ? ws2812_color(0, config::FLOW_GREEN_BRIGHTNESS, 0)
        : ws2812_color(0, 0, config::FLOW_IDLE_BLUE_BRIGHTNESS);

    uint32_t level_color = level_present
        ? ws2812_color(0, config::LEVEL_WATER_GREEN_BRIGHTNESS, 0)
        : ws2812_color(config::LEVEL_LOW_RED_BRIGHTNESS, 0, 0);

    strip_.set_flow_and_level(flow_color, level_color);
}

void HydroController::show_startup_progress(uint8_t percent, const char *status) {
    heartbeat_state_ = !heartbeat_state_;
    gpio_put(config::LED_PIN, heartbeat_state_ ? 1 : 0);

    if (config::ENABLE_SERIAL_LOG) {
        printf("Startup %u%%: %s\n", percent, status);
    }

    if (!lcd_.available()) {
        return;
    }

    char line1[17];
    char line2[17];
    snprintf(line1, sizeof(line1), "Avvio %3u%%", percent);
    snprintf(line2, sizeof(line2), "%s", status);
    lcd_.show_lines(line1, line2);
}

HydroController::StartupDiagnostics HydroController::run_startup_diagnostics(bool lcd_ok, bool veml_ok) {
    StartupDiagnostics diagnostics = {
        lcd_ok,
        veml_ok,
        false,
        false,
        level_sensor_.water_present(),
        {false, 0, 0.0f, 0.0f},
        {false, 0, 0.0f},
        {false, 0.0f, 0.0f},
        {false, false, WaterTemperatureStatus::not_read, 0.0f},
        0,
        {
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
        }
    };

    show_startup_progress(45, "VEML7700");
    diagnostics.veml_read_ok = veml_sensor_.read(&diagnostics.veml);
    show_startup_progress(55, diagnostics.veml_read_ok ? "VEML OK" : "VEML ERR");

    show_startup_progress(60, "DHT aria");
    diagnostics.dht_read_ok = dht_sensor_.read(&diagnostics.dht);
    show_startup_progress(70, diagnostics.dht_read_ok ? "DHT OK" : "DHT ERR");

    show_startup_progress(72, config::ENABLE_WATER_TEMPERATURE_SENSOR ? "Acqua async" : "Acqua OFF");
    if (config::ENABLE_WATER_TEMPERATURE_SENSOR) {
        water_temperature_count_ = water_temperature_sensor_.discover_devices();
        diagnostics.water_temperature_sensor_count = water_temperature_count_;
        diagnostics.water_temperature.status = water_temperature_count_ > 0
            ? WaterTemperatureStatus::not_read
            : water_temperature_sensor_.last_status();
        show_startup_progress(85, water_temperature_count_ > 0 ? "DS18B20 trov" : "DS18B20 NO");
    } else {
        diagnostics.water_temperature.status = WaterTemperatureStatus::not_read;
        show_startup_progress(85, "Sensore OFF");
    }

    if (config::ENABLE_ESP8266_WIFI) {
        show_startup_progress(86, "ESP8266 AT");
        diagnostics.wifi = wifi_module_.run_startup_diagnostic();
        wifi_ = diagnostics.wifi;
        if (config::ENABLE_GOOGLE_SHEETS_LOGGING && wifi_.module_present) {
            show_startup_progress(87, "WiFi casa");
            if (google_logger_.init()) {
                wifi_.status = Esp8266Status::wifi_join_ok;
                if (config::ENABLE_SERIAL_LOG) {
                    printf("WiFi home joined ssid=\"%s\"\n", config::GOOGLE_SHEETS_WIFI_SSID);
                }
            } else {
                wifi_.status = Esp8266Status::wifi_join_failed;
                wifi_.wifi_join_error_code = wifi_module_.last_wifi_join_error_code();
                wifi_.wifi_state = wifi_module_.last_wifi_state();
                wifi_.wifi_ap_seen = wifi_module_.last_wifi_ap_seen();
                wifi_failures_++;
                if (config::ENABLE_SERIAL_LOG) {
                    printf(
                        "WiFi home join failed ssid=\"%s\" ap_seen=%s cwjap_code=%d cwstate=%d\n",
                        config::GOOGLE_SHEETS_WIFI_SSID,
                        wifi_module_.last_wifi_ap_seen() ? "yes" : "no",
                        (int)wifi_module_.last_wifi_join_error_code(),
                        (int)wifi_module_.last_wifi_state()
                    );
                }
            }
        } else if (!wifi_.module_present) {
            wifi_failures_++;
        }
        diagnostics.wifi = wifi_;
        show_startup_progress(88, wifi_.status == Esp8266Status::wifi_join_ok ? "WiFi casa OK" : (wifi_.module_present ? "ESP AT OK" : "ESP no AT"));
    }

    show_startup_progress(90, "TDS ADC");
    diagnostics.tds = tds_sensor_.read();
    show_startup_progress(95, "TDS OK");

    if (diagnostics.veml_read_ok) {
        veml_ = diagnostics.veml;
        veml_last_ok_ms_ = to_ms_since_boot(get_absolute_time());
    }

    if (diagnostics.dht_read_ok) {
        dht_ = diagnostics.dht;
        dht_last_ok_ms_ = to_ms_since_boot(get_absolute_time());
    }

    if (diagnostics.water_temperature.device_present) {
        water_temperature_ = diagnostics.water_temperature;
    }

    tds_ = diagnostics.tds;
    return diagnostics;
}

void HydroController::show_startup_diagnostics(const StartupDiagnostics &diagnostics) {
    if (!config::ENABLE_STARTUP_DIAGNOSTICS || !lcd_.available()) {
        return;
    }

    char line1[17];
    char line2[17];
    uint8_t ok_count = 0;
    uint8_t startup_check_count = 4;

    if (config::ENABLE_WATER_TEMPERATURE_SENSOR) {
        startup_check_count++;
    }

    if (config::ENABLE_ESP8266_WIFI) {
        startup_check_count++;
    }

    if (diagnostics.lcd_ok) {
        ok_count++;
    }

    if (diagnostics.veml_read_ok) {
        ok_count++;
    }

    if (diagnostics.dht_read_ok) {
        ok_count++;
    }

    if (diagnostics.tds.valid) {
        ok_count++;
    }

    if (config::ENABLE_WATER_TEMPERATURE_SENSOR && diagnostics.water_temperature_sensor_count > 0) {
        ok_count++;
    }

    if (config::ENABLE_ESP8266_WIFI && diagnostics.wifi.module_present) {
        ok_count++;
    }

    snprintf(line1, sizeof(line1), "Diag %u/%u OK", ok_count, startup_check_count);
    if (config::ENABLE_ESP8266_WIFI) {
        snprintf(line2, sizeof(line2), "ESP %s", diagnostics.wifi.module_present ? "AT OK" : "no AT");
    } else {
        snprintf(line2, sizeof(line2), "DS18B20 async");
    }
    lcd_.show_lines(line1, line2);
    sleep_ms(config::STARTUP_DIAGNOSTIC_SCREEN_MS);
}

void HydroController::log_startup(const StartupDiagnostics &diagnostics) const {
    if (!config::ENABLE_SERIAL_LOG) {
        return;
    }

    printf("Startup diagnostics\n");
    printf("Firmware: %s\n", config::FIRMWARE_VERSION);
    printf("LCD I2C GP%u/GP%u: %s address=0x%02X\n", config::LCD_SDA_PIN, config::LCD_SCL_PIN, diagnostics.lcd_ok ? "OK" : "ERR", lcd_.address());
    printf("VEML7700 GP%u/GP%u: init=%s read=%s lux=%.0f raw=%u\n", config::VEML_SDA_PIN, config::VEML_SCL_PIN, diagnostics.veml_init_ok ? "OK" : "ERR", diagnostics.veml_read_ok ? "OK" : "ERR", diagnostics.veml.lux, diagnostics.veml.raw_als);
    printf("DHT GP%u: %s T=%.1fC H=%.1f%%\n", config::DHT_PIN, diagnostics.dht_read_ok ? "OK" : "ERR", diagnostics.dht.temperature_c, diagnostics.dht.humidity_percent);
    printf("DS18B20 GP%u: enabled=%s sensors=%u status=%u T=%.1fC\n", config::WATER_TEMPERATURE_PIN, config::ENABLE_WATER_TEMPERATURE_SENSOR ? "SI" : "NO", diagnostics.water_temperature_sensor_count, (unsigned)diagnostics.water_temperature.status, diagnostics.water_temperature.temperature_c);
    printf("ESP8266 UART0 GP%u->RX GP%u<-TX: enabled=%s present=%s baud=%lu echo=%s gmr=%s status=%u\n", config::ESP8266_UART_TX_PIN, config::ESP8266_UART_RX_PIN, config::ENABLE_ESP8266_WIFI ? "SI" : "NO", diagnostics.wifi.module_present ? "SI" : "NO", (unsigned long)diagnostics.wifi.baudrate, diagnostics.wifi.echo_disabled ? "OK" : "NO", diagnostics.wifi.firmware_queried ? "OK" : "NO", (unsigned)diagnostics.wifi.status);
    printf("TDS GP%u ADC%u: raw=%u voltage=%.2fV ppm=%.0f\n", config::TDS_ADC_PIN, config::TDS_ADC_INPUT, diagnostics.tds.raw_adc, diagnostics.tds.voltage, diagnostics.tds.ppm);
    printf("Livello GP%u: %s\n", config::LEVEL_SENSOR_PIN, diagnostics.level_present ? "OK" : "BASSO");
    printf("Flusso GP%u: attivo, verifica muovendo acqua\n", config::FLOW_SENSOR_PIN);
    printf("Striscia WS2812 su GP%u con %u LED\n", config::STRIP_PIN, config::STRIP_LED_COUNT);
}

void HydroController::log_sample(bool level_present, bool flow_detected, float liters_per_minute) const {
    if (!config::ENABLE_SERIAL_LOG) {
        return;
    }

    printf(
        "DHT: %s T=%.1fC H=%.1f%% | Acqua: %s %.1fC | Luce: %s %.0f lux raw=%u | TDS: %.0f ppm %.2fV raw=%u | Livello: %s | Flow: %s %.2f L/min Tot=%.3f L\n",
        dht_.valid ? "OK" : "ERR",
        dht_.temperature_c,
        dht_.humidity_percent,
        water_temperature_.valid ? "OK" : (water_temperature_.device_present ? "ERR" : "NO"),
        water_temperature_.temperature_c,
        veml_.valid ? "OK" : "ERR",
        veml_.lux,
        veml_.raw_als,
        tds_.ppm,
        tds_.voltage,
        tds_.raw_adc,
        level_present ? "OK" : "BASSO",
        flow_detected ? "SI" : "NO",
        liters_per_minute,
        total_liters_
    );
}

void HydroController::log_health(uint64_t now_ms, bool level_present) const {
    if (!config::ENABLE_SERIAL_LOG) {
        return;
    }

    printf(
        "Health uptime=%llus fail[DHT=%lu VEML=%lu H2O=%lu WiFi=%lu] valid[DHT=%s VEML=%s H2O=%s] level=%s flow=%s total=%.3fL\n",
        (unsigned long long)(now_ms / 1000),
        (unsigned long)dht_failures_,
        (unsigned long)veml_failures_,
        (unsigned long)water_temperature_failures_,
        (unsigned long)wifi_failures_,
        dht_.valid ? "SI" : "NO",
        veml_.valid ? "SI" : "NO",
        water_temperature_.valid ? "SI" : "NO",
        level_present ? "OK" : "BASSO",
        flow_detected_ ? "SI" : "NO",
        total_liters_
    );
}

void HydroController::send_remote_log(uint64_t now_ms, bool level_present) {
    if (!config::ENABLE_GOOGLE_SHEETS_LOGGING || !wifi_.module_present) {
        return;
    }

    HydroTelemetry telemetry = {
        now_ms,
        &dht_,
        water_temperatures_,
        water_temperature_count_,
        &veml_,
        &tds_,
        level_present,
        flow_detected_,
        liters_per_minute_,
        total_liters_,
        dht_failures_,
        veml_failures_,
        water_temperature_failures_,
        wifi_failures_
    };

    if (google_logger_.send(telemetry)) {
        wifi_.status = Esp8266Status::http_send_ok;
        if (config::ENABLE_SERIAL_LOG) {
            printf("Google Sheets log OK uptime=%llus\n", (unsigned long long)(now_ms / 1000));
        }
    } else {
        wifi_failures_++;
        wifi_.status = wifi_module_.last_status();
        wifi_.ssl_dns_ok = wifi_module_.last_ssl_dns_ok();
        wifi_.ssl_sni_ok = wifi_module_.last_ssl_sni_ok();
        wifi_.ssl_errno = wifi_module_.last_ssl_errno();
        if (config::ENABLE_SERIAL_LOG) {
            printf(
                "Google Sheets log ERR uptime=%llus status=%u tls[dns=%s sni=%s errno=%d] transport=%s detail=%lu\n",
                (unsigned long long)(now_ms / 1000),
                (unsigned)wifi_.status,
                wifi_.ssl_dns_ok ? "OK" : "NO",
                wifi_.ssl_sni_ok ? "OK" : "NO",
                (int)wifi_.ssl_errno,
                wifi_module_.last_transport_error_text(),
                (unsigned long)wifi_module_.last_transport_detail()
            );
        }
    }
}

void HydroController::expire_stale_readings(uint64_t now_ms) {
    if (dht_.valid && dht_last_ok_ms_ > 0 && now_ms - dht_last_ok_ms_ > config::DHT_READING_STALE_MS) {
        dht_.valid = false;
    }

    if (veml_.valid && veml_last_ok_ms_ > 0 && now_ms - veml_last_ok_ms_ > config::VEML_READING_STALE_MS) {
        veml_.valid = false;
    }

    if (water_temperature_.valid &&
        water_temperature_last_ok_ms_ > 0 &&
        now_ms - water_temperature_last_ok_ms_ > config::WATER_TEMPERATURE_STALE_MS) {
        water_temperature_.valid = false;
        for (uint8_t i = 0; i < water_temperature_count_; ++i) {
            water_temperatures_[i].valid = false;
        }
    }
}

void HydroController::init() {
    stdio_init_all();
    if (config::ENABLE_STARTUP_DELAY) {
        sleep_ms(2000);
    }

    init_gpio();
    show_startup_progress(10, "GPIO OK");

    if (config::ENABLE_ONBOARD_LED_STARTUP) {
        blink_onboard_led_startup();
    }

    init_i2c();
    show_startup_progress(25, "I2C pronto");

    if (config::ENABLE_ESP8266_WIFI) {
        wifi_module_.init(config::ESP8266_UART_BAUDRATE);
        show_startup_progress(28, "UART ESP init");
    }

    tds_sensor_.init();
    show_startup_progress(30, "ADC pronto");

    bool lcd_ok = lcd_.init();
    init_display();
    show_startup_progress(35, lcd_ok ? "LCD OK" : "LCD ERR");

    bool veml_ok = veml_sensor_.init();
    show_startup_progress(40, veml_ok ? "VEML init OK" : "VEML init ERR");

    init_strip();
    show_startup_progress(42, "LED strip OK");

    StartupDiagnostics diagnostics = run_startup_diagnostics(lcd_ok, veml_ok);
    show_startup_diagnostics(diagnostics);
    log_startup(diagnostics);

    flow_meter_.init();
    flow_meter_.get_and_reset_pulses();
    show_startup_progress(100, "Loop avviato");
    sleep_ms(config::STARTUP_DIAGNOSTIC_SCREEN_MS);
}

[[noreturn]] void HydroController::run_forever() {
    uint64_t next_sample_ms = 0;
    uint64_t next_dht_read_ms = 0;
    uint64_t next_water_temperature_read_ms =
        to_ms_since_boot(get_absolute_time()) + config::WATER_TEMPERATURE_FIRST_READ_DELAY_MS;
    uint64_t next_veml_read_ms = 0;
    uint64_t next_tds_read_ms = 0;
    uint64_t next_lcd_update_ms = 0;
    uint64_t next_sample_log_ms = 0;
    uint64_t next_health_log_ms = config::HEALTH_LOG_INTERVAL_MS;

    while (true) {
        uint64_t now_ms = to_ms_since_boot(get_absolute_time());
        bool level_present = level_sensor_.water_present();
        expire_stale_readings(now_ms);

        if (now_ms >= next_sample_ms) {
            uint32_t pulses = flow_meter_.get_and_reset_pulses();
            float sample_seconds = config::SAMPLE_TIME_MS / 1000.0f;
            float liters_this_sample = pulses / config::PULSES_PER_LITER;

            liters_per_minute_ = liters_this_sample * (60.0f / sample_seconds);
            total_liters_ += liters_this_sample;
            flow_detected_ = pulses > 0;

            heartbeat_state_ = !heartbeat_state_;
            gpio_put(config::LED_PIN, heartbeat_state_ ? 1 : 0);
            update_strip(flow_detected_, level_present);
            if (now_ms >= next_sample_log_ms) {
                log_sample(level_present, flow_detected_, liters_per_minute_);
                next_sample_log_ms = now_ms + config::SERIAL_SAMPLE_LOG_INTERVAL_MS;
            }

            next_sample_ms = now_ms + config::SAMPLE_TIME_MS;
        }

        if (now_ms >= next_dht_read_ms) {
            DhtReading new_reading;
            if (dht_sensor_.read(&new_reading)) {
                dht_ = new_reading;
                dht_last_ok_ms_ = now_ms;
            } else if (!dht_.valid) {
                dht_.valid = false;
                dht_failures_++;
            } else {
                dht_failures_++;
            }

            next_dht_read_ms = now_ms + config::DHT_READ_INTERVAL_MS;
        }

        if (now_ms >= next_water_temperature_read_ms) {
            if (!config::ENABLE_WATER_TEMPERATURE_SENSOR) {
                next_water_temperature_read_ms = now_ms + config::WATER_TEMPERATURE_READ_INTERVAL_MS;
            } else if (!water_temperature_conversion_pending_) {
                if (water_temperature_count_ == 0) {
                    water_temperature_count_ = water_temperature_sensor_.discover_devices();
                }

                bool conversion_started = water_temperature_sensor_.start_temperature_conversion();
                if (conversion_started) {
                    water_temperature_ = {false, true, WaterTemperatureStatus::conversion_started, 0.0f};
                    for (uint8_t i = 0; i < water_temperature_count_; ++i) {
                        water_temperatures_[i] = water_temperature_;
                    }
                    water_temperature_conversion_pending_ = true;
                    water_temperature_ready_ms_ = now_ms + config::DS18B20_CONVERSION_TIME_MS;
                    next_water_temperature_read_ms = water_temperature_ready_ms_;
                } else {
                    water_temperature_ = {false, false, water_temperature_sensor_.last_status(), 0.0f};
                    water_temperature_failures_++;
                    next_water_temperature_read_ms = now_ms + config::WATER_TEMPERATURE_RETRY_DELAY_MS;
                }
            } else if (now_ms >= water_temperature_ready_ms_) {
                bool any_valid_reading = false;

                for (uint8_t i = 0; i < water_temperature_count_; ++i) {
                    WaterTemperatureReading new_reading = water_temperature_sensor_.read_converted_temperature_c(i);
                    if (new_reading.device_present) {
                        water_temperatures_[i] = new_reading;
                    } else if (!water_temperatures_[i].valid) {
                        water_temperatures_[i] = new_reading;
                    }

                    if (water_temperatures_[i].valid && !any_valid_reading) {
                        water_temperature_ = water_temperatures_[i];
                        water_temperature_last_ok_ms_ = now_ms;
                        any_valid_reading = true;
                    }
                }

                if (!any_valid_reading && water_temperature_count_ > 0) {
                    water_temperature_ = water_temperatures_[0];
                    water_temperature_failures_++;
                }

                water_temperature_conversion_pending_ = false;
                next_water_temperature_read_ms = now_ms + (
                    any_valid_reading
                        ? config::WATER_TEMPERATURE_READ_INTERVAL_MS
                        : config::WATER_TEMPERATURE_RETRY_DELAY_MS
                );
            }
        }

        if (now_ms >= next_veml_read_ms) {
            VemlReading new_light_reading;
            if (veml_sensor_.read(&new_light_reading)) {
                veml_ = new_light_reading;
                veml_last_ok_ms_ = now_ms;
            } else if (!veml_.valid) {
                veml_.valid = false;
                veml_failures_++;
            } else {
                veml_failures_++;
            }

            next_veml_read_ms = now_ms + config::VEML_READ_INTERVAL_MS;
        }

        if (now_ms >= next_tds_read_ms) {
            tds_ = tds_sensor_.read();
            next_tds_read_ms = now_ms + config::TDS_READ_INTERVAL_MS;
        }

        if (now_ms >= next_remote_log_ms_) {
            send_remote_log(now_ms, level_present);
            next_remote_log_ms_ = now_ms + config::GOOGLE_SHEETS_LOG_INTERVAL_MS;
        }

        if (lcd_.available() && now_ms >= next_lcd_update_ms) {
            carousel_.show(lcd_page_, dht_, water_temperatures_, water_temperature_count_, water_temperature_lcd_index_, veml_, tds_, level_present, flow_detected_, wifi_, liters_per_minute_, total_liters_);
            if (lcd_page_ == 1 && water_temperature_count_ > 0) {
                uint8_t water_temperature_step = water_temperature_count_ > 2 ? 2 : water_temperature_count_;
                water_temperature_lcd_index_ = (water_temperature_lcd_index_ + water_temperature_step) % water_temperature_count_;
            }
            lcd_page_ = (lcd_page_ + 1) % 7;
            next_lcd_update_ms = now_ms + config::LCD_CAROUSEL_INTERVAL_MS;
        }

        if (now_ms >= next_health_log_ms) {
            log_health(now_ms, level_present);
            next_health_log_ms = now_ms + config::HEALTH_LOG_INTERVAL_MS;
        }

        sleep_ms(20);
    }
}

}
