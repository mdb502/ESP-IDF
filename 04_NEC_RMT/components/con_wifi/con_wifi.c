/*-------------------------------------
 Proyecto:		Control Remoto Universal
 Modulo:		con_wifi.c
 Version:		1.0 - 18.02.2026
 Objetivo:		realiza la conexion a WiFi como STA
 Descripcion:	Implementa el manejo de eventos para gestionar la conexion de forma asincrona.
				Como xEventGroupWaitBits tiene portMAX_DELAY, el proceso se bloquea hasta que conecte exitosamente.
				No tiene sentido continuar con el proceso ya que todo interactua via WiFi
 				
-------------------------------------
*/

#include "config.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"       // APIs de configuracion de WiFi
#include "esp_event.h"      // Sistema de eventos (WIFI_EVENT, IP_EVENT)
#include "esp_log.h"
#include "nvs_flash.h"
#include "con_wifi.h"

static const char *TAG = "WIFI_CONN";

/* Manejador del grupo de eventos para sincronizar tareas */
static EventGroupHandle_t s_wifi_event_group;

/* Definicion de bits para estados de conexion */
#define WIFI_CONNECTED_BIT BIT0  // Se activa al obtener IP
#define WIFI_FAIL_BIT      BIT1  // Se activa tras agotar reintentos
#define MAXIMUM_RETRY      10    // Limite de intentos antes de reiniciar

static int s_retry_num = 0; // Contador de reintentos actual

/**
 * @brief Manejador de eventos para WiFi e IP
 * Responde a cambios de estado en la pila de red de forma asincrona.
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data) {
    
    // Si el WiFi se ha iniciado correctamente, intentamos la conexion fisica
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    // Si se pierde la conexion o falla el intento de asociacion al AP
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Reintentando conexion al AP... (%d/%d)", s_retry_num, MAXIMUM_RETRY);
        } else {
            // Informamos al grupo de eventos que la conexion fallo definitivamente
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } 
    // Si el DHCP del router nos asigna una direccion IP valida
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Reiniciamos contador al tener exito
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta(const char* ssid, const char* pass) {
    // Creamos el grupo de eventos antes de iniciar la pila de red
    s_wifi_event_group = xEventGroupCreate();

    // Inicializacion de la pila TCP/IP interna del ESP-IDF
    //ESP_ERROR_CHECK(esp_netif_init());
    
    // Crear el bucle de eventos por defecto (necesario para drivers y protocolos)
    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Crear la instancia de red tipo Station (cliente WiFi)
    esp_netif_create_default_wifi_sta();

    // Configuracion de los recursos de bajo nivel del driver WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Registro de los manejadores de eventos para WiFi e IP
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    // Configuracion de credenciales y seguridad (tomadas de config.h)
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Minimo de seguridad requerido
        },
    };

    // Aplicar configuracion y arrancar el driver WiFi
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Driver iniciado. Esperando respuesta del router...");

    /* BLOQUEO CRiTICO: La tarea se queda aqui hasta que ocurra CONNECTED o FAIL.
       Esto cumple con tu requerimiento de no avanzar sin internet.
    */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, // No limpiar bits al salir (util para consultas posteriores)
            pdFALSE, // Cualquiera de los dos bits sirve para despertar
            portMAX_DELAY); // Espera infinita (el timeout lo maneja el contador de reintentos)

    // Evaluacion del resultado de la conexion
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conexion establecida con: %s", WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Error critico: No se pudo conectar a %s", WIFI_SSID);
        return ESP_FAIL;
    } else {
        return ESP_FAIL;
    }
}