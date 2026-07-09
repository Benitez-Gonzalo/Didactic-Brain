#ifndef U8G2_ESP32_HAL_H
#define U8G2_ESP32_HAL_H

#include "u8g2.h"

#define U8G2_ESP32_HAL_UNDEFINED (-1)

typedef struct {
    gpio_num_t clk;     // Clock (SCK / D0)
    gpio_num_t mosi;    // MOSI (SDA / D1)
    gpio_num_t cs;      // Chip Select (CS)
    gpio_num_t dc;      // Data/Command (DC / A0)
    gpio_num_t reset;   // Reset (RES)
    spi_host_device_t spi_host; // Ejemplo: SPI2_HOST
} u8g2_esp32_hal_t ;

#define U8G2_ESP32_HAL_DEFAULT {U8G2_ESP32_HAL_UNDEFINED, U8G2_ESP32_HAL_UNDEFINED, U8G2_ESP32_HAL_UNDEFINED, U8G2_ESP32_HAL_UNDEFINED, U8G2_ESP32_HAL_UNDEFINED, SPI2_HOST}

void u8g2_esp32_hal_init(u8g2_esp32_hal_t u8g2_esp32_hal_param);
uint8_t u8g2_esp32_spi_byte_cb(u8g2_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8g2_esp32_gpio_and_delay_cb(u8g2_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif /* U8G2_ESP32_HAL_H */