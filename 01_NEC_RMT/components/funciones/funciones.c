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

// Definición de la variable global
ir_core_t ir_core = {0};

static cJSON *db_cache = NULL;

/**
 * @brief Carga la DB desde SPIFFS a la RAM
 */
esp_err_t funciones_db_init(void) {
    // Usamos la variable static local, no ir_core
    if (db_cache) cJSON_Delete(db_cache);

    FILE* f = fopen("/spiffs/db_controles.json", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "No se pudo abrir el archivo de DB");
        return ESP_FAIL;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    if (!data) return ESP_ERR_NO_MEM; // Buena práctica: check de memoria

    fread(data, 1, fsize, f);
    fclose(f);
    data[fsize] = 0;

    db_cache = cJSON_Parse(data); // <--- Cambio aquí
    free(data);

    if (db_cache == NULL) { // <--- Cambio aquí
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
    // Inicializar Transmisor y Receptor
    ir_tx_init(&ir_core.tx_chan, &ir_core.encoder);
    ir_rx_init(&ir_core.rx_chan, &ir_core.rx_queue);

    ESP_LOGI(TAG, "Hardware IR inicializado y listo para habilitar.");
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

    // --- REINICIO DE EMERGENCIA SILENCIOSO ---
    // Si el driver está confundido, lo forzamos a un estado conocido
    rmt_disable(ir_core.rx_chan); 
    rmt_enable(ir_core.rx_chan); 

    // Limpiamos la cola de cualquier residuo
    xQueueReset(ir_core.rx_queue);

    // Intentamos recibir
    esp_err_t ret = rmt_receive(ir_core.rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg);
    
    if (ret != ESP_OK) {
        // Si sigue fallando, es que algo más grave pasa con el GPIO o el Handle
        ESP_LOGE("FUNC", "Error persistente en RMT: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(500)); 
        return ret;
    }

    ESP_LOGI("FUNC", "Escuchando IR (Modo Aprendizaje)...");
	
/*
    // Esperamos los 10 segundos
    if (xQueueReceive(ir_core.rx_queue, &rx_data, pdMS_TO_TICKS(10000)) == pdPASS) {
        if (rx_data.num_symbols >= 34 && funciones_nec_parse_frame(rx_data.received_symbols, addr, cmd)) {
            return ESP_OK;
        }
        ESP_LOGW("FUNC", "Señal recibida no es NEC o está incompleta");
    } else {
        ESP_LOGW("FUNC", "Timeout: No se detectó ninguna señal");
    }

*/
	if (xQueueReceive(ir_core.rx_queue, &rx_data, pdMS_TO_TICKS(10000)) == pdPASS) {
        // Log de diagnóstico:
        ESP_LOGI("DIAG", "Símbolos recibidos: %d", rx_data.num_symbols);

        if (rx_data.num_symbols >= 34 && funciones_nec_parse_frame(rx_data.received_symbols, addr, cmd)) {
            return ESP_OK;
        } else {
            ESP_LOGW("DIAG", "La señal no coincide con el protocolo NEC esperado.");
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
    
    // 1. Usar la variable local db_cache
    if (!db_cache) return res; 

    cJSON *root_mqtt = cJSON_ParseWithLength(json_data, len);
    cJSON *id_buscado = cJSON_GetObjectItem(root_mqtt, "id");
    cJSON *cmd_buscado = cJSON_GetObjectItem(root_mqtt, "c");

    if (id_buscado && cmd_buscado) {
        // 2. Aquí estaba el error, cambiamos ir_core.db_cache por db_cache
        cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
        cJSON *disp = cJSON_GetObjectItem(cremotos, id_buscado->valuestring);
        
        if (disp) {
            cJSON *btn = cJSON_GetObjectItem(disp, cmd_buscado->valuestring);
            if (cJSON_IsString(btn)) {
                uint32_t raw = strtoul(btn->valuestring, NULL, 16);
                res.address = (raw >> 16) & 0xFFFF;
                res.command = raw & 0xFFFF;
                res.encontrado = true;
            }
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
