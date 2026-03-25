#ifndef CON_MQTT_H
#define CON_MQTT_H

#include <stdbool.h> // Necesario para el tipo bool

/**
 * @brief Inicializa el cliente MQTT con soporte TLS para HiveMQ Cloud
 */
void con_mqtt_init(void);

void mqtt_enviar_raw(const char *topic, const char *payload);
#endif