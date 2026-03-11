#include "cJSON.h"
#include <stdio.h>
#include "funciones.h"
#include "config.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_tx.h" // Asegúrate de que estos existan
#include "ir_rx.h"
#include <string.h>
#include <sys/stat.h>


static const char *TAG = "FUNC_CORE";

// Estructura interna privada: Centralizamos TODO el hardware aquí
static struct {
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t encoder;
    rmt_channel_handle_t rx_chan;
    QueueHandle_t rx_queue;
    cJSON *db_cache;
} ir_core = {0};

/**
 * @brief Carga la DB desde SPIFFS a la RAM
 */
esp_err_t funciones_db_init(void) {
    if (ir_core.db_cache) cJSON_Delete(ir_core.db_cache);

    FILE* f = fopen("/spiffs/db_controles.json", "r");
    if (f == NULL) return ESP_FAIL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    fread(data, 1, fsize, f);
    fclose(f);
    data[fsize] = 0;

    ir_core.db_cache = cJSON_Parse(data);
    free(data);

    if (ir_core.db_cache == NULL) {
        ESP_LOGE(TAG, "Error al parsear JSON inicial");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Base de datos cargada en RAM exitosamente");
    return ESP_OK;
}

/**
 * @brief Inicializa TX y RX (Hardware opaco para el main)
 */
void funciones_hardware_init(void) {
    // Inicializar Transmisor
    ir_tx_init(&ir_core.tx_chan, &ir_core.encoder);
    rmt_enable(ir_core.tx_chan);

    // Inicializar Receptor
    ir_rx_init(&ir_core.rx_chan, &ir_core.rx_queue);
    rmt_enable(ir_core.rx_chan);

    ESP_LOGI(TAG, "Hardware IR (TX y RX) inicializado y habilitado");
}

/**
 * @brief Función puente para que la tarea de aprendizaje reciba datos
 * sin conocer los handles de RMT.
 */
esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t raw_symbols[64];
    rmt_rx_done_event_data_t rx_data;
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    // Disparamos la escucha
    ESP_ERROR_CHECK(rmt_receive(ir_core.rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg));

    // Esperamos el evento de la cola interna
    if (xQueueReceive(ir_core.rx_queue, &rx_data, portMAX_DELAY) == pdPASS) {
        if (rx_data.num_symbols == 34 && funciones_nec_parse_frame(rx_data.received_symbols, addr, cmd)) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}


bool funciones_check_in_range(uint32_t signal_duration, uint32_t spec_duration) {
    return (signal_duration < (spec_duration + EXAMPLE_IR_NEC_DECODE_MARGIN)) &&
           (signal_duration > (spec_duration - EXAMPLE_IR_NEC_DECODE_MARGIN));
}

bool funciones_nec_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols) {
    return funciones_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ZERO_DURATION_0) &&
           funciones_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ZERO_DURATION_1);
}

bool funciones_nec_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols) {
    return funciones_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ONE_DURATION_0) &&
           funciones_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ONE_DURATION_1);
}

bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t *cur = rmt_symbols;
    uint16_t address = 0;
    uint16_t command = 0;

    // Validar cabecera (Leading code)
    if (!funciones_check_in_range(cur->duration0, NEC_LEADING_CODE_DURATION_0) ||
        !funciones_check_in_range(cur->duration1, NEC_LEADING_CODE_DURATION_1)) {
        return false;
    }
    cur++;

    // Parsear Dirección (16 bits) e Instrucción (16 bits)
    for (int i = 0; i < 16; i++, cur++) {
        if (funciones_nec_parse_logic1(cur)) address |= (1 << i);
    }
    for (int i = 0; i < 16; i++, cur++) {
        if (funciones_nec_parse_logic1(cur)) command |= (1 << i);
    }

    *addr = address;
    *cmd = command;
    return true;
}

void funciones_parse_nec_display(rmt_symbol_word_t *rmt_symbols, size_t symbol_num) {
    uint16_t addr = 0, cmd = 0;
    if (symbol_num == 34 && funciones_nec_parse_frame(rmt_symbols, &addr, &cmd)) {
        printf("NEC Recibido: Addr=0x%04X, Cmd=0x%04X\n", addr, cmd);
    } else if (symbol_num == 2) {
        printf("NEC: Repeat Code\n");
    } else {
        printf("NEC: Frame Desconocido (%d símbolos)\n", symbol_num);
    }
}


/**
 * @brief Búsqueda ultra-rápida en RAM
 */
resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data, int len) {
    resultado_busqueda_ir_t res = {0, 0, false};
    if (!ir_core.db_cache) return res;

    cJSON *root_mqtt = cJSON_ParseWithLength(json_data, len);
    if (!root_mqtt) return res;

    cJSON *id_buscado = cJSON_GetObjectItem(root_mqtt, "id");
    cJSON *cmd_buscado = cJSON_GetObjectItem(root_mqtt, "c");

    if (cJSON_IsString(id_buscado) && cJSON_IsString(cmd_buscado)) {
        cJSON *dispositivo = NULL;
        cJSON_ArrayForEach(dispositivo, ir_core.db_cache) {
            cJSON *id_db = cJSON_GetObjectItem(dispositivo, "id");
            if (cJSON_IsString(id_db) && strcmp(id_db->valuestring, id_buscado->valuestring) == 0) {
                cJSON *comandos = cJSON_GetObjectItem(dispositivo, "comandos");
                cJSON *cmd_obj = NULL;
                cJSON_ArrayForEach(cmd_obj, comandos) {
                    cJSON *c_name = cJSON_GetObjectItem(cmd_obj, "c");
                    if (cJSON_IsString(c_name) && strcmp(c_name->valuestring, cmd_buscado->valuestring) == 0) {
                        res.address = (uint16_t)strtol(cJSON_GetObjectItem(cmd_obj, "addr")->valuestring, NULL, 16);
                        res.command = (uint16_t)strtol(cJSON_GetObjectItem(cmd_obj, "cmd")->valuestring, NULL, 16);
                        res.encontrado = true;
                        break;
                    }
                }
            }
            if (res.encontrado) break;
        }
    }
    cJSON_Delete(root_mqtt);
    return res;
}

/**
 * @brief Envío directo usando el handle interno
 */
void funciones_enviar_ir(uint16_t addr, uint16_t cmd) {
    if (!ir_core.tx_chan) return;
    ir_nec_scan_code_t codigo = { .address = addr, .command = cmd };
    rmt_transmit_config_t transmit_cfg = {.loop_count = 0};
    rmt_transmit(ir_core.tx_chan, ir_core.encoder, &codigo, sizeof(codigo), &transmit_cfg);
}
