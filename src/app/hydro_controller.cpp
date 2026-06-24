#include "app/hydro_controller.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include <stdio.h>

namespace hydro {

HydroController::HydroController()
    : lcd_(i2c0),
      veml_sensor_(i2c1),
      tds_sensor_(config::TDS_ADC_PIN, config::TDS_ADC_INPUT),
      water_temperature_sensor_(config::WATER_TEMPERATURE_PIN),
      dht_sensor_(config::DHT_PIN),
      flow_meter_(config::FLOW_SENSOR_PIN),
      level_sensor_(config::LEVEL_SENSOR_PIN),
      carousel_(&lcd_) {}

void HydroController::init_gpio() {
    gpio_init(config::LED_PIN);
    gpio_set_dir(config::LED_PIN, GPIO_OUT);
    gpio_put(config::LED_PIN, 0);

    level_sensor_.init();
    dht_sensor_.init();
    water_temperature_sensor_.init();
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
        false,
        level_sensor_.water_present(),
        {false, 0, 0.0f, 0.0f},
        {false, 0, 0.0f},
        {false, 0.0f, 0.0f},
        {false, false, WaterTemperatureStatus::not_read, 0.0f}
    };

    show_startup_progress(45, "VEML7700");
    diagnostics.veml_read_ok = veml_sensor_.read(&diagnostics.veml);
    show_startup_progress(55, diagnostics.veml_read_ok ? "VEML OK" : "VEML ERR");

    show_startup_progress(60, "DHT aria");
    diagnostics.dht_read_ok = dht_sensor_.read(&diagnostics.dht);
    show_startup_progress(70, diagnostics.dht_read_ok ? "DHT OK" : "DHT ERR");

    show_startup_progress(72, "Acqua nel loop");
    diagnostics.water_temperature_read_ok = false;
    show_startup_progress(85, "DS18B20 skip");

    show_startup_progress(90, "TDS ADC");
    diagnostics.tds = tds_sensor_.read();
    show_startup_progress(95, "TDS OK");

    if (diagnostics.veml_read_ok) {
        veml_ = diagnostics.veml;
    }

    if (diagnostics.dht_read_ok) {
        dht_ = diagnostics.dht;
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

    if (diagnostics.lcd_ok) {
        ok_count++;
    }

    if (diagnostics.veml_read_ok) {
        ok_count++;
    }

    if (diagnostics.dht_read_ok) {
        ok_count++;
    }

    if (diagnostics.water_temperature_read_ok) {
        ok_count++;
    }

    if (diagnostics.tds.valid) {
        ok_count++;
    }

    snprintf(line1, sizeof(line1), "Diag %u/5 OK", ok_count);
    snprintf(line2, sizeof(line2), "Liv %s Flow att", diagnostics.level_present ? "OK" : "BASSO");
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
    printf("DS18B20 GP%u: present=%s read=%s status=%u T=%.1fC\n", config::WATER_TEMPERATURE_PIN, diagnostics.water_temperature.device_present ? "SI" : "NO", diagnostics.water_temperature_read_ok ? "OK" : "ERR", (unsigned)diagnostics.water_temperature.status, diagnostics.water_temperature.temperature_c);
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

    while (true) {
        uint64_t now_ms = to_ms_since_boot(get_absolute_time());
        bool level_present = level_sensor_.water_present();

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
            log_sample(level_present, flow_detected_, liters_per_minute_);

            next_sample_ms = now_ms + config::SAMPLE_TIME_MS;
        }

        if (now_ms >= next_dht_read_ms) {
            DhtReading new_reading;
            if (dht_sensor_.read(&new_reading)) {
                dht_ = new_reading;
            } else if (!dht_.valid) {
                dht_.valid = false;
            }

            next_dht_read_ms = now_ms + config::DHT_READ_INTERVAL_MS;
        }

        if (now_ms >= next_water_temperature_read_ms) {
            if (!config::ENABLE_WATER_TEMPERATURE_SENSOR) {
                next_water_temperature_read_ms = now_ms + config::WATER_TEMPERATURE_READ_INTERVAL_MS;
            } else if (!water_temperature_conversion_pending_) {
                bool conversion_started = water_temperature_sensor_.start_temperature_conversion();
                if (conversion_started) {
                    water_temperature_.device_present = true;
                    water_temperature_conversion_pending_ = true;
                    water_temperature_ready_ms_ = now_ms + config::DS18B20_CONVERSION_TIME_MS;
                    next_water_temperature_read_ms = water_temperature_ready_ms_;
                } else {
                    water_temperature_ = {false, false, water_temperature_sensor_.last_status(), 0.0f};
                    next_water_temperature_read_ms = now_ms + config::WATER_TEMPERATURE_READ_INTERVAL_MS;
                }
            } else if (now_ms >= water_temperature_ready_ms_) {
                WaterTemperatureReading new_reading = water_temperature_sensor_.read_converted_temperature_c();
                if (new_reading.device_present) {
                    water_temperature_ = new_reading;
                } else if (!water_temperature_.valid) {
                    water_temperature_ = new_reading;
                }

                water_temperature_conversion_pending_ = false;
                next_water_temperature_read_ms = now_ms + config::WATER_TEMPERATURE_READ_INTERVAL_MS;
            }
        }

        if (now_ms >= next_veml_read_ms) {
            VemlReading new_light_reading;
            if (veml_sensor_.read(&new_light_reading)) {
                veml_ = new_light_reading;
            } else if (!veml_.valid) {
                veml_.valid = false;
            }

            next_veml_read_ms = now_ms + config::VEML_READ_INTERVAL_MS;
        }

        if (now_ms >= next_tds_read_ms) {
            tds_ = tds_sensor_.read();
            next_tds_read_ms = now_ms + config::TDS_READ_INTERVAL_MS;
        }

        if (lcd_.available() && now_ms >= next_lcd_update_ms) {
            carousel_.show(lcd_page_, dht_, water_temperature_, veml_, tds_, level_present, flow_detected_, liters_per_minute_, total_liters_);
            lcd_page_ = (lcd_page_ + 1) % 6;
            next_lcd_update_ms = now_ms + config::LCD_CAROUSEL_INTERVAL_MS;
        }

        sleep_ms(20);
    }
}

}
