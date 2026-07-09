/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	GPIO_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
 *
 * @author Gonzalo Francisco Benitez Bodalo (gonzalo.benitez@ingenieria.uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_log.h"
#include "gpio_mcu.h"
#include "timer_mcu.h"
#include "u8g2_esp32_hal.h"
#include "driver/gpio.h"             
/*==================[macros and definitions]=================================*/
#define LED_STRIP_GPIO  18        // GPIO TIRA LED
#define LED_COUNT   20      // Cantidad de LEDs de la tira
#define CONFIG_LED_PERIOD 1000000 //Tiempo que dura el estímulo.

#define BUTTON_GPIO 22

//---Definiciones para la pantalla OLED---//

#define GPIO_OLED_SCK    6
#define GPIO_OLED_SDA    7
#define GPIO_OLED_RES    21
#define GPIO_OLED_DC     20
#define GPIO_OLED_CS     10
/*==================[internal data definition]===============================*/
TaskHandle_t reflexArcSimulationTask = NULL;
led_strip_handle_t led_strip;
u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
/**
 * @brief Definición de los estados del sistema
 */
typedef enum {
    MODE_IDLE = 0,
    MODE_FAST,
    MODE_SLOW,
    MODE_TRAIN,
    MODE_MAX_COUNT
}system_mode_t; //Nota de color: la "t" al final de las variables denota "tipo".
system_mode_t current_mode = MODE_IDLE; //Estado actual del sistema.
QueueHandle_t mode_queue; //Usamos un queue para ir informado los cambios de modo.
u8g2_t u8g2; //Estructura pantalla OLED
char buffer[32]; //Buffer para el mensaje mostrado por la pantalla OLED.
/*==================[internal functions declaration]=========================*/
 
//-------------Inicio bloque de funciones auxiliares-------------//

/**
 * @brief Función que asigna los colores a la tira GRB.
 * @param strip Tipo tira led.
 * @param index Índice que indica el led a encender.
 * @param red Intensidad de rojo.
 * @param green Intensidad de verde.
 * @param blue Intensidad de azul.
 */
void set_led_color(led_strip_handle_t strip, int index, int red, int green, int blue) 
{
    led_strip_set_pixel(strip, index, green, red, blue); 
}

/**
 * @brief Función que detecta si hubo un cambio de modo.
 */
bool check_mode_change(){
    return uxQueueMessagesWaiting(mode_queue) > 0;
}

/**
 * @brief Función que simula el impulso nervioso.
 * @param tail_length Cantidad de ledes encendidos al mismo tiempo en la cola.
 * @param speed_ms Velocidad del efecto.
 * @param r Intensidad de rojo.
 * @param g Intensidad de verde.
 * @param b Intensidad de azul.
 */
void generate_nerve_impulse(int tail_length,int speed_ms,int r, int g, int b){
    float fade_factor = 0.0;
    int pixel_id = 0;
    for(int head=0; head < (LED_COUNT + tail_length); head++){
        if(check_mode_change()) return; //En este caso, como la función es void, el "return" saldrá de la misma.
        led_strip_clear(led_strip);
        for(int i=0; i<tail_length; i++){
            pixel_id = head - i;
            if(pixel_id >= 0 && pixel_id < LED_COUNT){
                fade_factor = 1 - ((float)i / tail_length);
                set_led_color(led_strip,pixel_id,(int)(r*fade_factor),(int)(g*fade_factor),(int)(b*fade_factor));
            }           
        }
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(speed_ms));
    }
}

/**
 * @brief Función inicializadora del OLED.
 */
void init_oled_display() {
    // 1. Asignar GPIOes
    u8g2_esp32_hal.clk   = GPIO_OLED_SCK;
    u8g2_esp32_hal.mosi  = GPIO_OLED_SDA;
    u8g2_esp32_hal.cs    = GPIO_OLED_CS;
    u8g2_esp32_hal.dc    = GPIO_OLED_DC;
    u8g2_esp32_hal.reset = GPIO_OLED_RES;
    
    // 2. Inicializar HAL
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    // 3. RESET MANUAL DE HARDWARE (Vital para quitar el ruido inicial)
    // Forzamos el GPIO de Reset a BAJO (Activo) y luego ALTO.
    gpio_set_level(GPIO_OLED_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(GPIO_OLED_RES, 0); // Reset presionado
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(GPIO_OLED_RES, 1); // Reset soltado (Arranca la pantalla)
    vTaskDelay(pdMS_TO_TICKS(100));  // Esperar a que la pantalla despierte

    // 4. Configuración del Driver
    // INTENTO 1: SH1106 (Común en pantallas SPI de 1.3")
    
    u8g2_Setup_sh1106_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        (u8x8_msg_cb)u8g2_esp32_spi_byte_cb,
        (u8x8_msg_cb)u8g2_esp32_gpio_and_delay_cb
    );
    
    /*
    // NOTA: Si ves la imagen corrida o con ruido, comenta la linea de arriba 
    // y descomenta la de abajo para probar el driver SSD1306: 
    u8g2_Setup_ssd1306_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        (u8x8_msg_cb)u8g2_esp32_spi_byte_cb,
        (u8x8_msg_cb)u8g2_esp32_gpio_and_delay_cb
    );
    */

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // Encender display
    
    // 5. Limpiar pantalla inmediatamente
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
}

/**
 * @brief Función inicializadora del botón.
 */
void configure_button(){
    GPIOInit(BUTTON_GPIO, GPIO_INPUT);
    GPIOInputFilter(BUTTON_GPIO);
}

//-------------Fin bloque de funciones auxiliares-------------//

//------------------Inicio bloque de tareas-------------------//

static void taskLedControl(void *pvParameters){
    system_mode_t received_mode;

    while(true){

        //Necesitamos revisar si hay un comando en la cola.
        if(xQueueReceive(mode_queue,&received_mode,0) == pdTRUE){
            current_mode = received_mode;
            led_strip_clear(led_strip);
            led_strip_refresh(led_strip);
        }

        switch (current_mode){

            case MODE_IDLE:
                led_strip_clear(led_strip);
                led_strip_refresh(led_strip);
                vTaskDelay(pdMS_TO_TICKS(100));
            break;

            case MODE_FAST:
                generate_nerve_impulse(4,100,255,0,255);
                vTaskDelay(pdMS_TO_TICKS(300));
            break;

            case MODE_SLOW:
                generate_nerve_impulse(4,300,153,51,102);
                vTaskDelay(pdMS_TO_TICKS(500));
            break;

            case MODE_TRAIN:
                //Two fast pulses
                generate_nerve_impulse(4,50,204,255,255);
                if(!check_mode_change()) generate_nerve_impulse(4,50,204,255,255);
                vTaskDelay(pdMS_TO_TICKS(100));
            break;

            default: break;

        }
    }
}

static void oledManaging(void *pvParameter){

    init_oled_display();

    while(true){
            
        u8g2_ClearBuffer(&u8g2);

        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(&u8g2,5,14,"DidacticBrain");
        u8g2_DrawHLine(&u8g2,0,15,128);

        u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
        u8g2_DrawStr(&u8g2,5,30,"Modo: ");
        
        switch (current_mode) {
            case MODE_IDLE: sprintf(buffer, "REPOSO"); break;
            case MODE_FAST: sprintf(buffer, "MIELINICO"); break;
            case MODE_SLOW: sprintf(buffer, "DOLOR (C)"); break;
            case MODE_TRAIN:sprintf(buffer, "TREN IMP."); break;
            default:        sprintf(buffer, "???"); break;
        }

        int str_width = u8g2_GetStrWidth(&u8g2, buffer);
        u8g2_DrawStr(&u8g2, (128 - str_width)/2,50,buffer);
        u8g2_SendBuffer(&u8g2);

        vTaskDelay(pdMS_TO_TICKS(100));

    }
}

static void manageButton (void *pvParameters){

    configure_button();
    int next_state_prev = 1; // Pull-up
    system_mode_t next_mode_val = MODE_IDLE;

    while(true){
        int next_state = !GPIORead(BUTTON_GPIO);

        if(next_state == 0 && next_state_prev == 1){
            next_mode_val = current_mode + 1;
            if (next_mode_val >= MODE_MAX_COUNT) next_mode_val = MODE_IDLE;
            xQueueSend(mode_queue,&next_mode_val,0);
        }
        next_state_prev = next_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/*==================[external functions definition]==========================*/
void app_main(void){

    mode_queue = xQueueCreate(10,sizeof(system_mode_t));

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_COUNT,
    };

    led_strip_rmt_config_t chip_config = {
        .resolution_hz = 10000000, //Diez millones
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_config, &chip_config, &led_strip);

    xTaskCreate(&taskLedControl,"LedControl",2048,NULL,5,NULL);
    xTaskCreate(&oledManaging,"OledControl",2048,NULL,5,NULL);
    xTaskCreate(&manageButton,"ButtonControl",2048,NULL,5,NULL);
}