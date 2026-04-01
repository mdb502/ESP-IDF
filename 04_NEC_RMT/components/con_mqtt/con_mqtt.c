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
extern const uint8_t cert_pem_start[] asm("_binary_isrg_root_x1_pem_start");
extern void tarea_sincronizacion_inicial(void *pvParameters);

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al Broker HiveMQ Cloud.");
            esp_mqtt_client_subscribe(client, MQTT_TOPIC_COMANDO, 1);
            break;

        case MQTT_EVENT_DATA: {
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            if (!root) return;
    
            cJSON *cmd_node = cJSON_GetObjectItem(root, KEY_MQTT_CMD);
				if (!cJSON_IsString(cmd_node)) {
				    cJSON_Delete(root);
				    return;
				}

            const char *cmd_val = cmd_node->valuestring;

            if (strcmp(cmd_val, VAL_CMD_DISP) == 0) {
			    // Caso 1: Disparo normal
			    if (mode_callback) mode_callback(MODO_REMOTO_CONTROL);
			    char *data_str = cJSON_PrintUnformatted(root);
			    xQueueSend(cola_control_ir, &data_str, 10);
			} 
			else if (strcmp(cmd_val, VAL_CMD_LEARN) == 0) {
			    // Caso 2: Aprender nuevo código
			    funciones_set_contexto_aprendizaje(root);
			    if (mode_callback) mode_callback(MODO_APRENDIZAJE);
			} 
			else if (strcmp(cmd_val, VAL_CMD_SYNC) == 0) {
			    // Caso 3: Forzar actualización de DB
			    xTaskCreate(tarea_sincronizacion_inicial, "sync_task", 8192, NULL, 5, NULL);
			}
    
            cJSON_Delete(root);
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error en MQTT Event");
            break;
        default: 
            break;
    }
}

void mqtt_enviar_raw(const char *topic, const char *payload) {
    if (client != NULL && payload != NULL) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
    }
}

void con_mqtt_init(mqtt_change_mode_cb_t cb, const char* client_id) {
    mode_callback = cb;
    
    // En ESP-IDF v5.x se recomienda inicializar la estructura de esta forma
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URL,
        .broker.address.port = MQTT_PORT,
        .broker.verification.certificate = (const char *)cert_pem_start,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id = client_id,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Cliente MQTT Iniciado ID: %s", client_id);
}