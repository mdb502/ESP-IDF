#ifndef FUNCIONES_H
#define FUNCIONES_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/rmt_types.h"

/**
 * @brief Estructura para retornar los resultados de búsqueda de códigos IR
 */
typedef struct {
    uint16_t address;
    uint16_t command;
    bool encontrado;
} resultado_busqueda_ir_t;

/**
 * @brief Inicializa la base de datos cargándola desde SPIFFS a la RAM.
 * @return ESP_OK si tuvo éxito.
 */
esp_err_t funciones_db_init(void);

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

#endif // FUNCIONES_H