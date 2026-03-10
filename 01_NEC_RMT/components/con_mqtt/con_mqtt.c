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

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "con_mqtt.h"
#include "funciones.h"
#include <stdlib.h>

static const char *TAG = "MQTT_CORE";
static esp_mqtt_client_handle_t client = NULL;
static mqtt_change_mode_cb_t mode_callback = NULL;


// Solo necesitamos el puntero de inicio del certificado
extern const uint8_t cert_pem_start[] asm("_binary_isrg_root_x1_pem_start");

/**
 * @brief Manejador de eventos de MQTT
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al Broker HiveMQ Cloud.");

            esp_mqtt_client_subscribe(client, TOPIC_MODO, 1);
			esp_mqtt_client_subscribe(client, TOPIC_CMD, 1);
            break;

        case MQTT_EVENT_DATA: {
            // Extraer datos de forma segura
            char data[16] = {0};
            int len = (event->data_len < 15) ? event->data_len : 15;
            memcpy(data, event->data, len);

            if (strncmp(event->topic, TOPIC_MODO, event->topic_len) == 0) {
                int modo_int = atoi(data);
                if (mode_callback != NULL) {
                    mode_callback((modo_sistema_t)modo_int);
                }
            }
            break;
        }

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Evento: Desconectado del servidor MQTT.");
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Evento: Error en comunicación MQTT.");
            break;

        default:
            break;
    }
}

void mqtt_enviar_raw(const char *topic, const char *payload) {
    if (client != NULL) {
        // qos: 1, retain: 1 para mantener el último estado conocido
        esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
    }
}

void con_mqtt_init(mqtt_change_mode_cb_t cb) {
	mode_callback = cb; // Guardamos la función que viene de main
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
    ESP_LOGI(TAG, "Cliente MQTT configurado y arrancado.");
}