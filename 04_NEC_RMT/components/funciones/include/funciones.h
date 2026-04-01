#ifndef FUNCIONES_H
#define FUNCIONES_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "config.h" // <--- Aquí ya viven las estructuras globales
#include "driver/rmt_types.h"

// --- GESTIÓN DE SISTEMA Y LOGS ---
void inicializar_niveles_log(void);

// --- GESTIÓN DE HARDWARE ---
esp_err_t funciones_hardware_init(void);
void funciones_enviar_ir(uint16_t addr, uint16_t cmd);
void funciones_enviar_ir_sharp_48(uint16_t addr, uint16_t cmd);
esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd, char *norma_out);

// --- GESTIÓN DE BASE DE DATOS (SPIFFS/RAM) ---
esp_err_t funciones_db_init(void);
esp_err_t funciones_db_guardar_a_spiffs(void);
bool funciones_db_existe_dispositivo(const char* id_dispositivo);
void funciones_db_reiniciar(void);
esp_err_t funciones_db_fusionar_dispositivo(const char* id_dispositivo, cJSON* json_dispositivo_nuevo);

// --- TAREAS Y LÓGICA DE CONTROL ---
void tarea_modo_remoto(void *pvParameters);
void tarea_modo_aprendizaje(void *pvParameters);
void tarea_sincronizacion_inicial(void *pvParameters);

// Ajustado para coincidir con funciones.c
void funciones_limpiar_cola_comandos(QueueHandle_t cola);
void funciones_ejecutar_comando_desde_json(const char *json_data);
void funciones_set_contexto_aprendizaje(cJSON *data_node);
void utils_trim(char *dest, const char *src, size_t dest_size);

// --- SOPORTE PROTOCOLO ---
bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);
bool funciones_sharp_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);
resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data);

#endif // FUNCIONES_H