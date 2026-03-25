#ifndef CON_MQTT_H
#define CON_MQTT_H

#include "esp_err.h"
#include "config.h" // <--- Al incluirlo aquí, ya tenemos acceso a modo_sistema_t

// 1. Declarar el tipo de callback
// Usamos el 'modo_sistema_t' que ya viene de config.h
typedef void (*mqtt_change_mode_cb_t)(modo_sistema_t nuevo_modo);

/**
 * @brief Inicializa el cliente MQTT
 * @param cb Callback para el cambio de modos (Aprendizaje/Control)
 * @param client_id Identificador único cargado desde la NVS
 */
void con_mqtt_init(mqtt_change_mode_cb_t cb, const char* client_id);

/**
 * @brief Envía un mensaje crudo a un tópico específico
 */
void mqtt_enviar_raw(const char *topic, const char *payload);

#endif