/*-------------------------------------
 Proyecto:		Control de luces
 Modulo:		con_luces.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Control del comportamiento de las luces
 Descripcion:	Centraloiza el procesamiento de las actividades según modos de operación
 				
 --------------------------------------
*/

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "con_luces.h"
#include "funciones.h"

static const char *TAG = "LUCES_HW";
static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    // 1. Recuperamos el número del pin desde el argumento
    uint32_t gpio_num = (uint32_t)(intptr_t)arg; 
    
    // 2. Enviamos el número a la cola (ahora la variable sí existe)
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void luces_task(void* arg) {
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Antirrebote simple
            
            for (int i = 0; i < TOTAL_LUCES; i++) {
                if (luces[i].switch_pin == io_num) {
                    luces[i].estado = !luces[i].estado;
                    gpio_set_level(luces[i].triac_pin, luces[i].estado);
                    
                    ESP_LOGI(TAG, "Luz %d (Pin %d) -> %s", i+1, luces[i].triac_pin, luces[i].estado ? "ON" : "OFF");
                    
                    // Publicar estado
                    char t_estado[100];
                    snprintf(t_estado, sizeof(t_estado), "%sestado", luces[i].topic_base);
                    func_publicar_mensaje(t_estado, luces[i].estado ? "1" : "0");
                }
            }
        }
    }
}

void con_luces_init(void) {
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(luces_task, "luces_task", 3072, NULL, 10, NULL);
    gpio_install_isr_service(0);

    for (int i = 0; i < TOTAL_LUCES; i++) {
        // Configurar Salida (Triac)
        gpio_reset_pin(luces[i].triac_pin);
        gpio_set_direction(luces[i].triac_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(luces[i].triac_pin, 0);

        // Configurar Entrada (Interruptor)
        gpio_reset_pin(luces[i].switch_pin);
        gpio_set_direction(luces[i].switch_pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(luces[i].switch_pin, GPIO_PULLUP_ONLY); // Asumiendo que el switch cierra a GND
        gpio_set_intr_type(luces[i].switch_pin, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(luces[i].switch_pin, gpio_isr_handler, (void *)(int)luces[i].switch_pin);
    }
    ESP_LOGI(TAG, "Hardware listo. Total luces: %d", TOTAL_LUCES);
}
