#include "sensors/ds18b20_sensor.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace hydro {
namespace {

constexpr uint8_t DS18B20_CMD_SKIP_ROM = 0xCC;
constexpr uint8_t DS18B20_CMD_CONVERT_T = 0x44;
constexpr uint8_t DS18B20_CMD_READ_SCRATCHPAD = 0xBE;

}

Ds18b20Sensor::Ds18b20Sensor(uint pin) : pin_(pin) {}

void Ds18b20Sensor::init() {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    last_status_ = WaterTemperatureStatus::not_read;
}

bool Ds18b20Sensor::detect_device() {
    return reset_bus();
}

WaterTemperatureStatus Ds18b20Sensor::last_status() const {
    return last_status_;
}

bool Ds18b20Sensor::start_temperature_conversion() {
    if (!reset_bus()) {
        return false;
    }

    write_byte(DS18B20_CMD_SKIP_ROM);
    write_byte(DS18B20_CMD_CONVERT_T);
    last_status_ = WaterTemperatureStatus::conversion_started;
    return true;
}

bool Ds18b20Sensor::reset_bus() {
    uint32_t interrupt_state = save_and_disable_interrupts();

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    sleep_us(10);

    if (!gpio_get(pin_)) {
        restore_interrupts(interrupt_state);
        last_status_ = WaterTemperatureStatus::bus_stuck_low;
        return false;
    }

    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);
    sleep_us(480);

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    sleep_us(70);

    bool device_present = !gpio_get(pin_);
    sleep_us(410);

    restore_interrupts(interrupt_state);
    if (!device_present) {
        last_status_ = WaterTemperatureStatus::device_missing;
    }
    return device_present;
}

void Ds18b20Sensor::write_bit(bool value) {
    uint32_t interrupt_state = save_and_disable_interrupts();

    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);

    if (value) {
        sleep_us(6);
        gpio_set_dir(pin_, GPIO_IN);
        gpio_pull_up(pin_);
        sleep_us(64);
    } else {
        sleep_us(60);
        gpio_set_dir(pin_, GPIO_IN);
        gpio_pull_up(pin_);
        sleep_us(10);
    }

    restore_interrupts(interrupt_state);
}

bool Ds18b20Sensor::read_bit() {
    uint32_t interrupt_state = save_and_disable_interrupts();

    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);
    sleep_us(6);

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    sleep_us(9);

    bool value = gpio_get(pin_);
    sleep_us(55);

    restore_interrupts(interrupt_state);
    return value;
}

void Ds18b20Sensor::write_byte(uint8_t value) {
    for (uint8_t i = 0; i < 8; ++i) {
        write_bit((value & 0x01) != 0);
        value >>= 1;
    }
}

uint8_t Ds18b20Sensor::read_byte() {
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; ++i) {
        if (read_bit()) {
            value |= (uint8_t)(1u << i);
        }
    }

    return value;
}

uint8_t Ds18b20Sensor::crc8(const uint8_t *data, uint8_t length) {
    uint8_t crc = 0;

    for (uint8_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            uint8_t mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix != 0) {
                crc ^= 0x8C;
            }
            byte >>= 1;
        }
    }

    return crc;
}

WaterTemperatureReading Ds18b20Sensor::read_temperature_c() {
    WaterTemperatureReading reading = {false, false, WaterTemperatureStatus::not_read, 0.0f};

    if (!start_temperature_conversion()) {
        reading.status = last_status_;
        return reading;
    }

    reading.device_present = true;
    reading.status = WaterTemperatureStatus::conversion_started;
    sleep_ms(config::DS18B20_CONVERSION_TIME_MS);
    return read_converted_temperature_c();
}

WaterTemperatureReading Ds18b20Sensor::read_converted_temperature_c() {
    WaterTemperatureReading reading = {false, false, WaterTemperatureStatus::not_read, 0.0f};

    if (!reset_bus()) {
        reading.status = last_status_;
        return reading;
    }

    reading.device_present = true;
    write_byte(DS18B20_CMD_SKIP_ROM);
    write_byte(DS18B20_CMD_READ_SCRATCHPAD);

    uint8_t scratchpad[9];
    for (uint8_t i = 0; i < sizeof(scratchpad); ++i) {
        scratchpad[i] = read_byte();
    }

    bool all_zero = true;
    bool all_one = true;
    for (uint8_t i = 0; i < sizeof(scratchpad); ++i) {
        all_zero = all_zero && scratchpad[i] == 0x00;
        all_one = all_one && scratchpad[i] == 0xFF;
    }

    if (all_zero || all_one) {
        reading.status = WaterTemperatureStatus::invalid_data;
        last_status_ = reading.status;
        return reading;
    }

    if (crc8(scratchpad, 8) != scratchpad[8]) {
        reading.status = WaterTemperatureStatus::crc_error;
        last_status_ = reading.status;
        return reading;
    }

    int16_t raw_temperature = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    reading.temperature_c = raw_temperature / 16.0f;

    if (reading.temperature_c < -55.0f || reading.temperature_c > 125.0f) {
        reading.status = WaterTemperatureStatus::invalid_data;
        last_status_ = reading.status;
        return reading;
    }

    reading.valid = true;
    reading.status = WaterTemperatureStatus::valid;
    last_status_ = reading.status;
    return reading;
}

}
