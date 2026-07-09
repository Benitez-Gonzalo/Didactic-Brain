#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_rom_sys.h"
#include "u8g2_esp32_hal.h"

static const char *TAG = "u8g2_hal_spi";

static u8g2_esp32_hal_t u8g2_esp32_hal; // Configuración guardada
static spi_device_handle_t handle_spi;  // Handle del dispositivo SPI

// Inicializa el SPI del ESP32
void u8g2_esp32_hal_init(u8g2_esp32_hal_t u8g2_esp32_hal_param) {
    u8g2_esp32_hal = u8g2_esp32_hal_param;

    esp_err_t ret;

    // 1. Configuración del BUS SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = -1, // No usamos MISO (solo escritura hacia la pantalla)
        .mosi_io_num = u8g2_esp32_hal.mosi,
        .sclk_io_num = u8g2_esp32_hal.clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 0
    };

    // 2. Configuración del Dispositivo (La pantalla)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 4 * 1000 * 1000,  // 4 MHz (Velocidad segura)
        .mode = 0,                          // SPI Modo 0 (CPOL=0, CPHA=0)
        .spics_io_num = u8g2_esp32_hal.cs,  // Pin CS controlado por el driver
        .queue_size = 7,
    };

    // Inicializar el bus SPI
    ret = spi_bus_initialize(u8g2_esp32_hal.spi_host, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    // Adjuntar la pantalla al bus
    ret = spi_bus_add_device(u8g2_esp32_hal.spi_host, &devcfg, &handle_spi);
    ESP_ERROR_CHECK(ret);

    // 3. Configurar GPIOs adicionales (DC y RESET)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = ((1ULL << u8g2_esp32_hal.dc) | (1ULL << u8g2_esp32_hal.reset));
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE; // Opcional, ayuda a la estabilidad
    gpio_config(&io_conf);
}

// Callback para controlar GPIOs (DC, Reset) y Delays
uint8_t u8g2_esp32_gpio_and_delay_cb(u8g2_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT: // CAMBIADO U8G2_ POR U8X8_
            // La inicialización ya se hizo en u8g2_esp32_hal_init
            break;
        case U8X8_MSG_DELAY_MILLI:         // CAMBIADO U8G2_ POR U8X8_
            vTaskDelay(arg_int / portTICK_PERIOD_MS);
            break;
        case U8X8_MSG_DELAY_10MICRO:       // CAMBIADO U8G2_ POR U8X8_
            esp_rom_delay_us(10);          // CAMBIADO ets_delay_us POR esp_rom_delay_us
            break;
        case U8X8_MSG_DELAY_100NANO:       // CAMBIADO U8G2_ POR U8X8_
            esp_rom_delay_us(1);           // CAMBIADO ets_delay_us POR esp_rom_delay_us
            break;
        case U8X8_MSG_GPIO_DC:             // CAMBIADO U8G2_ POR U8X8_
            // Seleccionar Data (1) o Comando (0)
            gpio_set_level(u8g2_esp32_hal.dc, arg_int);
            break;
        case U8X8_MSG_GPIO_RESET:          // CAMBIADO U8G2_ POR U8X8_
            // Controlar el pin de Reset
            gpio_set_level(u8g2_esp32_hal.reset, arg_int);
            break;
        case U8X8_MSG_GPIO_CS:             // CAMBIADO U8G2_ POR U8X8_
            // El driver spi_master maneja el CS automáticamente en cada transmisión
            // pero si U8g2 lo pide explícitamente, lo dejamos aquí por compatibilidad.
            break;
        default:
            return 0; // Mensaje no manejado
    }
    return 1;
}

/*
// Callback para controlar GPIOs y Delays
uint8_t u8g2_esp32_gpio_and_delay_cb(u8g2_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            // Inicializar TODOS los pines como salida para Software SPI
            gpio_set_direction(u8g2_esp32_hal.clk, GPIO_MODE_OUTPUT);
            gpio_set_direction(u8g2_esp32_hal.mosi, GPIO_MODE_OUTPUT);
            gpio_set_direction(u8g2_esp32_hal.cs, GPIO_MODE_OUTPUT);
            gpio_set_direction(u8g2_esp32_hal.dc, GPIO_MODE_OUTPUT);
            gpio_set_direction(u8g2_esp32_hal.reset, GPIO_MODE_OUTPUT);
            
            // Poner CS en alto (deseleccionado) por defecto
            gpio_set_level(u8g2_esp32_hal.cs, 1);
            break;

        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(arg_int / portTICK_PERIOD_MS);
            break;

        case U8X8_MSG_DELAY_10MICRO:
            esp_rom_delay_us(10);
            break;

        case U8X8_MSG_DELAY_100NANO:
            esp_rom_delay_us(1);
            break;

        case U8X8_MSG_GPIO_DC:
            gpio_set_level(u8g2_esp32_hal.dc, arg_int);
            break;

        case U8X8_MSG_GPIO_RESET:
            gpio_set_level(u8g2_esp32_hal.reset, arg_int);
            break;

        case U8X8_MSG_GPIO_CS:
            gpio_set_level(u8g2_esp32_hal.cs, arg_int);
            break;

        // --- CORRECCIÓN DE NOMBRES ---
        case U8X8_MSG_GPIO_SPI_CLOCK:  // Nombre correcto para el Clock
            gpio_set_level(u8g2_esp32_hal.clk, arg_int);
            break;

        case U8X8_MSG_GPIO_SPI_DATA:   // Nombre correcto para MOSI/Data
            gpio_set_level(u8g2_esp32_hal.mosi, arg_int);
            break;
        // -----------------------------

        default:
            return 0;
    }
    return 1;
}
*/
// Callback para enviar datos por SPI
uint8_t u8g2_esp32_spi_byte_cb(u8g2_t *u8g2, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_BYTE_SEND: {         // CAMBIADO U8G2_ POR U8X8_
            uint8_t *data = (uint8_t *)arg_ptr;
            spi_transaction_t t;
            memset(&t, 0, sizeof(t));
            
            t.length = 8 * arg_int; // Longitud en bits
            t.tx_buffer = data;     // Puntero a los datos
            
            // Usamos polling porque las transacciones de U8g2 suelen ser cortas y rápidas
            spi_device_polling_transmit(handle_spi, &t);
            break;
        }
        case U8X8_MSG_BYTE_INIT:           // CAMBIADO U8G2_ POR U8X8_
            break;
        case U8X8_MSG_BYTE_SET_DC:         // CAMBIADO U8G2_ POR U8X8_
            // Ya manejado en GPIO_DC
            gpio_set_level(u8g2_esp32_hal.dc, arg_int);
            break;
        case U8X8_MSG_BYTE_START_TRANSFER: // CAMBIADO U8G2_ POR U8X8_
            break;
        default:
            return 0;
    }
    return 1;
}