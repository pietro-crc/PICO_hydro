#include "drivers/lcd1602_i2c.h"

#include "config/hardware_config.h"
#include "pico/stdlib.h"

namespace hydro {

Lcd1602I2c::Lcd1602I2c(i2c_inst_t *i2c) : i2c_(i2c) {}

bool Lcd1602I2c::available() const {
    return available_;
}

uint8_t Lcd1602I2c::address() const {
    return address_;
}

bool Lcd1602I2c::write(uint8_t value) {
    if (address_ == 0) {
        return false;
    }

    int result = i2c_write_blocking(i2c_, address_, &value, 1, false);
    return result == 1;
}

void Lcd1602I2c::pulse_enable(uint8_t value) {
    write(value | config::LCD_ENABLE);
    sleep_us(1);
    write(value & ~config::LCD_ENABLE);
    sleep_us(50);
}

void Lcd1602I2c::write4(uint8_t nibble, uint8_t mode) {
    uint8_t value = (nibble & 0xF0) | config::LCD_BACKLIGHT_ON | mode;
    write(value);
    pulse_enable(value);
}

void Lcd1602I2c::send(uint8_t value, uint8_t mode) {
    write4(value & 0xF0, mode);
    write4((value << 4) & 0xF0, mode);
}

void Lcd1602I2c::command(uint8_t command_value) {
    send(command_value, 0);
    if (command_value == 0x01 || command_value == 0x02) {
        sleep_ms(2);
    }
}

void Lcd1602I2c::write_char(char c) {
    send((uint8_t)c, config::LCD_RS);
}

void Lcd1602I2c::set_cursor(uint8_t col, uint8_t row) {
    static const uint8_t row_offsets[] = {0x00, 0x40};
    command(0x80 | (col + row_offsets[row > 0 ? 1 : 0]));
}

void Lcd1602I2c::print_padded(uint8_t row, const char *text) {
    set_cursor(0, row);

    uint8_t i = 0;
    for (; i < 16 && text[i] != '\0'; ++i) {
        write_char(text[i]);
    }

    for (; i < 16; ++i) {
        write_char(' ');
    }
}

void Lcd1602I2c::show_lines(const char *line1, const char *line2) {
    if (!available_) {
        return;
    }

    print_padded(0, line1);
    print_padded(1, line2);
}

void Lcd1602I2c::diagnostic_test() {
    if (!available_) {
        return;
    }

    for (uint i = 0; i < 3; ++i) {
        write(config::LCD_BACKLIGHT_OFF);
        sleep_ms(300);
        write(config::LCD_BACKLIGHT_ON);
        sleep_ms(300);
    }

    show_lines("LCD TEST 123456", "8888888888888888");
    sleep_ms(8000);

    show_lines("ABCDEFGHIJKLMNOP", "0123456789ABCDEF");
    sleep_ms(5000);

    command(0x01);
    sleep_ms(2);
}

uint8_t Lcd1602I2c::find_address() {
    const uint8_t candidates[] = {0x27, 0x3F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E};

    for (uint i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        uint8_t value = 0;
        int result = i2c_write_blocking(i2c_, candidates[i], &value, 1, false);
        if (result == 1) {
            return candidates[i];
        }
    }

    return 0;
}

bool Lcd1602I2c::init() {
    address_ = find_address();
    if (address_ == 0) {
        available_ = false;
        return false;
    }

    sleep_ms(50);

    write4(0x30, 0);
    sleep_ms(5);
    write4(0x30, 0);
    sleep_us(150);
    write4(0x30, 0);
    sleep_us(150);
    write4(0x20, 0);
    sleep_us(150);

    command(0x28);
    command(0x0C);
    command(0x06);
    command(0x01);

    available_ = true;
    return true;
}

}
