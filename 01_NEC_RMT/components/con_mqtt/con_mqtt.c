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
            esp_mqtt_client_subscribe(client, TOPIC_MODO, 1);
            esp_mqtt_client_subscribe(client, TOPIC_CMD, 1);
            break;

        case MQTT_EVENT_DATA: {
            // --- CASO 1: COMANDO IR ---
            if (strncmp(event->topic, TOPIC_CMD, event->topic_len) == 0) {
                char *mensaje_para_cola = malloc(event->data_len + 1);
                if (mensaje_para_cola != NULL) {
                    memcpy(mensaje_para_cola, event->data, event->data_len);
                    mensaje_para_cola[event->data_len] = '\0';

                    if (xQueueSend(cola_control_ir, &mensaje_para_cola, 0) != pdPASS) {
                        free(mensaje_para_cola);
                        ESP_LOGW(TAG, "Cola llena, comando descartado");
                    } else {
                        ESP_LOGI(TAG, "Comando IR enviado a la cola");
                    }
                }
            } 
            // --- CASO 2: CAMBIO DE MODO ---
            else if (strncmp(event->topic, TOPIC_MODO, event->topic_len) == 0) {
                if (mode_callback != NULL) {
                    if (strncmp(event->data, "LEARN", event->data_len) == 0) {
                        mode_callback(MODO_APRENDIZAJE);
                    } else if (strncmp(event->data, "REMOTE", event->data_len) == 0) {
                        mode_callback(MODO_REMOTO_CONTROL);
                    }
                }
            }
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

void con_mqtt_init(mqtt_change_mode_cb_t cb) {
    mode_callback = cb;
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URL,
        .broker.address.port = MQTT_PORT,
        .broker.verification.certificate = (const char *)cert_pem_start,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .credentials.client_id = MQTT_CLIENT_ID,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "Cliente MQTT arrancado.");
}