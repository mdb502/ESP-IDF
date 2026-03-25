/*-------------------------------------
 Proyecto:		Control Remoto Universal
 Modulo:		con_mqtt.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Realiza la conexion al broker MQTT
 Descripcion:	- Ejecuta las tareas para conexión Segura
 				- Publica mensajes en topicos definidos en config.c
 				- Suscribe topicos
 				- procesa los mensajes recibidos
 --------------------------------------
*/

#include "con_mqtt.h"
#include "config.h"
#include "funciones.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"

static const char *TAG = "MQTT_CORE";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_change_mode_cb_t mode_callback = NULL;

extern QueueHandle_t cola_control_ir;

// Certificado ISRG Root X1 para HiveMQ Cloud
extern const uint8_t cert_pem_start[] asm("_binary_isrg_root_x1_pem_start");

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al Broker HiveMQ Cloud.");
			esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMANDO, 1);
    		ESP_LOGI(TAG, "Suscrito a: %s", MQTT_TOPIC_COMANDO);
            break;

        case MQTT_EVENT_DATA: {
            ESP_LOGI("MQTT_CORE", "Datos recibidos: %.*s", event->data_len, event->data);
    
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            if (!root) {
                ESP_LOGE("MQTT_CORE", "Error: JSON mal formado");
                return;
            }
    
            // Extraemos los campos
            cJSON *accion = cJSON_GetObjectItem(root, "accion");
            cJSON *modo   = cJSON_GetObjectItem(root, "modo"); // <-- Nueva llave
            cJSON *disp   = cJSON_GetObjectItem(root, "dispositivo");
            cJSON *btn    = cJSON_GetObjectItem(root, "boton");
    
            // --- LÓGICA DE DECISIÓN ACTUALIZADA ---

            // 1. ¿Es una orden de MODO? (Sincronizar)
            if (cJSON_IsString(modo) && strcmp(modo->valuestring, "sincronizar") == 0) {
                ESP_LOGW("MQTT_CORE", "Accion: Sincronización forzada desde MQTT");
                
                // Declaramos la función real que reside en main.c
                extern void tarea_sincronizacion_inicial(void *pvParameters);
                
                // Creamos la tarea para que corra de forma independiente
                // Stack de 8KB es suficiente para las peticiones HTTPS de Firebase
                xTaskCreate(tarea_sincronizacion_inicial, "sync_task_forced", 8192, NULL, 5, NULL);
            }
            // 2. ¿Es una orden de APRENDIZAJE?
            else if (cJSON_IsString(accion) && strcmp(accion->valuestring, "aprender") == 0) {
                ESP_LOGI("MQTT_CORE", "Accion: Entrando en modo Aprendizaje");
                funciones_set_contexto_aprendizaje(event->data, event->data_len);
                if (mode_callback) mode_callback(MODO_APRENDIZAJE);
            } 
            // 3. ¿Es un comando de DISPARO IR?
            else if (cJSON_IsString(disp) && cJSON_IsString(btn)) {
                ESP_LOGI("MQTT_CORE", "Accion: Disparo IR detectado (%s -> %s)", 
                         disp->valuestring, btn->valuestring);
                
                if (mode_callback) mode_callback(MODO_REMOTO_CONTROL);
                
                char *copy = strndup(event->data, event->data_len);
                if (xQueueSend(cola_control_ir, &copy, 0) != pdPASS) {
                    free(copy);
                    ESP_LOGE("MQTT_CORE", "Error: Cola de control llena");
                }
            }
            // 4. No coincide
            else {
                ESP_LOGW("MQTT_CORE", "JSON ignorado: No contiene modo, accion o par disp/btn válido");
            }
    
            cJSON_Delete(root);
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Desconectado del servidor MQTT.");
            break;

        default:
            break;
    }
}

void mqtt_enviar_raw(const char *topic, const char *payload) {
    if (client != NULL) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 0); // retain: 0 para datos de aprendizaje
    }
}

void con_mqtt_init(mqtt_change_mode_cb_t cb, const char* client_id) {
    mode_callback = cb;
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URL,
        .broker.address.port = MQTT_PORT,
        .broker.verification.certificate = (const char *)cert_pem_start,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        
        // CAMBIO CLAVE: Usamos el parámetro que viene de la g_config (NVS)
        .credentials.client_id = client_id,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Cliente MQTT arrancado.");
}