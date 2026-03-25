/*-------------------------------------
 Proyecto:		Control de luces
 Modulo:		main.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Logica principal del proyecto
 Descripcion:	Inicialización de componentes y coordinación de la ejecución.
 				
 --------------------------------------
*/

#include "config.h"
#include <stdio.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "con_wifi.h"
#include "con_ntp.h"
#include "con_mqtt.h"
#include "con_luces.h"
#include "funciones.h"

static const char *TAG = "MAIN_APP";

void app_main(void) {
	// 1. CONFIGURACIÓN DE LOGS (Solo despliegues del proceso)
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);
    
    // Logs de nuestra lógica (Información detallada)
    esp_log_level_set("MAIN_APP", ESP_LOG_INFO);
    esp_log_level_set("TRIAC_MASTER", ESP_LOG_INFO); // Nueva etiqueta centralizada
    esp_log_level_set("FUNC_LOGIC", ESP_LOG_INFO);
    esp_log_level_set("NVS_S3", ESP_LOG_INFO);

    // 2. Inicializar almacenamiento persistente (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Inicializar Hardware de Luces (Pines y Tarea de interrupciones)
    // Se hace antes de WiFi para que las luces inicien apagadas de inmediato
    con_luces_init();

    // 4. Conectar a WiFi (Bloqueante según requerimiento)
    ESP_LOGI(TAG, "Iniciando fase de conexion WiFi...");
    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "Error critico en WiFi. Reiniciando...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }

    // 5. Sincronizar tiempo real con NTP
    con_ntp_init();

    // 6. Conectar a HiveMQ Cloud
    con_mqtt_init();
    ESP_LOGI(TAG, "Sistema operativo con %d luces.", TOTAL_LUCES);
    
    // Inicia tarea de vigilancia de horarios (está en funciones.c)
    xTaskCreate(tarea_control_luces, "control_luces", 4096, NULL, 5, NULL);

    // Bucle principal para el reporte de control de funcionamiento
    while(1) {
        // Reporte cada X minutos (puedes definir MINUTOS_REPORTE en config.h)
        vTaskDelay(pdMS_TO_TICKS(MINUTOS_REPORTE * 60000));
        
        func_publicar_mensaje(TOPIC_CONTROL, "funcionando");
        ESP_LOGI(TAG, "Enviando mensaje de control: funcionando");
    }
}
