// components/funciones/funciones.c
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "funciones.h"
#include "config.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "ir_tx.h" 
#include "ir_rx.h"
#include "con_firebase.h"
#include "con_mqtt.h"

static const char *TAG = "FUNC_CORE";
extern app_config_t g_config;

// --- VARIABLES PRIVADAS (Encapsulamiento) ---
static ir_core_t ir_core = {0};
static cJSON *db_cache = NULL;
static context_aprendizaje_t ctx_learn = {0};
static bool listo_para_capturar = false;

// --- PROTOTIPOS DE FUNCIONES INTERNAS (Evita errores de implicit declaration) ---
bool funciones_sharp_check_range(uint32_t signal, uint32_t spec);
int8_t funciones_sharp_get_bit(rmt_symbol_word_t *sym);
bool funciones_sharp_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);
bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);

// ==========================================
// 1. GESTIÓN DE HARDWARE Y LOGS
// ==========================================

void inicializar_niveles_log(void) {
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);
    esp_log_level_set("FUNC_CORE", ESP_LOG_INFO);
    esp_log_level_set("MAIN_APP", ESP_LOG_INFO);
    ESP_LOGI(TAG, "Logs configurados.");
}

esp_err_t funciones_hardware_init(void) {
    ir_tx_init(&ir_core.tx_chan, &ir_core.encoder);
    ir_rx_init(&ir_core.rx_chan, &ir_core.rx_queue);

    esp_err_t ret = rmt_enable(ir_core.tx_chan);
    if (ret == ESP_OK) ret = rmt_enable(ir_core.rx_chan);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Hardware IR inicializado y habilitado (TX/RX).");
    } else {
        ESP_LOGE(TAG, "Error al habilitar RMT: %s", esp_err_to_name(ret));
    }
    return ret;
}

// ==========================================
// 2. PROTOCOLO NEC (BAJO NIVEL)
// ==========================================

bool funciones_check_in_range(uint32_t signal_duration, uint32_t spec_duration) {
    return (signal_duration < (spec_duration + EXAMPLE_IR_NEC_DECODE_MARGIN)) &&
           (signal_duration > (spec_duration - EXAMPLE_IR_NEC_DECODE_MARGIN));
}

bool funciones_nec_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols) {
    return funciones_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ONE_DURATION_0) &&
           funciones_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ONE_DURATION_1);
}

bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t *cur = rmt_symbols;
    uint16_t address = 0, command = 0;

    if (!funciones_check_in_range(cur->duration0, NEC_LEADING_CODE_DURATION_0) ||
        !funciones_check_in_range(cur->duration1, NEC_LEADING_CODE_DURATION_1)) return false;
    cur++;

    for (int i = 0; i < 16; i++, cur++) if (funciones_nec_parse_logic1(cur)) address |= (1 << i);
    for (int i = 0; i < 16; i++, cur++) if (funciones_nec_parse_logic1(cur)) command |= (1 << i);

    *addr = address; *cmd = command;
    return true;
}

// ==========================================
// 3. PROTOCOLO SHARP (BAJO NIVEL)
// ==========================================

bool funciones_sharp_check_range(uint32_t signal, uint32_t spec) {
    return (signal < (spec + SHARP_DECODE_MARGIN)) &&
           (signal > (spec - SHARP_DECODE_MARGIN));
}

int8_t funciones_sharp_get_bit(rmt_symbol_word_t *sym) {
    // Cambiamos SHARP_BIT_MARK_DURATION por SHARP_BIT_MARK
    if (!funciones_sharp_check_range(sym->duration0, SHARP_BIT_MARK)) return -1;
    
    if (funciones_sharp_check_range(sym->duration1, SHARP_BIT_ONE_SPACE)) return 1;
    if (funciones_sharp_check_range(sym->duration1, SHARP_BIT_ZERO_SPACE)) return 0;
    
    return -1;
}

bool funciones_sharp_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    // 1. Validar que tenemos suficientes símbolos antes de empezar
    // (Lead + 48 bits + 1 stop = 50 símbolos)
    // Usamos una variable para el conteo para evitar confusiones
    const int BITS_ESPERADOS = 48;

    // 2. Validar Leading Code (Símbolo 0) con el margen de seguridad
    if (!funciones_sharp_check_range(rmt_symbols[0].duration0, SHARP_H_LEAD_0) ||
        !funciones_sharp_check_range(rmt_symbols[0].duration1, SHARP_H_LEAD_1)) {
        return false;
    }

    uint64_t data = 0;
    
    // 3. Parsear los bits con protección de índice
    for (int i = 0; i < BITS_ESPERADOS; i++) {
        // El bit i está en el símbolo i + 1 (porque el 0 es el Lead)
        rmt_symbol_word_t sym = rmt_symbols[i + 1]; 
        
        // Si el pulso (duration0) no coincide con el MARK de Sharp, abortamos
        if (!funciones_sharp_check_range(sym.duration0, SHARP_BIT_MARK)) {
            return false;
        }

        // Evaluamos el espacio (duration1)
        if (funciones_sharp_check_range(sym.duration1, SHARP_BIT_ONE_SPACE)) {
            data |= ((uint64_t)1 << i);
        } else if (funciones_sharp_check_range(sym.duration1, SHARP_BIT_ZERO_SPACE)) {
            // Es un 0, data ya tiene ese bit en 0 por defecto
        } else {
            // Timing de bit inválido (ni 1 ni 0)
            return false; 
        }
    }

    // 4. Mapeo de datos para tu Receiver Sharp
    // Basado en tus logs: 0xF483 y 0xB04F
    *addr = (uint16_t)(data & 0xFFFF);           // Primeros 16 bits
    *cmd  = (uint16_t)((data >> 16) & 0xFFFF);    // Siguientes 16 bits
    
    return true;
}

// ==========================================
// 4. LÓGICA DE CAPTURA Y ENVÍO IR
// ==========================================

void funciones_enviar_ir_sharp_48(uint16_t addr, uint16_t cmd) {
    if (ir_core.tx_chan == NULL) return;

    // 1. Construcción del dato (32 bits útiles + 16 bits de relleno/checksum)
    uint64_t data = ((uint64_t)cmd << 16) | addr;
    
    // 2. Buffer de símbolos (1 lead + 48 bits + 1 stop = 50)
    rmt_symbol_word_t symbols[50];
    int s = 0;

    // --- Leading Code ---
    symbols[s++] = (rmt_symbol_word_t) {
        .duration0 = SHARP_H_LEAD_0,
        .level0 = 1,
        .duration1 = SHARP_H_LEAD_1,
        .level1 = 0
    };

    // --- 48 Bits Payload ---
    for (int i = 0; i < 48; i++) {
        uint32_t space_bit = (data & ((uint64_t)1 << i)) ? SHARP_BIT_ONE_SPACE : SHARP_BIT_ZERO_SPACE;
        
        symbols[s++] = (rmt_symbol_word_t) {
            .duration0 = SHARP_BIT_MARK,
            .level0 = 1,
            .duration1 = space_bit,
            .level1 = 0
        };
    }

    // --- Stop Bit ---
    symbols[s++] = (rmt_symbol_word_t) {
        .duration0 = SHARP_BIT_MARK,
        .level0 = 1,
        .duration1 = 3000, 
        .level1 = 0
    };

    // 3. Transmisión
    rmt_transmit_config_t tx_conf = { .loop_count = 0 };
    ESP_LOGI(TAG, "Enviando Sharp 48-bit: Addr:0x%04X Cmd:0x%04X", addr, cmd);
    
    // El segundo parámetro (encoder) es NULL porque pasamos símbolos "raw"
    ESP_ERROR_CHECK(rmt_transmit(ir_core.tx_chan, NULL, symbols, s, &tx_conf));
}


void funciones_enviar_ir(uint16_t addr, uint16_t cmd) {
    if (ir_core.tx_chan == NULL || ir_core.encoder == NULL) return;
    ir_nec_scan_code_t scan_code = { .address = addr, .command = cmd };
    rmt_transmit_config_t transmit_config = { .loop_count = 0 };
    ESP_LOGI(TAG, "Enviando IR (NEC) -> Addr: 0x%04X, Cmd: 0x%04X", addr, cmd);
    ESP_ERROR_CHECK(rmt_transmit(ir_core.tx_chan, ir_core.encoder, &scan_code, sizeof(scan_code), &transmit_config));
    vTaskDelay(pdMS_TO_TICKS(100)); 
}

esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t raw_symbols[64];
    rmt_rx_done_event_data_t rx_data;
    rmt_receive_config_t receive_cfg = { 
        .signal_range_min_ns = 1250, 
        .signal_range_max_ns = 12000000 
    };

    // Limpiar hardware antes de esperar
    rmt_disable(ir_core.rx_chan); 
    rmt_enable(ir_core.rx_chan); 
    xQueueReset(ir_core.rx_queue);
    
    // Iniciar recepción
    if (rmt_receive(ir_core.rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg) != ESP_OK) {
        return ESP_FAIL;
    }

    // Esperar a que la cola reciba el evento de "RX finalizado"
    if (xQueueReceive(ir_core.rx_queue, &rx_data, pdMS_TO_TICKS(10000)) == pdPASS) {
        
        // --- BLOQUE DE DEBUG CORREGIDO (Solo se ejecuta si hay símbolos reales) ---
        if (rx_data.num_symbols > 0) {
            ESP_LOGI(TAG, "--- DEBUG SÍMBOLOS (%d) ---", rx_data.num_symbols);
            for (int i = 0; i < rx_data.num_symbols; i++) {
                printf("%d,%d | ", (int)rx_data.received_symbols[i].duration0, 
                                   (int)rx_data.received_symbols[i].duration1);
                if ((i + 1) % 4 == 0) printf("\n");
            }
            printf("\n---------------------------\n");
        }

        // Intento de parseo NEC
        if (rx_data.num_symbols >= 33 && funciones_nec_parse_frame(rx_data.received_symbols, addr, cmd)) {
            ESP_LOGI(TAG, "Protocolo NEC detectado");
            return ESP_OK;
        }
        
        // Intento de parseo SHARP (48 bits = 50 símbolos)
        if (rx_data.num_symbols >= 49) { 
            if (funciones_sharp_parse_frame(rx_data.received_symbols, addr, cmd)) {
                ESP_LOGI(TAG, "Protocolo SHARP 48-bit detectado");
                return ESP_OK;
            }
        }
        
        ESP_LOGW(TAG, "Trama no reconocida (%d símbolos).", rx_data.num_symbols);
    } else {
        ESP_LOGW(TAG, "Timeout: No se recibió señal IR.");
    }
    
    return ESP_FAIL; 
}

// ==========================================
// 5. GESTIÓN DE BASE DE DATOS (SPIFFS / RAM)
// ==========================================

esp_err_t funciones_db_init(void) {
    if (db_cache) cJSON_Delete(db_cache);
    FILE* f = fopen("/spiffs/db_ir.json", "r");
    if (f == NULL) {
        db_cache = cJSON_CreateObject();
        cJSON_AddObjectToObject(db_cache, "CRemotos");
        return ESP_OK; 
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    fread(data, 1, fsize, f);
    fclose(f);
    data[fsize] = 0;
    db_cache = cJSON_Parse(data);
    free(data);
    if (db_cache == NULL) {
        db_cache = cJSON_CreateObject();
        cJSON_AddObjectToObject(db_cache, "CRemotos");
    }
    ESP_LOGI(TAG, "Base de datos cargada.");
    return ESP_OK;
}

esp_err_t funciones_db_guardar_a_spiffs(void) {
    char *json_string = cJSON_PrintUnformatted(db_cache);
    if (!json_string) return ESP_FAIL;
    FILE *f = fopen("/spiffs/db_ir.json", "w");
    if (f == NULL) { free(json_string); return ESP_FAIL; }
    fprintf(f, "%s", json_string);
    fclose(f);
    free(json_string);
    return ESP_OK;
}

void funciones_db_reiniciar(void) {
    if (!db_cache) db_cache = cJSON_CreateObject();
    cJSON_DeleteItemFromObject(db_cache, "CRemotos");
    cJSON_AddObjectToObject(db_cache, "CRemotos");
    funciones_db_guardar_a_spiffs();
}

esp_err_t funciones_db_fusionar_dispositivo(const char* id_dispositivo, cJSON* json_dispositivo_nuevo) {
    if (!db_cache || !json_dispositivo_nuevo) return ESP_FAIL;
    cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
    if (!cremotos) cremotos = cJSON_AddObjectToObject(db_cache, "CRemotos");
    if (cJSON_HasObjectItem(cremotos, id_dispositivo)) cJSON_DeleteItemFromObject(cremotos, id_dispositivo);
    cJSON_AddItemToObject(cremotos, id_dispositivo, cJSON_Duplicate(json_dispositivo_nuevo, true));
    return funciones_db_guardar_a_spiffs();
}

bool funciones_db_existe_dispositivo(const char* id_dispositivo) {
    if (!db_cache) return false;
    return cJSON_HasObjectItem(cJSON_GetObjectItem(db_cache, "CRemotos"), id_dispositivo);
}

// ==========================================
// 6. LÓGICA DE NEGOCIO (MQTT / APP)
// ==========================================

resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data, int len) {
    resultado_busqueda_ir_t res = {0, 0, false};
    cJSON *root = cJSON_ParseWithLength(json_data, len);
    if (!root) return res;
    cJSON *disp_node = cJSON_GetObjectItem(root, "id");
    if (!disp_node) disp_node = cJSON_GetObjectItem(root, "dispositivo");
    cJSON *cmd_node = cJSON_GetObjectItem(root, "c");
    if (!cmd_node) cmd_node = cJSON_GetObjectItem(root, "boton");

    if (cJSON_IsString(disp_node) && cJSON_IsString(cmd_node)) {
        cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
        cJSON *dispositivo = cJSON_GetObjectItem(cremotos, disp_node->valuestring);
        if (dispositivo) {
            cJSON *codigo_hex = cJSON_GetObjectItem(dispositivo, cmd_node->valuestring);
            if (cJSON_IsString(codigo_hex)) {
                uint32_t raw = strtoul(codigo_hex->valuestring, NULL, 16);
                res.address = (uint16_t)(raw >> 16);
                res.command = (uint16_t)(raw & 0xFFFF);
                res.encontrado = true;
            }
        }
    }
    cJSON_Delete(root);
    return res;
}

// ==========================================
// 6. LÓGICA DE NEGOCIO (MQTT / APP) CORREGIDA
// ==========================================

esp_err_t funciones_ejecutar_comando_desde_json(const char *json_data) {
    // 1. Parsear el JSON para ver qué dispositivo es
    cJSON *root = cJSON_Parse(json_data);
    if (!root) return ESP_FAIL;

    // Intentamos obtener el ID del dispositivo (puede venir como "id" o "dispositivo")
    cJSON *disp_node = cJSON_GetObjectItem(root, "id");
    if (!disp_node) disp_node = cJSON_GetObjectItem(root, "dispositivo");

    // 2. Buscar el código en la base de datos local
    resultado_busqueda_ir_t res = funciones_procesar_control_mqtt(json_data, strlen(json_data));

    if (res.encontrado && disp_node && cJSON_IsString(disp_node)) {
        // 3. Decidir qué protocolo usar
        if (strcmp(disp_node->valuestring, "Receiver_Sharp_B530") == 0) {
            // Usamos la nueva función para Sharp de 48 bits
            funciones_enviar_ir_sharp_48(res.address, res.command);
        } else {
            // El protocolo NEC estándar que ya tenías
            funciones_enviar_ir(res.address, res.command);
        }
        cJSON_Delete(root);
        return ESP_OK;
    }

    cJSON_Delete(root);
    ESP_LOGW(TAG, "Comando no ejecutado: Dispositivo no encontrado o JSON inválido.");
    return ESP_FAIL;
}

void funciones_set_contexto_aprendizaje(const char* data, int len) {
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root) {
        cJSON *d = cJSON_GetObjectItem(root, "dispositivo");
        cJSON *b = cJSON_GetObjectItem(root, "boton");
        if (d && b) {
            strncpy(ctx_learn.dispositivo, d->valuestring, sizeof(ctx_learn.dispositivo) - 1);
            strncpy(ctx_learn.boton, b->valuestring, sizeof(ctx_learn.boton) - 1);
            listo_para_capturar = true;
            ESP_LOGI(TAG, "Aprendizaje: %s -> %s", ctx_learn.dispositivo, ctx_learn.boton);
        }
        cJSON_Delete(root);
    }
}

void funciones_limpiar_cola_comandos(QueueHandle_t cola) {
    if (cola == NULL) return;
    char *tmp;
    while (xQueueReceive(cola, &tmp, 0) == pdPASS) free(tmp);
}

// ==========================================
// 7. TAREAS FREERTOS
// ==========================================

void tarea_modo_remoto(void *pvParameters) {
    extern QueueHandle_t cola_control_ir; 
    char *json_ptr = NULL; 
    while (1) {
        if (xQueueReceive(cola_control_ir, &json_ptr, portMAX_DELAY) == pdPASS) {
            funciones_ejecutar_comando_desde_json(json_ptr);
            free(json_ptr); 
        }
    }
}

void tarea_modo_aprendizaje(void *pvParameters) {
    while (1) {
        if (!listo_para_capturar) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        uint16_t addr = 0, cmd = 0;
        if (funciones_esperar_y_parsear_ir(&addr, &cmd) == ESP_OK) {
            if (con_firebase_patch_comando(ctx_learn.dispositivo, ctx_learn.boton, addr, cmd) == ESP_OK) {
                mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"status\":\"aprendido_ok\"}");
                listo_para_capturar = false; 
            } else {
                mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"status\":\"ERROR_CLOUD\"}");
            }
        }
    }
}

void tarea_sincronizacion_inicial(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (g_config.mqtt_id[0] == '\0') { vTaskDelete(NULL); return; }
    funciones_db_reiniciar();
    char path_config[128];
    snprintf(path_config, sizeof(path_config), "Nodos_Config/%s", g_config.mqtt_id);
    char *json_lista = con_firebase_get_json(path_config);
    if (json_lista) {
        cJSON *lista = cJSON_Parse(json_lista);
        if (cJSON_IsArray(lista)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, lista) {
                if (item->valuestring && !funciones_db_existe_dispositivo(item->valuestring)) {
                    char path_disp[128];
                    snprintf(path_disp, sizeof(path_disp), "CRemotos/%s", item->valuestring);
                    char *json_disp = con_firebase_get_json(path_disp);
                    if (json_disp) {
                        cJSON *obj_disp = cJSON_Parse(json_disp);
                        if (obj_disp) { funciones_db_fusionar_dispositivo(item->valuestring, obj_disp); cJSON_Delete(obj_disp); }
                        free(json_disp);
                    }
                }
            }
        }
        cJSON_Delete(lista); free(json_lista);
    }
    ESP_LOGI(TAG, "Sincronización finalizada.");
    vTaskDelete(NULL);
}