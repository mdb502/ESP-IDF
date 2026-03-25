/*-------------------------------------
 Proyecto:		Control de luces
 Modulo:		con_ntp.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Sincronizar reloj interno con hora real
 Descripcion:	Conecta con NTP para sincronizar con hora real
 				Realiza sincronizaciones periodicas para evira derivas
 --------------------------------------
*/

#include "config.h"
#include "con_ntp.h"
#include "esp_sntp.h"
#include "esp_log.h" 
#include "funciones.h" 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>     // Necesario para setenv y tzset

static const char *TAG = "CON_NTP";

static void ntp_sync_task(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando servicio SNTP...");
    
    // a) Configuración de Zona Horaria usando tu define
    setenv("TZ", TZ_CHILE, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    while (1) {
        int intentos = 0;
        const int MAX_INTENTOS = 15;

        while (intentos < MAX_INTENTOS) {
            sntp_sync_status_t status = sntp_get_sync_status();
            
            if (status == SNTP_SYNC_STATUS_COMPLETED) {
                if (!hora_sincronizada) { 
                    hora_sincronizada = true;
                    // b) Uso de TOPIC_EVENTO definido en config.h
                    func_publicar_mensaje(TOPIC_EVENTO, "hora sincronizada");
                }
                
                // c) Log con timestamp en consola
                char t_str[25];
                func_obtener_timestamp(t_str, sizeof(t_str));
                ESP_LOGI(TAG, "[%s] Hora sincronizada. Re-verificando en 1 hora...", t_str);
                
                vTaskDelay(pdMS_TO_TICKS(3600000)); 
                intentos = 0;
                continue; 
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
            intentos++;
        }

        hora_sincronizada = false;
        ESP_LOGW(TAG, "Sin respuesta NTP. Reintento en 1 min.");
        func_publicar_mensaje(TOPIC_EVENTO, "sin tiempo real - modo manual");
        
        vTaskDelay(pdMS_TO_TICKS(60000)); 
    }
}

void con_ntp_init(void) {
    xTaskCreate(ntp_sync_task, "ntp_sync_task", 4096, NULL, 5, NULL);
}
