#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "funciones.h"
#include "config.h"
#include "esp_log.h"
#include "ir_tx.h" 
#include "ir_rx.h"
#include "con_firebase.h"
#include "con_mqtt.h"
#include "driver/rmt_tx.h" // Necesario para rmt_transmit
#include "driver/rmt_rx.h" // Necesario para rmt_receive

static const char *TAG = "FUNC_CORE";

// --- Variables estáticas ---
static ir_core_t ir_core = {0};
static cJSON *db_cache = NULL;
static context_aprendizaje_t ctx_learn = {0};
static bool listo_para_capturar = false;

// g_config ya está definida en main.c, aquí solo la referenciamos
extern app_config_t g_config;
// La cola viene de main.c
extern QueueHandle_t cola_control_ir;

// --- UTILIDADES ---
void utils_trim(char *dest, const char *src, size_t dest_size) {
    if (!src || !dest) return;
    const char *start = src;
    while (*start == ' ') start++;
    
    strncpy(dest, start, dest_size - 1);
    dest[dest_size - 1] = '\0';
    
    char *back = dest + strlen(dest) - 1;
    while (back >= dest && (*back == ' ' || *back == '\n' || *back == '\r')) { 
        *back = '\0'; 
        back--; 
    }
}

esp_err_t funciones_hardware_init(void) {
    // Inicializamos TX y RX pasando las referencias de nuestra estructura estática
    ESP_ERROR_CHECK(ir_tx_init(&ir_core.tx_chan, &ir_core.encoder));
    ESP_ERROR_CHECK(ir_rx_init(&ir_core.rx_chan, &ir_core.rx_queue));
    
    ESP_ERROR_CHECK(rmt_enable(ir_core.tx_chan));
    ESP_ERROR_CHECK(rmt_enable(ir_core.rx_chan));
    
    ESP_LOGI(TAG, "Hardware IR listo (TX Pin %d, RX Pin %d).", IR_TX_GPIO_NUM, IR_RX_GPIO_NUM);
    return ESP_OK;
}

// --- PROTOCOLOS ---
bool funciones_check_in_range(uint32_t signal, uint32_t spec) {
    return (signal < (spec + IR_NEC_DECODE_MARGIN)) && (signal > (spec - IR_NEC_DECODE_MARGIN));
}

bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t *cur = rmt_symbols;
    uint16_t address = 0, command = 0;
    
    // Header NEC: 9ms low, 4.5ms high
    if (!funciones_check_in_range(cur->duration0, 9000) || !funciones_check_in_range(cur->duration1, 4500)) return false;
    cur++;
    
    for (int i = 0; i < 16; i++, cur++) { 
        if (funciones_check_in_range(cur->duration1, 1690)) address |= (1 << i); 
    }
    for (int i = 0; i < 16; i++, cur++) { 
        if (funciones_check_in_range(cur->duration1, 1690)) command |= (1 << i); 
    }
    *addr = address; *cmd = command;
    return true;
}

bool funciones_sharp_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    if (!funciones_check_in_range(rmt_symbols[0].duration0, 3260)) return false;
    uint64_t data = 0;
    for (int i = 0; i < 48; i++) {
        if (funciones_check_in_range(rmt_symbols[i + 1].duration1, 1375)) data |= ((uint64_t)1 << i);
    }
    *addr = (uint16_t)(data & 0xFFFF);
    *cmd  = (uint16_t)((data >> 16) & 0xFFFF);
    return true;
}

// --- ENVÍO ---
void funciones_enviar_ir_sharp_48(uint16_t addr, uint16_t cmd) {
    uint64_t data = ((uint64_t)cmd << 16) | addr;
    rmt_symbol_word_t symbols[51];
    int s = 0;

    // Inicialización usando .val para evitar errores de unión en GCC 14+
    symbols[s++].val = (((uint32_t)3260 << 16) | ((uint32_t)1 << 15) | (1780 & 0x7FFF)); 
    
    for (int i = 0; i < 48; i++) {
        uint32_t spc = (data & ((uint64_t)1 << i)) ? 1375 : 535;
        symbols[s++].val = (((uint32_t)305 << 16) | ((uint32_t)1 << 15) | (spc & 0x7FFF));
    }
    symbols[s++].val = (((uint32_t)305 << 16) | ((uint32_t)1 << 15) | (3000 & 0x7FFF));

    rmt_transmit_config_t tx_conf = {.loop_count = 0};
    rmt_transmit(ir_core.tx_chan, NULL, symbols, s, &tx_conf);
}

void funciones_enviar_ir(uint16_t addr, uint16_t cmd) {
    ir_nec_scan_code_t scan_code = {.address = addr, .command = cmd};
    rmt_transmit_config_t tx_conf = {.loop_count = 0};
    rmt_transmit(ir_core.tx_chan, ir_core.encoder, &scan_code, sizeof(scan_code), &tx_conf);
}

esp_err_t funciones_esperar_y_parsear_ir(uint16_t *addr, uint16_t *cmd, char *norma_out) {
    rmt_rx_done_event_data_t rx_data;
    
    // 1. PREPARAR EL BUFFER (Símbolos)
    // El RMT v5 necesita un lugar donde escribir los pulsos antes de enviarlos a la cola
    rmt_symbol_word_t raw_symbols[64]; 
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250, // Filtro mínimo para ignorar ruido muy rápido
        .signal_range_max_ns = 12000000, // 12ms máximo de pulso
    };

    // 2. ACTIVAR LA RECEPCIÓN ( Rearmar el hardware )
    // Esto es lo que faltaba: Sin esto, el pin 19 está "muerto"
    ESP_ERROR_CHECK(rmt_receive(ir_core.rx_chan, raw_symbols, sizeof(raw_symbols), &receive_config));

    ESP_LOGW(TAG, "Hardware rearmado. Esperando rayo IR...");

    // 3. ESPERAR EN LA COLA
    if (xQueueReceive(ir_core.rx_queue, &rx_data, pdMS_TO_TICKS(10000)) == pdPASS) {
        ESP_LOGI(TAG, "¡Evento de recepción detectado! Símbolos: %d", rx_data.num_symbols);

        if (rx_data.num_symbols >= 33 && funciones_nec_parse_frame((rmt_symbol_word_t*)rx_data.received_symbols, addr, cmd)) {
            strcpy(norma_out, "NEC"); return ESP_OK;
        }
        // ... (resto de tus validaciones Sharp)
    }

    return ESP_FAIL;
}

// --- DB LOCAL ---
esp_err_t funciones_db_init(void) {
    FILE* f = fopen("/spiffs/db_ir.json", "r");
    if (f == NULL) {
        db_cache = cJSON_CreateObject();
        cJSON *crem = cJSON_CreateObject();
        cJSON_AddItemToObject(db_cache, "CRemotos", crem);
        return ESP_OK; 
    }
    fseek(f, 0, SEEK_END); long fsize = ftell(f); fseek(f, 0, SEEK_SET);
    char *data = malloc(fsize + 1);
    fread(data, 1, fsize, f); fclose(f);
    data[fsize] = 0;
    db_cache = cJSON_Parse(data);
    free(data);
    return ESP_OK;
}

resultado_busqueda_ir_t funciones_procesar_control_mqtt(const char *json_data) {
    resultado_busqueda_ir_t res = {0};
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Error: No se pudo parsear el JSON de la cola.");
        return res;
    }

    cJSON *disp_n = cJSON_GetObjectItem(root, KEY_MQTT_DISP);
    cJSON *btn_n  = cJSON_GetObjectItem(root, KEY_MQTT_BTN);

    if (cJSON_IsString(disp_n) && cJSON_IsString(btn_n)) {
        ESP_LOGI(TAG, "Buscando en DB -> Disp: %s, Boton: %s", disp_n->valuestring, btn_n->valuestring);

        cJSON *cremotos = cJSON_GetObjectItem(db_cache, FIREBASE_PATH_REMOTOS);
        if (!cremotos) {
            ESP_LOGE(TAG, "Error: db_cache no tiene el nodo '%s'. ¿Sync falló?", FIREBASE_PATH_REMOTOS);
            cJSON_Delete(root);
            return res;
        }

        cJSON *dispositivo = cJSON_GetObjectItem(cremotos, disp_n->valuestring);
        if (dispositivo) {
            cJSON *norma_node = cJSON_GetObjectItem(dispositivo, KEY_DB_NORMA);
            strcpy(res.norma, cJSON_IsString(norma_node) ? norma_node->valuestring : VAL_NORMA_NEC);
            
            cJSON *code_node = cJSON_GetObjectItem(dispositivo, btn_n->valuestring);
            if (cJSON_IsString(code_node)) {
                uint64_t raw = strtoull(code_node->valuestring, NULL, 16);
                ESP_LOGI(TAG, "Código encontrado: %s (Norma: %s)", code_node->valuestring, res.norma);

                if (strcmp(res.norma, "SHARP48") == 0) {
                    res.address = (uint16_t)(raw & 0xFFFF);
                    res.command = (uint16_t)((raw >> 16) & 0xFFFF);
                } else {
                    res.address = (uint16_t)(raw >> 16);
                    res.command = (uint16_t)(raw & 0xFFFF);
                }
                res.encontrado = true;
            } else {
                ESP_LOGW(TAG, "El botón '%s' no existe para el dispositivo '%s'.", btn_n->valuestring, disp_n->valuestring);
            }
        } else {
            ESP_LOGW(TAG, "Dispositivo '%s' no encontrado en la base de datos.", disp_n->valuestring);
        }
    }
    cJSON_Delete(root);
    return res;
}

void funciones_ejecutar_comando_desde_json(const char *json_data) {
    resultado_busqueda_ir_t res = funciones_procesar_control_mqtt(json_data);
    if (res.encontrado) {
        ESP_LOGW(TAG, "DISPARANDO LED IR -> Addr: 0x%04X, Cmd: 0x%04X", res.address, res.command);
        
        if (strcmp(res.norma, "SHARP48") == 0) {
            funciones_enviar_ir_sharp_48(res.address, res.command);
        } else {
            funciones_enviar_ir(res.address, res.command);
        }

        // --- NOTIFICACIÓN DE EVENTO ---
        char msg[128];
        snprintf(msg, sizeof(msg), "{\"event\":\"IR_SENT\", \"norma\":\"%s\", \"addr\":\"0x%04X\"}", 
                 res.norma, res.address);
        mqtt_enviar_raw(MQTT_TOPIC_EVENTO, msg);
    } else {
        // Opcional: Notificar error si el botón no existe
        mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"IR_ERROR\", \"info\":\"Not found in DB\"}");
    }
}

void funciones_set_contexto_aprendizaje(cJSON *data_node) {
    cJSON *disp = cJSON_GetObjectItem(data_node, "disp");
    cJSON *btn = cJSON_GetObjectItem(data_node, "btn");
    if (cJSON_IsString(disp) && cJSON_IsString(btn)) {
        utils_trim(ctx_learn.dispositivo, disp->valuestring, sizeof(ctx_learn.dispositivo));
        utils_trim(ctx_learn.boton, btn->valuestring, sizeof(ctx_learn.boton));
        listo_para_capturar = true;
    }
}

void tarea_modo_remoto(void *pvParameters) {
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
        if (!listo_para_capturar) { 
            vTaskDelay(pdMS_TO_TICKS(500)); 
            continue; 
        }

        ESP_LOGW(TAG, ">>> ESPERANDO SEÑAL IR para [%s] - [%s]...", ctx_learn.dispositivo, ctx_learn.boton);
        
        uint16_t addr = 0, cmd = 0;
        char norma[20] = {0};

        // Esta función bloquea hasta 10 segundos esperando el rayo IR
        if (funciones_esperar_y_parsear_ir(&addr, &cmd, norma) == ESP_OK) {
            ESP_LOGI(TAG, "¡Captura Exitosa! Norma: %s, Addr: 0x%04X, Cmd: 0x%04X", norma, addr, cmd);
            
            // Subimos a Firebase
            if (con_firebase_patch_comando(ctx_learn.dispositivo, ctx_learn.boton, addr, cmd, norma) == ESP_OK) {
                ESP_LOGI(TAG, "Datos guardados en Firebase correctamente.");
                
                // Notificamos a la App
                char msg[150];
                snprintf(msg, sizeof(msg), "{\"event\":\"LEARNED_OK\", \"disp\":\"%s\", \"btn\":\"%s\"}", 
                         ctx_learn.dispositivo, ctx_learn.boton);
                mqtt_enviar_raw(MQTT_TOPIC_EVENTO, msg);
                
                listo_para_capturar = false; // Cerramos el ciclo de captura
                
                // Opcional: Volver a modo remoto automáticamente
                // cambiar_modo_trabajo(MODO_REMOTO_CONTROL);
            }
        } else {
            ESP_LOGE(TAG, "Timeout: No se detectó ninguna señal IR.");
            mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"LEARN_TIMEOUT\"}");
            listo_para_capturar = false; // <--- IMPORTANTE: Detener el aprendizaje
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// --- GESTIÓN DE COLAS ---

/**
 * @brief Limpia la cola de control IR liberando la memoria de los strings pendientes.
 * Es fundamental llamar a esto antes de cambiar de modo para evitar 
 * disparos accidentales de comandos antiguos.
 */
void funciones_limpiar_cola_comandos(QueueHandle_t cola) {
    if (cola == NULL) {
        return;
    }

    char *json_ptr = NULL;
    int contador = 0;

    // Intentamos recibir todos los elementos de la cola sin bloquear (timeout 0)
    while (xQueueReceive(cola, &json_ptr, 0) == pdPASS) {
        if (json_ptr != NULL) {
            free(json_ptr); // Liberamos la memoria del string que reservó el driver MQTT
            contador++;
        }
    }

    if (contador > 0) {
        ESP_LOGI(TAG, "Cola control_ir limpiada. Se eliminaron %d mensajes pendientes.", contador);
    } else {
        ESP_LOGD(TAG, "La cola ya estaba vacía.");
    }
}

// --- GESTIÓN DE LOGS ---
void inicializar_niveles_log(void) {
    // Esto limpia la terminal de mensajes basura de WiFi y MQTT 
    // y deja solo lo importante para el proyecto
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_WARN);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_WARN);
    esp_log_level_set("esp-tls", ESP_LOG_WARN);
    ESP_LOGI(TAG, "Niveles de log configurados.");
}

// --- TAREA DE SINCRONIZACIÓN ---
void tarea_sincronizacion_inicial(void *pvParameters) {
    ESP_LOGI(TAG, "Iniciando sincronización con Firebase...");
    mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"SYNC_START\"}");
    
    // 1. Obtenemos el STRING (texto) desde Firebase
    char *json_string = con_firebase_get_json(FIREBASE_PATH_REMOTOS);
    
    if (json_string != NULL) {
        // 2. Parseamos el texto para convertirlo en objeto cJSON
        cJSON *nuevo_cache_data = cJSON_Parse(json_string);
        
        // Liberamos el string original ya que cJSON_Parse crea su propia copia en memoria
        free(json_string); 

        if (nuevo_cache_data != NULL) {
            if (db_cache != NULL) {
                cJSON_Delete(db_cache);
            }
            
            // 3. Reconstruimos la estructura: { "CRemotos": { ...datos... } }
            db_cache = cJSON_CreateObject();
            cJSON_AddItemToObject(db_cache, FIREBASE_PATH_REMOTOS, nuevo_cache_data);
            
            ESP_LOGI(TAG, "Sincronización EXITOSA. Memoria actualizada.");
            mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"SYNC_OK\"}");
        } else {
            ESP_LOGE(TAG, "Error: El texto recibido no es un JSON válido.");
            mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"event\":\"SYNC_ERROR\", \"info\":\"JSON Parse Fail\"}");
        }
    } else {
        ESP_LOGE(TAG, "ERROR: No se pudo obtener datos de Firebase (json_string es NULL).");
    }

    ESP_LOGI(TAG, "Sincronización finalizada. Tarea terminada.");
    vTaskDelete(NULL); 
}
