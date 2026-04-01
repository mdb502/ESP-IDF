#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_spiffs.h"

#include "config.h"
#include "con_param.h"
#include "con_wifi.h"
#include "con_mqtt.h"
#include "con_provision.h"
#include "funciones.h"

static const char *TAG = "MAIN_APP";

static TaskHandle_t xTareaIR = NULL;
QueueHandle_t cola_control_ir = NULL;


static esp_err_t init_spiffs(void) {
    ESP_LOGI(TAG, "Inicializando SPIFFS...");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo al montar SPIFFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

void cambiar_modo_trabajo(modo_sistema_t nuevo_modo) {
    if (xTareaIR != NULL) {
        vTaskDelete(xTareaIR);
        xTareaIR = NULL;
    }
    if (nuevo_modo == MODO_REMOTO_CONTROL) {
        funciones_limpiar_cola_comandos(cola_control_ir);
        xTaskCreate(tarea_modo_remoto, "task_remote", 4096, NULL, 5, &xTareaIR);
    } else {
        xTaskCreate(tarea_modo_aprendizaje, "task_learn", 8192, NULL, 5, &xTareaIR);
    }
}

void app_main(void) {
    inicializar_niveles_log();
    
    esp_err_t ret = param_init_nvs();
    if (ret != ESP_OK) esp_restart();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (param_load_config(&g_config) != ESP_OK) {
        provision_start(); 
        return; 
    }

    if (init_spiffs() == ESP_OK) {
        funciones_db_init(); 
    }

    if (wifi_init_sta(g_config.wifi_ssid, g_config.wifi_pass) == ESP_OK) {
        funciones_hardware_init();
        cola_control_ir = xQueueCreate(10, sizeof(char *));
        
        // 1. Sincronizamos PRIMERO (puedes hacerla bloqueante o esperar un evento)
        // Por ahora, lanzamos la tarea pero asegúrate que funcione.
        xTaskCreate(tarea_sincronizacion_inicial, "sync_task", 8192, NULL, 10, NULL);
        
        // 2. Iniciamos MQTT y el modo de trabajo
        con_mqtt_init(cambiar_modo_trabajo, g_config.mqtt_id);
        cambiar_modo_trabajo(MODO_REMOTO_CONTROL);
    }
    
    ESP_LOGI(TAG, "Sistema listo. Heap: %u", (unsigned int)esp_get_free_heap_size());
    mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"SYSTEM_BOOT\", \"version\":\"1.1\"}");
}