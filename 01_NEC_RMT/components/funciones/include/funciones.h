#ifndef FUNCIONES_H
#define FUNCIONES_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/rmt_types.h"
#include "driver/rmt_tx.h" // Añadido para los handles
#include "driver/rmt_rx.h" // Añadido para los handles
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "cJSON.h"


/**
 * @brief Estructura central de hardware IR
 */
typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_channel_handle_t rx_chan;
    rmt_encoder_handle_t encoder;
    QueueHandle_t rx_queue;
} ir_core_t;

// Declaramos que ir_core existe globalmente en funciones.c
extern ir_core_t ir_core;


/**
 * @brief Estructura para retornar los resultados de búsqueda de códigos IR
 */
typedef struct {
    uint16_t address;
    uint16_t command;
    bool encontrado;
} resultado_busqueda_ir_t;


// --- GESTIÓN DE SISTEMA Y LOGS ---
void inicializar_niveles_log(void);


// --- GESTIÓN DE BASE DE DATOS (RAM & SPIFFS) ---
/**
 * @brief Inicializa la base de datos cargándola desde SPIFFS a la RAM.
 * @return ESP_OK si tuvo éxito.
 */
esp_err_t funciones_db_init(void);

/**
 * @brief Verifica si un dispositivo ya existe en la caché local.
 */
bool funciones_db_existe_dispositivo(const char* id_dispositivo);

/**
 * @brief Fusiona un nuevo objeto de dispositivo en la base de datos actual.
 * Actualiza SPIFFS automáticamente.
 */
esp_err_t funciones_db_fusionar_dispositivo(const char* id_dispositivo, cJSON* json_dispositivo_nuevo);

/**
 * @brief Guarda el estado actual de la db_cache de RAM al archivo físico en SPIFFS.
 */
esp_err_t funciones_db_guardar_a_spiffs(void);

/**
 * @brief Actualiza o agrega un comando específico de forma local (útil tras aprendizaje).
 */
esp_err_t funciones_db_actualizar_comando_local(const char* id_disp, const char* boton, uint16_t addr, uint16_t cmd);



// --- HARDWARE Y PROCESAMIENTO ---
/**
 * @brief Configura y habilita los periféricos RMT para TX y RX.
 */
void funciones_hardware_init(void);

/**
 * @brief Procesa un JSON de entrada (MQTT) y busca el código en la caché de RAM.
 * @param json_data Puntero al string JSON.
 * @param len Longitud del string.
 * @return Estructura con el código encontrado o bandera de error.
 */
resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data, int len);

/**
 * @brief Envía una señal IR usando el protocolo NEC.
 * @param addr Dirección del dispositivo.
 * @param cmd Código del comando.
 */
void funciones_enviar_ir(uint16_t addr, uint16_t cmd);

/**
 * @brief Bloquea la tarea actual hasta recibir una trama IR válida.
 * @param addr Puntero donde se guardará la dirección recibida.
 * @param cmd Puntero donde se guardará el comando recibido.
 * @return ESP_OK si se recibió una trama válida.
 */
esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd);

/**
 * @brief Parsea una trama NEC desde símbolos RMT.
 */
bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);


// --- COMUNICACIÓN Y CONTEXTO ---
/**
 * @brief Actualiza el contexto de qué dispositivo/botón se está aprendiendo.
 */
void actualizar_contexto_aprendizaje(const char* data, int len);

/**
 * @brief Envía un comando IR capturado a Firebase (Implementado en con_firebase.c)
 */
esp_err_t con_firebase_patch_comando(const char* disp, const char* btn, uint16_t addr, uint16_t cmd);


#endif // FUNCIONES_H