#ifndef CON_MQTT_H
#define CON_MQTT_H

#include "config.h"

// 1. Declarar el tipo de callback
typedef void (*mqtt_change_mode_cb_t)(modo_sistema_t nuevo_modo);

// 2. Actualizar la firma para que acepte el callback
void con_mqtt_init(mqtt_change_mode_cb_t cb);

void mqtt_enviar_raw(const char *topic, const char *payload);

#endif