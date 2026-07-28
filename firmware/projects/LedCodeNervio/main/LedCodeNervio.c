/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * El programa controla la lógica de un par de tiras LED las cuales simulan impulsos nerviosos.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection (keep this updated)
 * 
 * |    OLED SPI    |     ESP32   	|
 * |:--------------:|:--------------|
 * |      GND	    | 	  GND	    |
 * |      VCC   	| 	  3.3 V	    |
 * |      SCK    	| 	  GPIO 6    |
 * |      SDA   	| 	  GPIO 7    |
 * |      RES   	| 	  GPIO 21   |
 * |      DC	    | 	  GPIO 20   |
 * |      CS	    | 	  GPIO 10   |
 * 
 * |    LED Strip 1 |    ESP32   	|
 * |:--------------:|:--------------|
 * |      GND	    | 	  GND	    |
 * |      VCC	    | 	  12 V	    |
 * |      DI/DO	    | 	  GPIO 18   |
 * |      BI/BO	    |  	  GND       |
 * 
 * |    Button      |    ESP32   	|
 * |:--------------:|:--------------|
 * |     PIN 1	    | 	  GND	    |
 * |     PIN 2	    | 	  GPIO 22	|
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
#define STRIP_A_GPIO      18      // GPIO TIRA LED N° 1
#define STRIP_B_GPIO      19      // GPIO TIRA LED N° 2
#define LED_COUNT_A      10      // Cantidad de LEDs de la tira N°1
#define LED_COUNT_B      10      // Cantidad de LEDs de la tira N°2

#define FRAME_PERIOD_MS  10       // Tick del motor de animación de las tiras. Todas las vías se actualizan a este ritmo.

#define BUTTON_GPIO 22

//---Definiciones para la pantalla OLED---//

#define GPIO_OLED_SCK    6
#define GPIO_OLED_SDA    7
#define GPIO_OLED_RES    21
#define GPIO_OLED_DC     20
#define GPIO_OLED_CS     10

/**
 * @brief Identificador de cada vía nerviosa simulada.
 * Las 3 primeras son sensitivas (ascendentes: médula -> cerebro).
 * Las 2 últimas son motoras (descendentes: cerebro -> médula).
 */
typedef enum {
    PATHWAY_SPINOTHALAMIC_L = 0,   // Espinotalámica, tren inferior izquierdo
    PATHWAY_SPINOTHALAMIC_R,       // Espinotalámica, tren inferior derecho
    PATHWAY_DCML,                  // Cordón posterior - lemnisco medial
    PATHWAY_CORTICOSPINAL_LATERAL, // Motora, corticoespinal lateral
    PATHWAY_CORTICOSPINAL_ANTERIOR,// Motora, corticoespinal anterior
    PATHWAY_COUNT
} pathway_id_t;

/*==================[internal data definition]===============================*/
led_strip_handle_t led_strip_a;
led_strip_handle_t led_strip_b;

u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
u8g2_t u8g2; //Estructura pantalla OLED

char buffer[32]; //Buffer para el mensaje mostrado por la pantalla OLED.

QueueHandle_t mode_queue; //Usamos un queue para ir informado los cambios de modo.

/**
 * @brief Configuración de los parámetros de una vía nerviosa. La dirección de la misma se define con los valores de start_led y end_led.
 * @param tail_length Es el largo de cola de LEDs encendidos. Es un parámetro estético, no funcional.
 */
typedef struct {
    led_strip_handle_t *strip;
    int start_led;
    int end_led;
    int tail_length;
    int speed_ms;
    uint8_t r,g,b;
    bool active;
    int head;
    int elapsed_ms;
}pathway_t;

pathway_t pathways[PATHWAY_COUNT];

/*==================[internal functions declaration]=========================*/
 
//-------------Inicio bloque de funciones auxiliares-------------//

/**
 * @brief La tira es GRB. Esta función asigna los colores en RGB, que es más común.
 */
void set_led_color(led_strip_handle_t strip, int index, int red, int green, int blue) 
{
    led_strip_set_pixel(strip, index, green, red, blue); 
}

/**
 * @brief Configura los parámetros fijos de una vía (no la dispara).
 * @param id Identificador de la vía (pathway_id_t).
 * @param strip Puntero a la tira física donde vive el tramo de esta vía.
 * @param start_led Primer LED del tramo (extremo de inicio de la animación).
 * @param end_led Último LED del tramo (extremo de llegada). El orden entre
 *                start_led y end_led define la dirección del impulso (si es 
 *                motor o sensitivo).
 */
void pathway_configure(pathway_id_t id, led_strip_handle_t *strip,
                        int start_led, int end_led, int tail_length, int speed_ms,
                        uint8_t r, uint8_t g, uint8_t b)
{
    pathways[id].strip       = strip;
    pathways[id].start_led   = start_led;
    pathways[id].end_led     = end_led;
    pathways[id].tail_length = tail_length;
    pathways[id].speed_ms    = speed_ms;
    pathways[id].r = r; pathways[id].g = g; pathways[id].b = b;
    pathways[id].active      = false;
}

/**
 * @brief Dispara el impulso de una via.
 */
void pathway_trigger(pathway_id_t id)
{
    pathways[id].head        = 0;
    pathways[id].elapsed_ms  = 0;
    pathways[id].active      = true;   
}

/**
 * @brief Escribe en el framebuffer de la tira los píxeles correspondientes
 * al estado actual de una vía (un solo frame). No limpia ni refresca la tira:
 * eso lo hace impulseEngineTask una vez por frame para todas las vías juntas.
 */
static void render_pathway(pathway_t *p)
{
    int seg_len = abs(p->end_led - p->start_led) + 1;
    int dir = (p->end_led >= p->start_led) ? 1 : -1;
 
    for (int i = 0; i < p->tail_length; i++) {
        int pos = p->head - i;
        if (pos >= 0 && pos < seg_len) {
            int led_index = p->start_led + dir * pos;
            float fade = 1.0f - ((float)i / p->tail_length);
            set_led_color(*(p->strip), led_index,
                          (int)(p->r * fade), (int)(p->g * fade), (int)(p->b * fade));
        }
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
    
    u8g2_Setup_sh1106_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        (u8x8_msg_cb)u8g2_esp32_spi_byte_cb,
        (u8x8_msg_cb)u8g2_esp32_gpio_and_delay_cb
    );


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


static void impulseEngineTask(void *pvParameters){

    while (true){

        led_strip_clear(led_strip_a);
        led_strip_clear(led_strip_b);

        for (int i = 0; i<PATHWAY_COUNT; i++){
            pathway_t *p = &pathways[i];

            if(!p->active) continue;

            render_pathway(p);

            p->elapsed_ms += FRAME_PERIOD_MS;
            if(p->elapsed_ms >= p->speed_ms){
                p->elapsed_ms = 0;
                p->head++;
                int seg_len = abs(p->end_led - p->start_led) + 1;
                if(p->head >= seg_len + p->tail_length){
                    p->active = false; 
                }
            }
        }

        led_strip_refresh(led_strip_a);
        led_strip_refresh(led_strip_b);

        vTaskDelay(pdMS_TO_TICKS(FRAME_PERIOD_MS));

    }
}


static void oledManaging(void *pvParameter){
 
    init_oled_display();
 
    static const char *pathway_names[PATHWAY_COUNT] = {
        "ESPTAL. IZQ.",
        "ESPTAL. DER.",
        "CP-LM",
        "CORT.ESP. LAT.",
        "CORT.ESP. ANT."
    };
 
    while(true){
 
        u8g2_ClearBuffer(&u8g2);
 
        u8g2_SetFont(&u8g2, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(&u8g2,5,14,"DidacticBrain");
        u8g2_DrawHLine(&u8g2,0,15,128);
 
        u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
        u8g2_DrawStr(&u8g2,5,30,"Via activa: ");
 
        // Muestra la última vía disparada que sigue activa.
        sprintf(buffer, "---");
        for (int i = 0; i < PATHWAY_COUNT; i++) {
            if (pathways[i].active) {
                sprintf(buffer, "%s", pathway_names[i]);
                break;
            }
        }
 
        int str_width = u8g2_GetStrWidth(&u8g2, buffer);
        u8g2_DrawStr(&u8g2, (128 - str_width)/2,50,buffer);
        u8g2_SendBuffer(&u8g2);
 
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea de prueba: cada pulsación cicla y dispara una vía distinta.
 * Esto es solo un demo para validar el motor de animación; la lógica real
 * de qué vía disparar (según estímulo/otro periférico) se define después.
 */
static void manageButton (void *pvParameters){
 
    configure_button();
    int next_state_prev = 1; // Pull-up
    pathway_id_t next_pathway = PATHWAY_SPINOTHALAMIC_L;
 
    while(true){
        int next_state = !GPIORead(BUTTON_GPIO);
 
        if(next_state == 0 && next_state_prev == 1){
            pathway_trigger(next_pathway);
            next_pathway = (next_pathway + 1) % PATHWAY_COUNT;
        }
        next_state_prev = next_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/*==================[external functions definition]==========================*/
void app_main(void){

    led_strip_config_t strip_a_config = {
        .strip_gpio_num = STRIP_A_GPIO,
        .max_leds = LED_COUNT_A,
    };
    led_strip_rmt_config_t chip_a_config = {
        .resolution_hz = 10000000, //Diez millones
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_a_config, &chip_a_config, &led_strip_a);
 
    led_strip_config_t strip_b_config = {
        .strip_gpio_num = STRIP_B_GPIO,
        .max_leds = LED_COUNT_B,
    };
    led_strip_rmt_config_t chip_b_config = {
        .resolution_hz = 10000000,
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_b_config, &chip_b_config, &led_strip_b);

    // --------------------------------------------------------------------
    // AJUSTAR: estos rangos de LEDs son PLACEHOLDERS. Reemplazar por los
    // índices reales de cada tramo una vez que tengamos exactamente por qué
    // huecos de qué secciones queda enhebrada cada vía en cada tira física.
    // El orden (start_led -> end_led) define la dirección de la animación:
    // ascendente para las sensitivas (médula -> cerebro), descendente para
    // las motoras (cerebro -> médula).
    // --------------------------------------------------------------------
 
    // Vías sensitivas (ascendentes)
    pathway_configure(PATHWAY_SPINOTHALAMIC_L, &led_strip_a, 0, 5, 2, 100, 255, 0, 255);
    pathway_configure(PATHWAY_SPINOTHALAMIC_R, &led_strip_a, 4, 8, 2, 100, 255, 0, 255);
    pathway_configure(PATHWAY_DCML,            &led_strip_b, 0, 9,  3, 150, 153, 51, 102);
 
    // Vías motoras (descendentes)
    pathway_configure(PATHWAY_CORTICOSPINAL_LATERAL,  &led_strip_a, 9, 5, 2, 100, 204, 255, 255);
    pathway_configure(PATHWAY_CORTICOSPINAL_ANTERIOR, &led_strip_b, 7, 3, 2, 100, 204, 255, 255);
 
    xTaskCreate(&impulseEngineTask,"ImpulseEngine",2048,NULL,5,NULL);
    xTaskCreate(&oledManaging,"OledControl",2048,NULL,5,NULL);
    xTaskCreate(&manageButton,"ButtonControl",2048,NULL,5,NULL);
}