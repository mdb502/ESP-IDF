/*-------------------------------------
 Proyecto:		Control de luces
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

static const char *TAG = "MQTT_CORE";
static esp_mqtt_client_handle_t client = NULL;

// Solo necesitamos el puntero de inicio del certificado
extern const uint8_t cert_pem_start[] asm("_binary_isrg_root_x1_pem_start");

/**
 * @brief Manejador de eventos de MQTT
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    char sub_topic[128];

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al Broker HiveMQ Cloud.");

            // 1. Notificar inicio de sistema
            func_publicar_mensaje(TOPIC_INICIO, "inicio");

            // 2. Suscripción dinámica con log de ruta
            for (int i = 0; i < TOTAL_LUCES; i++) {
                // Suscripción a ESTADO
                snprintf(sub_topic, sizeof(sub_topic), "%sestado", luces[i].topic_base);
                esp_mqtt_client_subscribe(client, sub_topic, 1);
                ESP_LOGI(TAG, "Suscrito a topico de control: %s", sub_topic);

                // Suscripción a MODO
                snprintf(sub_topic, sizeof(sub_topic), "%smodo", luces[i].topic_base);
                esp_mqtt_client_subscribe(client, sub_topic, 1);
                ESP_LOGI(TAG, "Suscrito a topico de control: %s", sub_topic);
            }
            ESP_LOGI(TAG, "Suscripciones dinamicas finalizadas para %d luces.", TOTAL_LUCES);
            break;

        case MQTT_EVENT_DATA: {
            // Buffers seguros inicializados en cero
            char topic[128] = {0};
            char data[256] = {0};

            int t_len = (event->topic_len < 127) ? event->topic_len : 127;
            int d_len = (event->data_len < 255) ? event->data_len : 255;
            
            memcpy(topic, event->topic, t_len);
            memcpy(data, event->data, d_len);

            // Log de tráfico entrante
            ESP_LOGI(TAG, "MQTT DATA -> Topic: [%s] | Msg: [%s]", topic, data);

            for (int i = 0; i < TOTAL_LUCES; i++) {
                if (strstr(topic, luces[i].topic_base)) {
                    if (strstr(topic, "estado")) {
                        bool val = (strcmp(data, "1") == 0);
                        if (luces[i].estado != val) {
                            luces[i].estado = val;
                            gpio_set_level(luces[i].triac_pin, val);
                            ESP_LOGI(TAG, "Luz %d cambiada fisicamente a %s", i+1, val ? "ON" : "OFF");
                        }
                    } 
                    else if (strstr(topic, "modo")) {
                        // Delegamos el procesamiento del JSON a funciones.c
                        func_actualizar_logica_modo(i, data);
                    }
                    break;
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

void con_mqtt_init(void) {
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