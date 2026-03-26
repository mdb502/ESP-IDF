#ifndef FUNCIONES_H
#define FUNCIONES_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "config.h" // Necesario para modo_sistema_t
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_types.h"

// --- ESTRUCTURAS DE DATOS ---

/**
 * @brief Resultado de una búsqueda en la base de datos local.
 */
typedef struct {
    bool encontrado;
    uint32_t address;
    uint32_t command;
    char norma[16]; // Añadimos este campo para guardar "NEC", "SHARP48", etc.
} resultado_busqueda_ir_t;

/**
 * @brief Contenedor de handles para el hardware IR (RMT).
 */
typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_channel_handle_t rx_chan;
    rmt_encoder_handle_t encoder;
    QueueHandle_t rx_queue;
} ir_core_t;


// --- GESTIÓN DE SISTEMA Y LOGS ---

/**
 * @brief Configura la verbosidad de los logs para limpiar la terminal.
 */
void inicializar_niveles_log(void);


// --- GESTIÓN DE HARDWARE (ENCAPSULADO) ---

/**
 * @brief Inicializa y habilita los periféricos RMT (TX y RX).
 * @return ESP_OK si el hardware está listo para operar.
 */
esp_err_t funciones_hardware_init(void);

/**
 * @brief Envía un comando IR mediante el protocolo NEC.
 */
void funciones_enviar_ir(uint16_t addr, uint16_t cmd);

/**
 * @brief Bloquea la tarea hasta recibir una trama NEC o alcanzar el timeout.
 */
esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd);


// --- GESTIÓN DE BASE DE DATOS (SPIFFS/RAM) ---

/**
 * @brief Carga la base de datos desde el archivo JSON en SPIFFS a la memoria RAM.
 */
esp_err_t funciones_db_init(void);

/**
 * @brief Guarda el estado actual de la caché JSON al archivo físico en SPIFFS.
 */
esp_err_t funciones_db_guardar_a_spiffs(void);

/**
 * @brief Verifica si un dispositivo existe en la base de datos local.
 */
bool funciones_db_existe_dispositivo(const char* id_dispositivo);

/**
 * @brief Borra la caché de dispositivos en RAM y actualiza el archivo local.
 */
void funciones_db_reiniciar(void);

/**
 * @brief Fusiona un nuevo objeto JSON de dispositivo en la base de datos local.
 */
esp_err_t funciones_db_fusionar_dispositivo(const char* id_dispositivo, cJSON* json_dispositivo_nuevo);


// --- TAREAS Y LÓGICA DE CONTROL ---

/**
 * @brief Tarea encargada de escuchar la cola de comandos y disparar el IR.
 */
void tarea_modo_remoto(void *pvParameters);

/**
 * @brief Tarea encargada de la captura de nuevos códigos y envío a Firebase.
 */
void tarea_modo_aprendizaje(void *pvParameters);

/**
 * @brief Tarea de fondo que sincroniza la configuración local con la nube.
 */
void tarea_sincronizacion_inicial(void *pvParameters);

/**
 * @brief Recibe un string JSON de control, busca el código en DB y lo envía.
 */
esp_err_t funciones_ejecutar_comando_desde_json(const char *json_data);

/**
 * @brief Configura el contexto (ID equipo/botón) para la siguiente captura IR.
 */
void funciones_set_contexto_aprendizaje(const char* data, int len);

/**
 * @brief Limpia de forma segura los mensajes pendientes en una cola.
 */
void funciones_limpiar_cola_comandos(QueueHandle_t cola);


// --- SOPORTE PROTOCOLO ---

/**
 * @brief Parsea una trama NEC desde símbolos RMT.
 */
bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);

/**
 * @brief Procesa el JSON de MQTT para extraer parámetros de búsqueda.
 */
resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data, int len);

#endif // FUNCIONES_H