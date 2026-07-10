#include "sensors/tds_sensor.h"

#include "config/hardware_config.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"

#include <cmath>

namespace hydro {

TdsSensor::TdsSensor(uint adc_pin, uint adc_input) : adc_pin_(adc_pin), adc_input_(adc_input) {}

void TdsSensor::init() {
    adc_init();
    adc_gpio_init(adc_pin_);
}

TdsReading TdsSensor::read(float water_temperature_c) {
    adc_select_input(adc_input_);

    uint32_t total = 0;
    static constexpr uint8_t samples = 10;

    for (uint8_t i = 0; i < samples; ++i) {
        total += adc_read();
        sleep_ms(5);
    }

    uint16_t raw = (uint16_t)(total / samples);
    float voltage = ((float)raw / config::ADC_MAX_VALUE) * config::PICO_ADC_REFERENCE_VOLTAGE;
    float compensation_temperature_c = config::TDS_TEMPERATURE_COMPENSATION_C;
    if (std::isfinite(water_temperature_c) &&
        water_temperature_c >= config::TDS_COMPENSATION_MIN_TEMPERATURE_C &&
        water_temperature_c <= config::TDS_COMPENSATION_MAX_TEMPERATURE_C) {
        compensation_temperature_c = water_temperature_c;
    }

    float compensation_coefficient = 1.0f + 0.02f * (compensation_temperature_c - 25.0f);
    float compensation_voltage = voltage / compensation_coefficient;
    float ppm = (133.42f * compensation_voltage * compensation_voltage * compensation_voltage
        - 255.86f * compensation_voltage * compensation_voltage
        + 857.39f * compensation_voltage) * 0.5f;

    if (ppm < 0.0f) {
        ppm = 0.0f;
    }

    return {true, raw, voltage, ppm};
}

}
