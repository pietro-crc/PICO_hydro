#include "sensors/ds18b20_sensor.h"

#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

namespace hydro {
namespace {

constexpr uint8_t DS18B20_CMD_SKIP_ROM = 0xCC;
constexpr uint8_t DS18B20_CMD_SEARCH_ROM = 0xF0;
constexpr uint8_t DS18B20_CMD_MATCH_ROM = 0x55;
constexpr uint8_t DS18B20_CMD_CONVERT_T = 0x44;
constexpr uint8_t DS18B20_CMD_READ_SCRATCHPAD = 0xBE;
constexpr uint8_t DS18B20_FAMILY_CODE = 0x28;

inline void ow_delay_us(uint32_t microseconds) {
    busy_wait_us_32(microseconds);
}

}

Ds18b20Sensor::Ds18b20Sensor(uint pin) : pin_(pin) {}

void Ds18b20Sensor::init() {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    device_count_ = 0;
    last_status_ = WaterTemperatureStatus::not_read;
}

uint8_t Ds18b20Sensor::discover_devices() {
    device_count_ = 0;

    Ds18b20Address rom = {};
    uint8_t last_discrepancy = 0;
    bool last_device = false;

    while (!last_device && device_count_ < config::MAX_WATER_TEMPERATURE_SENSORS) {
        if (!reset_bus()) {
            return device_count_;
        }

        write_byte(DS18B20_CMD_SEARCH_ROM);
        uint8_t discrepancy_marker = 0;

        for (uint8_t bit_number = 1; bit_number <= 64; ++bit_number) {
            bool id_bit = read_bit();
            bool complement_bit = read_bit();

            if (id_bit && complement_bit) {
                device_count_ = 0;
                last_status_ = WaterTemperatureStatus::invalid_data;
                return 0;
            }

            bool direction;
            uint8_t byte_index = (uint8_t)((bit_number - 1) / 8);
            uint8_t bit_mask = (uint8_t)(1u << ((bit_number - 1) % 8));

            if (id_bit != complement_bit) {
                direction = id_bit;
            } else {
                if (bit_number < last_discrepancy) {
                    direction = (rom.bytes[byte_index] & bit_mask) != 0;
                } else {
                    direction = bit_number == last_discrepancy;
                }

                if (!direction) {
                    discrepancy_marker = bit_number;
                }
            }

            if (direction) {
                rom.bytes[byte_index] |= bit_mask;
            } else {
                rom.bytes[byte_index] &= (uint8_t)~bit_mask;
            }

            write_bit(direction);
        }

        last_discrepancy = discrepancy_marker;
        last_device = last_discrepancy == 0;

        if (rom.bytes[0] == DS18B20_FAMILY_CODE && crc8(rom.bytes, 7) == rom.bytes[7]) {
            devices_[device_count_] = rom;
            device_count_++;
        } else {
            last_status_ = WaterTemperatureStatus::invalid_data;
        }
    }

    last_status_ = device_count_ > 0
        ? WaterTemperatureStatus::not_read
        : WaterTemperatureStatus::device_missing;
    return device_count_;
}

uint8_t Ds18b20Sensor::device_count() const {
    return device_count_;
}

const Ds18b20Address &Ds18b20Sensor::device_address(uint8_t index) const {
    return devices_[index];
}

WaterTemperatureStatus Ds18b20Sensor::last_status() const {
    return last_status_;
}

bool Ds18b20Sensor::start_temperature_conversion() {
    if (device_count_ == 0) {
        discover_devices();
    }

    if (device_count_ == 0) {
        last_status_ = WaterTemperatureStatus::device_missing;
        return false;
    }

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
    ow_delay_us(10);

    if (!gpio_get(pin_)) {
        restore_interrupts(interrupt_state);
        last_status_ = WaterTemperatureStatus::bus_stuck_low;
        return false;
    }

    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);
    ow_delay_us(480);

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    ow_delay_us(70);

    bool device_present = !gpio_get(pin_);
    ow_delay_us(410);

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
        ow_delay_us(6);
        gpio_set_dir(pin_, GPIO_IN);
        gpio_pull_up(pin_);
        ow_delay_us(64);
    } else {
        ow_delay_us(60);
        gpio_set_dir(pin_, GPIO_IN);
        gpio_pull_up(pin_);
        ow_delay_us(10);
    }

    restore_interrupts(interrupt_state);
}

bool Ds18b20Sensor::read_bit() {
    uint32_t interrupt_state = save_and_disable_interrupts();

    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);
    ow_delay_us(2);

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
    ow_delay_us(10);

    bool value = gpio_get(pin_);
    ow_delay_us(53);

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

void Ds18b20Sensor::write_address(const Ds18b20Address &address) {
    for (uint8_t i = 0; i < sizeof(address.bytes); ++i) {
        write_byte(address.bytes[i]);
    }
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

WaterTemperatureReading Ds18b20Sensor::read_converted_temperature_c(uint8_t device_index) {
    WaterTemperatureReading reading = {false, false, WaterTemperatureStatus::not_read, 0.0f};

    if (device_index >= device_count_) {
        reading.status = WaterTemperatureStatus::device_missing;
        last_status_ = reading.status;
        return reading;
    }

    if (!reset_bus()) {
        reading.status = last_status_;
        return reading;
    }

    reading.device_present = true;
    write_byte(DS18B20_CMD_MATCH_ROM);
    write_address(devices_[device_index]);
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

WaterTemperatureReading Ds18b20Sensor::read_converted_temperature_c() {
    if (device_count_ == 0) {
        discover_devices();
    }

    return read_converted_temperature_c(0);
}

}
