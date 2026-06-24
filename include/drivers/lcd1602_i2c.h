#pragma once

#include "hardware/i2c.h"

#include <cstdint>

namespace hydro {

class Lcd1602I2c {
public:
    explicit Lcd1602I2c(i2c_inst_t *i2c);

    bool init();
    bool available() const;
    uint8_t address() const;
    void show_lines(const char *line1, const char *line2);
    void diagnostic_test();

private:
    bool write(uint8_t value);
    void pulse_enable(uint8_t value);
    void write4(uint8_t nibble, uint8_t mode);
    void send(uint8_t value, uint8_t mode);
    void command(uint8_t command);
    void write_char(char c);
    void set_cursor(uint8_t col, uint8_t row);
    void print_padded(uint8_t row, const char *text);
    uint8_t find_address();

    i2c_inst_t *i2c_;
    uint8_t address_ = 0;
    bool available_ = false;
};

}
