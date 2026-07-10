#pragma once

#include "pico/stdlib.h"

#include <cstdint>

#if __has_include("config/secrets.h")
#include "config/secrets.h"
#define HYDRO_HAS_SECRETS 1
#else
#define HYDRO_HAS_SECRETS 0
#endif

namespace hydro::config {

constexpr const char *FIRMWARE_VERSION = "PICO Hydro 1.0";

constexpr uint LED_PIN = PICO_DEFAULT_LED_PIN;

constexpr uint LCD_SDA_PIN = 0;
constexpr uint LCD_SCL_PIN = 1;
constexpr uint LEVEL_SENSOR_PIN = 2;
constexpr uint STRIP_PIN = 3;
constexpr uint FLOW_SENSOR_PIN = 4;
constexpr uint DHT_PIN = 5;
constexpr uint VEML_SDA_PIN = 6;
constexpr uint VEML_SCL_PIN = 7;
constexpr uint WATER_TEMPERATURE_PIN = 8;
constexpr uint ESP8266_UART_TX_PIN = 12;
constexpr uint ESP8266_UART_RX_PIN = 13;
constexpr uint RELAY_PIN = 15;
constexpr uint TDS_ADC_PIN = 26;
constexpr uint TDS_ADC_INPUT = 0;

constexpr bool DHT_SENSOR_IS_DHT11 = true;
constexpr uint STRIP_LED_COUNT = 8;
constexpr bool LEVEL_WATER_PRESENT_WHEN_HIGH = true;
constexpr bool RELAY_ACTIVE_LOW = true;
constexpr bool ENABLE_RELAY_TEST_LOOP = false;

constexpr uint32_t SAMPLE_TIME_MS = 1000;
constexpr uint32_t DHT_READ_INTERVAL_MS = 5000;
constexpr uint32_t VEML_READ_INTERVAL_MS = 2000;
constexpr uint32_t TDS_READ_INTERVAL_MS = 2000;
constexpr uint32_t WATER_TEMPERATURE_READ_INTERVAL_MS = 5000;
constexpr uint32_t WATER_TEMPERATURE_FIRST_READ_DELAY_MS = 1000;
constexpr uint32_t WATER_TEMPERATURE_RETRY_DELAY_MS = 30000;
constexpr uint32_t LCD_CAROUSEL_INTERVAL_MS = 3000;
constexpr uint32_t LCD_RECOVERY_INTERVAL_MS = 30000;
constexpr uint32_t SERIAL_SAMPLE_LOG_INTERVAL_MS = 60000;
constexpr uint32_t HEALTH_LOG_INTERVAL_MS = 300000;
constexpr uint32_t DHT_READING_STALE_MS = 30000;
constexpr uint32_t VEML_READING_STALE_MS = 30000;
constexpr uint32_t WATER_TEMPERATURE_STALE_MS = 300000;
constexpr uint32_t GOOGLE_SHEETS_LOG_INTERVAL_MS = 60000;
constexpr uint32_t MIN_PULSE_DISTANCE_US = 1000;
constexpr uint32_t STARTUP_DIAGNOSTIC_SCREEN_MS = 650;
constexpr uint I2C_OPERATION_TIMEOUT_US = 5000;
constexpr uint32_t ESP8266_UART_BAUDRATE = 115200;

constexpr bool ENABLE_STARTUP_DELAY = false;
constexpr bool ENABLE_ONBOARD_LED_STARTUP = true;
constexpr bool ENABLE_STRIP_STARTUP_TEST = false;
constexpr bool ENABLE_LCD_DIAGNOSTIC = false;
constexpr bool ENABLE_STARTUP_DIAGNOSTICS = true;
constexpr bool ENABLE_SERIAL_LOG = true;
constexpr bool ENABLE_WATER_TEMPERATURE_SENSOR = true;
constexpr uint8_t MAX_WATER_TEMPERATURE_SENSORS = 4;
constexpr bool ENABLE_ESP8266_WIFI = true;
#if HYDRO_HAS_SECRETS
constexpr bool ENABLE_GOOGLE_SHEETS_LOGGING = hydro::secrets::ENABLE_GOOGLE_SHEETS_LOGGING;
constexpr const char *GOOGLE_SHEETS_WIFI_SSID = hydro::secrets::WIFI_SSID;
#else
constexpr bool ENABLE_GOOGLE_SHEETS_LOGGING = false;
constexpr const char *GOOGLE_SHEETS_WIFI_SSID = "";
#endif
constexpr float PULSES_PER_LITER = 450.0f;
constexpr float ADC_MAX_VALUE = 4095.0f;
constexpr float PICO_ADC_REFERENCE_VOLTAGE = 3.3f;
constexpr float TDS_TEMPERATURE_COMPENSATION_C = 25.0f;
constexpr float TDS_COMPENSATION_MIN_TEMPERATURE_C = 0.0f;
constexpr float TDS_COMPENSATION_MAX_TEMPERATURE_C = 50.0f;
constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

constexpr uint8_t VEML7700_ADDRESS = 0x10;
constexpr uint8_t VEML7700_REG_ALS_CONF = 0x00;
constexpr uint8_t VEML7700_REG_ALS_DATA = 0x04;
constexpr float VEML7700_LUX_PER_COUNT_GAIN_1_IT_100MS = 0.0576f;

constexpr uint8_t FLOW_GREEN_BRIGHTNESS = 40;
constexpr uint8_t FLOW_IDLE_BLUE_BRIGHTNESS = 25;
constexpr uint8_t LEVEL_WATER_GREEN_BRIGHTNESS = 40;
constexpr uint8_t LEVEL_LOW_RED_BRIGHTNESS = 40;

constexpr bool LCD_BACKLIGHT_ACTIVE_LOW = false;
constexpr uint8_t LCD_BACKLIGHT_BIT = 0x08;
constexpr uint8_t LCD_BACKLIGHT_ON = LCD_BACKLIGHT_ACTIVE_LOW ? 0x00 : LCD_BACKLIGHT_BIT;
constexpr uint8_t LCD_BACKLIGHT_OFF = LCD_BACKLIGHT_ACTIVE_LOW ? LCD_BACKLIGHT_BIT : 0x00;
constexpr uint8_t LCD_ENABLE = 0x04;
constexpr uint8_t LCD_RS = 0x01;

static_assert(
    ESP8266_UART_TX_PIN == 0 || ESP8266_UART_TX_PIN == 12 || ESP8266_UART_TX_PIN == 16,
    "ESP8266_UART_TX_PIN must be a valid UART0 TX pin"
);

static_assert(
    ESP8266_UART_RX_PIN == 1 || ESP8266_UART_RX_PIN == 13 || ESP8266_UART_RX_PIN == 17,
    "ESP8266_UART_RX_PIN must be a valid UART0 RX pin"
);

}
