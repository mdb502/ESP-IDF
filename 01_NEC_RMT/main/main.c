#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "con_wifi.h"
#include "con_mqtt.h"
#include "esp_log.h"
#include "ir_tx.h"
#include "ir_rx.h"
#include "funciones.h"
#include "config.h"

// Prototipos de funciones (para que el compilador sepa que existen abajo)
void tarea_modo_aprendizaje(void *pvParameters);
void tarea_modo_remoto(void *pvParameters);
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo);

static const char *TAG = "MAIN_APP";

// Estructura para pasar handles a las tareas
typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_channel_handle_t rx_chan;
    rmt_encoder_handle_t encoder;
    QueueHandle_t rx_queue;
} ir_handles_t;

static TaskHandle_t xTareaIR = NULL;
static ir_handles_t ir_handles;

// Función para cambiar de modo de forma segura
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo) {
    // IMPORTANTE: vTaskDelete(NULL) borraría la tarea actual (main_task). 
    // Siempre verificamos que xTareaIR sea un handle válido.
    if (xTareaIR != NULL) {
        ESP_LOGI(TAG, "Eliminando tarea de modo anterior...");
        vTaskDelete(xTareaIR);
        xTareaIR = NULL;
    }

    if (nuevo_modo == MODO_REMOTO_CONTROL) {
        xTaskCreate(tarea_modo_remoto, "task_tx", 4096, &ir_handles, 5, &xTareaIR);
        ESP_LOGI("MAIN", "Cambiado a MODO CONTROL REMOTO");
    } else {
        xTaskCreate(tarea_modo_aprendizaje, "task_rx", 4096, &ir_handles, 5, &xTareaIR);
        ESP_LOGI("MAIN", "Cambiado a MODO APRENDIZAJE");
    }
}


// --- TAREA MODO 1: CONTROL REMOTO (Transmisión) ---
void tarea_modo_remoto(void *pvParameters) {
    ir_handles_t *handles = (ir_handles_t *)pvParameters;
    ESP_LOGI(TAG, "Iniciado: MODO CONTROL REMOTO (TX)");

    while (1) {
        // En este paso, simulamos la recepción de un comando MQTT enviando cada 5s
        const ir_nec_scan_code_t scan_code = {
            .address = 0x7C87,
            .command = 0x7F80,
        };
        rmt_transmit_config_t transmit_cfg = {.loop_count = 0};
        
        ESP_LOGI(TAG, "Modo Remoto: Enviando comando IR...");
        ESP_ERROR_CHECK(rmt_transmit(handles->tx_chan, handles->encoder, &scan_code, sizeof(scan_code), &transmit_cfg));
        
        vTaskDelay(pdMS_TO_TICKS(5000)); // Espera 5 segundos entre pruebas
    }
}

// --- TAREA MODO 2: APRENDIZAJE (Recepción) ---
void tarea_modo_aprendizaje(void *pvParameters) {
    ir_handles_t *handles = (ir_handles_t *)pvParameters;
    rmt_symbol_word_t raw_symbols[64];
    rmt_rx_done_event_data_t rx_data;
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    ESP_LOGI(TAG, "Iniciado: MODO APRENDIZAJE (RX)");
    
    // Iniciar primera escucha
    ESP_ERROR_CHECK(rmt_receive(handles->rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg));

    while (1) {
        if (xQueueReceive(handles->rx_queue, &rx_data, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "Señal captada. Procesando...");

            // 1. Extraer los datos numéricos de los símbolos recibidos
            uint16_t addr = 0;
            uint16_t cmd = 0;
            
            // Intentamos parsear el frame (34 símbolos para NEC estándar)
            if (rx_data.num_symbols == 34 && funciones_nec_parse_frame(rx_data.received_symbols, &addr, &cmd)) {
                
                // 2. Preparar el mensaje JSON para la base de datos centralizada
                char payload[128];
                snprintf(payload, sizeof(payload), 
                         "{\"dispositivo\":\"ESP32_MASTER\", \"protocolo\":\"NEC\", \"addr\":\"0x%04X\", \"cmd\":\"0x%04X\"}", 
                         addr, cmd);

                // 3. Publicar por MQTT
                mqtt_enviar_raw(TOPIC_LEARN, payload);
                
                ESP_LOGI(TAG, "Aprendizaje exitoso enviado a MQTT: %s", payload);
            } else {
                ESP_LOGW(TAG, "Señal recibida no es un frame NEC válido o es un Repeat Code.");
            }
            
            // 4. Reiniciar escucha para el próximo comando
            ESP_ERROR_CHECK(rmt_receive(handles->rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg));
        }
    }
}



void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (wifi_init_sta() == ESP_OK) {
        // A. Hardware IR
        ir_tx_init(&ir_handles.tx_chan, &ir_handles.encoder);
        ir_rx_init(&ir_handles.rx_chan, &ir_handles.rx_queue);
        rmt_enable(ir_handles.tx_chan);
        rmt_enable(ir_handles.rx_chan);

        // B. Iniciar en modo por defecto según config.h
        // Esto crea la tarea inicial antes de conectar MQTT
        cambiar_modo_trabajo(MODO_CONFIGURADO_INICIAL);

        // C. Iniciar MQTT pasando el callback para cambios remotos
        con_mqtt_init(cambiar_modo_trabajo);
    }
}