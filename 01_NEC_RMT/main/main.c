#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "con_wifi.h"
#include "con_mqtt.h"
#include "esp_log.h"
#include "funciones.h" // Aquí reside ahora toda la lógica IR
#include "config.h"
#include "esp_spiffs.h"

static const char *TAG = "MAIN_APP";
static TaskHandle_t xTareaIR = NULL;
QueueHandle_t cola_control_ir = NULL;

// --- PROTOTIPOS ---
void tarea_modo_aprendizaje(void *pvParameters);
void tarea_modo_remoto(void *pvParameters);
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo);
esp_err_t init_spiffs(void);

// --- GESTIÓN DE MODOS ---
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo) {
    if (xTareaIR != NULL) {
        ESP_LOGI(TAG, "Cambiando modo. Deteniendo tarea actual...");
        vTaskDelete(xTareaIR);
        xTareaIR = NULL;
    }

    if (nuevo_modo == MODO_REMOTO_CONTROL) {
        // Limpiar cola de comandos pendientes antes de iniciar
        char *tmp;
        while (cola_control_ir && xQueueReceive(cola_control_ir, &tmp, 0) == pdPASS) {
            free(tmp);
        }
        xTaskCreate(tarea_modo_remoto, "task_remote", 4096, NULL, 5, &xTareaIR);
        ESP_LOGI(TAG, "Modo CONTROL REMOTO activo");
    } else {
        xTaskCreate(tarea_modo_aprendizaje, "task_learn", 4096, NULL, 5, &xTareaIR);
        ESP_LOGI(TAG, "Modo APRENDIZAJE activo");
    }
}

// --- TAREA: EJECUCIÓN DE COMANDOS ---
void tarea_modo_remoto(void *pvParameters) {
    char *json_ptr; 
    while (1) {
        if (xQueueReceive(cola_control_ir, &json_ptr, portMAX_DELAY) == pdPASS) {
            // Búsqueda en caché RAM (Capa de funciones)
            resultado_busqueda_ir_t res = funciones_procesar_control_mqtt(json_ptr, strlen(json_ptr));

            if (res.encontrado) {
                funciones_enviar_ir(res.address, res.command);
                ESP_LOGI(TAG, "IR Enviado: Addr 0x%04X, Cmd 0x%04X", res.address, res.command);
            } else {
                ESP_LOGW(TAG, "Comando no reconocido en JSON recibido");
            }
            free(json_ptr); // Liberar memoria asignada en con_mqtt.c
        }
    }
}

// --- TAREA: APRENDIZAJE DE CÓDIGOS ---
void tarea_modo_aprendizaje(void *pvParameters) {
    // Nota: funciones_esperar_y_parsear_ir debe ser implementada en funciones.c
    // para encapsular el xQueueReceive del canal RX.
    while (1) {
        uint16_t addr = 0, cmd = 0;
        if (funciones_esperar_y_parsear_ir(&addr, &cmd) == ESP_OK) {
            char payload[128];
            snprintf(payload, sizeof(payload), 
                     "{\"id\":\"NUEVO_DISP\", \"addr\":\"0x%04X\", \"cmd\":\"0x%04X\"}", 
                     addr, cmd);

            mqtt_enviar_raw(TOPIC_LEARN, payload);
            ESP_LOGI(TAG, "MQTT Learn enviado: %s", payload);
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

    init_spiffs();
    funciones_db_init(); // Carga la DB en RAM

    if (wifi_init_sta() == ESP_OK) {
        funciones_hardware_init(); // Inicia TX y RX internamente
        
        cola_control_ir = xQueueCreate(10, sizeof(char *));
        
        // Iniciamos en modo remoto por defecto
        cambiar_modo_trabajo(MODO_REMOTO_CONTROL);
        
        // Pasamos el callback para cambios de modo vía MQTT
        con_mqtt_init(cambiar_modo_trabajo);
    }
}

// --- IMPLEMENTACIÓN DE INIT_SPIFFS ---
esp_err_t init_spiffs(void) {
    ESP_LOGI("SPIFFS", "Inicializando SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage", // Debe coincidir con tu partitions.csv
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SPIFFS", "Fallo al montar o formatear el sistema de archivos");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("SPIFFS", "No se encontró la partición SPIFFS");
        } else {
            ESP_LOGE("SPIFFS", "Error al inicializar SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Fallo al obtener información de la partición (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI("SPIFFS", "Partición OK. Total: %d, Usado: %d", total, used);
    }
    return ESP_OK;
}