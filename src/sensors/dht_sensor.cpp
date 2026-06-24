#include "sensors/dht_sensor.h"

#include "config/hardware_config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace hydro {

DhtSensor::DhtSensor(uint pin) : pin_(pin) {}

void DhtSensor::init() {
    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);
}

bool DhtSensor::wait_for_level(bool level, uint32_t timeout_us) {
    uint32_t start = time_us_32();

    while (gpio_get(pin_) != level) {
        if ((uint32_t)(time_us_32() - start) > timeout_us) {
            return false;
        }
    }

    return true;
}

bool DhtSensor::read(DhtReading *reading) {
    if (reading == nullptr) {
        return false;
    }

    uint8_t data[5] = {0, 0, 0, 0, 0};

    gpio_init(pin_);
    gpio_set_dir(pin_, GPIO_OUT);
    gpio_put(pin_, 0);
    sleep_ms(20);

    gpio_put(pin_, 1);
    sleep_us(40);

    gpio_set_dir(pin_, GPIO_IN);
    gpio_pull_up(pin_);

    uint32_t interrupt_state = save_and_disable_interrupts();
    bool ok = true;

    ok = ok && wait_for_level(false, 100);
    ok = ok && wait_for_level(true, 100);
    ok = ok && wait_for_level(false, 100);

    for (uint i = 0; ok && i < 40; ++i) {
        ok = ok && wait_for_level(true, 100);

        uint32_t high_start = time_us_32();
        ok = ok && wait_for_level(false, 120);
        uint32_t high_time = (uint32_t)(time_us_32() - high_start);

        data[i / 8] <<= 1;
        if (high_time > 45) {
            data[i / 8] |= 1;
        }
    }

    restore_interrupts(interrupt_state);

    if (!ok) {
        reading->valid = false;
        return false;
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        reading->valid = false;
        return false;
    }

    if (config::DHT_SENSOR_IS_DHT11) {
        reading->humidity_percent = (float)data[0] + ((float)data[1] / 10.0f);
        reading->temperature_c = (float)data[2] + ((float)data[3] / 10.0f);
    } else {
        uint16_t raw_humidity = ((uint16_t)data[0] << 8) | data[1];
        uint16_t raw_temperature = (((uint16_t)data[2] & 0x7F) << 8) | data[3];

        reading->humidity_percent = raw_humidity / 10.0f;
        reading->temperature_c = raw_temperature / 10.0f;

        if ((data[2] & 0x80) != 0) {
            reading->temperature_c = -reading->temperature_c;
        }
    }

    if (reading->humidity_percent < 0.0f || reading->humidity_percent > 100.0f ||
        reading->temperature_c < -40.0f || reading->temperature_c > 80.0f) {
        reading->valid = false;
        return false;
    }

    reading->valid = true;
    return true;
}

}
