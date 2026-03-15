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

    FILE* f = fopen("/spiffs/db_ir.json", "r");
    // Si el archivo no existe (ESP32 virgen), creamos una DB vacía en RAM
    if (f == NULL) {
        ESP_LOGW(TAG, "Archivo DB no encontrado. Creando base de datos vacia en RAM...");
        db_cache = cJSON_CreateObject();
        cJSON_AddObjectToObject(db_cache, "CRemotos");
        return ESP_OK; 
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    if (!data) return ESP_ERR_NO_MEM; // Buena práctica: check de memoria

    fread(data, 1, fsize, f);
    fclose(f);
    data[fsize] = 0;

    db_cache = cJSON_Parse(data);
    free(data);

    if (db_cache == NULL) {
        ESP_LOGE(TAG, "Error al parsear JSON inicial. Creando objeto vacio.");
        db_cache = cJSON_CreateObject();
        cJSON_AddObjectToObject(db_cache, "CRemotos");
    }

    ESP_LOGI(TAG, "Base de datos cargada en RAM exitosamente");
    return ESP_OK;
}


esp_err_t funciones_db_fusionar_dispositivo(const char* id_dispositivo, cJSON* json_dispositivo_nuevo) {
    if (!db_cache || !json_dispositivo_nuevo) return ESP_FAIL;

    // 1. Buscamos el nodo raíz "CRemotos"
    cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
    if (!cremotos) {
        cremotos = cJSON_AddObjectToObject(db_cache, "CRemotos");
    }

    // 2. Si el dispositivo ya existe localmente, lo eliminamos para actualizarlo
    if (cJSON_HasObjectItem(cremotos, id_dispositivo)) {
        ESP_LOGI("FUNC_CORE", "Actualizando dispositivo existente: %s", id_dispositivo);
        cJSON_DeleteItemFromObject(cremotos, id_dispositivo);
    } else {
        ESP_LOGI("FUNC_CORE", "Agregando nuevo dispositivo: %s", id_dispositivo);
    }

    // 3. Duplicamos el objeto descargado para que pertenezca a nuestro árbol de memoria
    cJSON *copia_dispositivo = cJSON_Duplicate(json_dispositivo_nuevo, true);
    cJSON_AddItemToObject(cremotos, id_dispositivo, copia_dispositivo);

    // 4. Persistencia inmediata en SPIFFS
    return funciones_db_guardar_a_spiffs();
}


bool funciones_db_existe_dispositivo(const char* id_dispositivo) {
    if (!db_cache) return false;
    cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
    return cJSON_HasObjectItem(cremotos, id_dispositivo);
}


esp_err_t funciones_db_guardar_a_spiffs(void) {
    // Usamos PrintUnformatted para ahorrar espacio en el archivo
    char *json_string = cJSON_PrintUnformatted(db_cache);
    if (!json_string) return ESP_FAIL;

    FILE *f = fopen("/spiffs/db_ir.json", "w");
    if (f == NULL) {
        ESP_LOGE("FUNC_CORE", "Error al abrir db_ir.json para escritura");
        free(json_string);
        return ESP_FAIL;
    }

    fprintf(f, "%s", json_string);
    fclose(f);
    free(json_string);
    
    ESP_LOGI("FUNC_CORE", "SPIFFS sincronizado (db_ir.json actualizado).");
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
    if (!db_cache) return res;

    cJSON *root = cJSON_ParseWithLength(json_data, len);
    if (!root) return res;

    // --- LÓGICA DE ETIQUETAS FLEXIBLES ---
    // Intentamos obtener el ID del dispositivo (puede venir como "id" o como "dispositivo")
    cJSON *disp_node = cJSON_GetObjectItem(root, "id");
    if (!disp_node) disp_node = cJSON_GetObjectItem(root, "dispositivo");

    // Intentamos obtener el comando (puede venir como "c" o como "boton")
    cJSON *cmd_node = cJSON_GetObjectItem(root, "c");
    if (!cmd_node) cmd_node = cJSON_GetObjectItem(root, "boton");

    if (cJSON_IsString(disp_node) && cJSON_IsString(cmd_node)) {
        // Accedemos a la base de datos cargada en RAM
        cJSON *cremotos = cJSON_GetObjectItem(db_cache, "CRemotos");
        if (cremotos) {
            cJSON *dispositivo = cJSON_GetObjectItem(cremotos, disp_node->valuestring);
            if (dispositivo) {
                cJSON *codigo_hex = cJSON_GetObjectItem(dispositivo, cmd_node->valuestring);
                if (cJSON_IsString(codigo_hex)) {
                    // Convertimos el string "0x7C877F80" a valores numéricos
                    uint32_t raw = strtoul(codigo_hex->valuestring, NULL, 16);
                    res.address = (uint16_t)(raw >> 16);
                    res.command = (uint16_t)(raw & 0xFFFF);
                    res.encontrado = true;
                    
                    ESP_LOGI("FUNC_CORE", "Busqueda exitosa: %s -> %s (0x%04X 0x%04X)", 
                             disp_node->valuestring, cmd_node->valuestring, res.address, res.command);
                }
            }
        }
    }

    if (!res.encontrado) {
        ESP_LOGW("FUNC_CORE", "No se encontro el comando en la base de datos");
    }

    cJSON_Delete(root);
    return res;
}


/**
 * @brief Envío directo usando el handle interno
 */
void funciones_enviar_ir(uint16_t addr, uint16_t cmd) {
    if (ir_core.tx_chan == NULL || ir_core.encoder == NULL) {
        ESP_LOGE("FUNC_CORE", "TX no inicializado");
        return;
    }

    ir_nec_scan_code_t scan_code = {
        .address = addr,
        .command = cmd,
    };

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };

    ESP_LOGI("FUNC_CORE", "Enviando IR -> Addr: 0x%04X, Cmd: 0x%04X", addr, cmd);

    // Transmitimos
    ESP_ERROR_CHECK(rmt_transmit(ir_core.tx_chan, ir_core.encoder, &scan_code, sizeof(scan_code), &transmit_config));

    // En lugar de wait_all_done (que te da el error de 60ms), 
    // usamos un pequeño delay de seguridad para que el hardware respire.
    // Esto es mucho más amigable con tus tareas de MQTT.
    vTaskDelay(pdMS_TO_TICKS(100)); 
    
    ESP_LOGI("FUNC_CORE", "Transmisión finalizada");
}


void inicializar_niveles_log(void) {
    // 1. Silenciamos el "ruido" del sistema (Drivers internos)
    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_NONE); // Esto quita los logs de red
    esp_log_level_set("nvs", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);

    // 2. Activamos TUS componentes (Nivel INFO para ver qué pasa)
    esp_log_level_set("WIFI_CONN", ESP_LOG_INFO);  // Tu componente de WiFi
    esp_log_level_set("MQTT_CORE", ESP_LOG_INFO);  // Tu componente MQTT
    esp_log_level_set("FUNC_CORE", ESP_LOG_INFO);  // Gestión de DB y Hardware IR
    esp_log_level_set("IR_RX", ESP_LOG_INFO);      // Driver RX
    esp_log_level_set("MAIN_APP", ESP_LOG_INFO);   // Lógica principal
    esp_log_level_set("DIAG", ESP_LOG_INFO);       // El diagnóstico de símbolos de ayer

    ESP_LOGI("FUNC_CORE", "Niveles de LOG configurados (Terminal limpia).");
}


