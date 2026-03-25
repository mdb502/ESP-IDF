
#ifndef FUNCIONES_H
#define FUNCIONES_H

#include <stddef.h>
#include "config.h"

/**
 * @brief Publica un mensaje en un tópico MQTT incluyendo el timestamp
 */
void func_publicar_mensaje(const char *topic, const char *msg);

/**
 * @brief Obtiene el tiempo actual formateado para logs: YYYY-MM-DD HH:MM:SS
 */
void func_obtener_timestamp(char *dest, size_t max_len);
void func_generar_timeline_modo4(int i);
bool func_check_timeline(int i, time_t ahora);

/**
 * @brief Procesa el JSON de modo y actualiza la estructura de la luz (Modos 1-5)
 */
void func_actualizar_logica_modo(int indice, const char* json_data);

/**
 * @brief Tarea que vigila los horarios de las luces (Modos 4 y 5)
 * Se ejecuta en segundo plano desde el app_main
 */
void tarea_control_luces(void *pvParameters); 

#endif