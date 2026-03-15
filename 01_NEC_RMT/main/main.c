#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "con_wifi.h"
#include "con_mqtt.h"
#include "esp_log.h"
#include "funciones.h" // Aquí reside ahora toda la lógica IR
#include "config.h"
#include "esp_spiffs.h"
#include "cJSON.h"
#include "con_firebase.h"
#include "driver/rmt_tx.h" // Para rmt_enable de transmisión
#include "driver/rmt_rx.h" // Para rmt_enable de recepción
#include <dirent.h> // Necesario para leer directorios


static const char *TAG = "MAIN_APP";
static TaskHandle_t xTareaIR = NULL;
QueueHandle_t cola_control_ir = NULL;
// Variable global para saber qué estamos aprendiendo
static context_aprendizaje_t ctx_learn = {0};
static bool listo_para_capturar = false;


// --- PROTOTIPOS ---
void tarea_modo_aprendizaje(void *pvParameters);
void tarea_modo_remoto(void *pvParameters);
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo);
void tarea_sincronizacion_inicial(void *pvParameters);

esp_err_t init_spiffs(void);

// --- GESTIÓN DE MODOS ---
void cambiar_modo_trabajo(modo_sistema_t nuevo_modo) {
    if (xTareaIR != NULL) {
        ESP_LOGI(TAG, "Cambiando modo. Deteniendo tarea actual...");
        vTaskDelete(xTareaIR);
        xTareaIR = NULL;
    }

    if (nuevo_modo == MODO_REMOTO_CONTROL) {
        // Limpiar cola de comandos pendientes antes de iniciar
        char *tmp;
        while (cola_control_ir && xQueueReceive(cola_control_ir, &tmp, 0) == pdPASS) {
            free(tmp);
        }
        xTaskCreate(tarea_modo_remoto, "task_remote", 4096, NULL, 5, &xTareaIR);
        ESP_LOGI(TAG, "Modo CONTROL REMOTO activo");
    } else {
        xTaskCreate(tarea_modo_aprendizaje, "task_learn", 4096, NULL, 5, &xTareaIR);
        ESP_LOGI(TAG, "Modo APRENDIZAJE activo");
    }
}

// --- TAREA: EJECUCIÓN DE COMANDOS ---
void tarea_modo_remoto(void *pvParameters) {
    char *json_ptr; 
    while (1) {
        if (xQueueReceive(cola_control_ir, &json_ptr, portMAX_DELAY) == pdPASS) {
            // Búsqueda en caché RAM (Capa de funciones)
            resultado_busqueda_ir_t res = funciones_procesar_control_mqtt(json_ptr, strlen(json_ptr));

            if (res.encontrado) {
                funciones_enviar_ir(res.address, res.command);
                ESP_LOGI(TAG, "IR Enviado: Addr 0x%04X, Cmd 0x%04X", res.address, res.command);
            } else {
                ESP_LOGW(TAG, "Comando no reconocido en JSON recibido");
            }
            free(json_ptr); // Liberar memoria asignada en con_mqtt.c
        }
    }
}




// --- TAREA: APRENDIZAJE DE CÓDIGOS ---

void actualizar_contexto_aprendizaje(const char* data, int len) {
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root) {
        cJSON *d = cJSON_GetObjectItem(root, "dispositivo");
        cJSON *b = cJSON_GetObjectItem(root, "boton");
        if (d && b) {
            strncpy(ctx_learn.dispositivo, d->valuestring, 31);
            strncpy(ctx_learn.boton, b->valuestring, 19);
            listo_para_capturar = true;
            ESP_LOGI(TAG, "Configurado para aprender: %s -> %s", ctx_learn.dispositivo, ctx_learn.boton);
        }
        cJSON_Delete(root);
    }
}


void tarea_modo_aprendizaje(void *pvParameters) {
    while (1) {
        if (!listo_para_capturar) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        uint16_t addr = 0, cmd = 0;
        if (funciones_esperar_y_parsear_ir(&addr, &cmd) == ESP_OK) {
            ESP_LOGI(TAG, "Capturado IR: 0x%04X 0x%04X. Subiendo a Firebase...", addr, cmd);
            
            if (con_firebase_patch_comando(ctx_learn.dispositivo, ctx_learn.boton, addr, cmd) == ESP_OK) {
                // Notificar éxito a la App vía MQTT
                mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"status\":\"aprendido_ok\"}");
                listo_para_capturar = false; // Esperar nueva orden de la App
            } else {
                mqtt_enviar_raw(MQTT_TOPIC_EVENTO, "{\"status\":\"ERROR_CLOUD\"}");
            }
        }
    }
}


void listar_archivos_spiffs(void) {
    ESP_LOGI("SPIFFS", "--- Listando archivos en /spiffs ---");
    struct dirent *de;
    DIR *dr = opendir("/spiffs"); 
    if (dr == NULL) {
        ESP_LOGE("SPIFFS", "No se pudo abrir el directorio /spiffs");
        return;
    }
    while ((de = readdir(dr)) != NULL) {
        ESP_LOGI("SPIFFS", "Encontrado: %s", de->d_name);
    }
    closedir(dr);
    ESP_LOGI("SPIFFS", "------------------------------------");
}



void app_main(void) {
	inicializar_niveles_log(); // <-- ¡Primero que todo!
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_spiffs();
    listar_archivos_spiffs();
    funciones_db_init(); // Carga la DB en RAM

    if (wifi_init_sta() == ESP_OK) {
	    // 1. Inicializa TX, RX y crea los handles (canales)
	    funciones_hardware_init(); 
	    
	    // 2. IMPORTANTE: Habilitar los canales una sola vez aquí
	    // Usamos la estructura global que definiste en funciones.c (ir_core)
	    rmt_enable(ir_core.tx_chan);
	    rmt_enable(ir_core.rx_chan);
	
	    cola_control_ir = xQueueCreate(10, sizeof(char *));
	    
	    // 3. Iniciar modo por defecto
	    cambiar_modo_trabajo(MODO_REMOTO_CONTROL);
	    
	    // 4. Iniciar MQTT
	    con_mqtt_init(cambiar_modo_trabajo);
	    
	    // Lanzamos la auditoría de configuración como una tarea independiente
	    // para que no bloquee el arranque del resto del sistema
	    xTaskCreate(tarea_sincronizacion_inicial, "sync_cfg", 8192, NULL, 5, NULL);
	    
	    
	}
}

// --- IMPLEMENTACIÓN DE INIT_SPIFFS ---
esp_err_t init_spiffs(void) {
    ESP_LOGI("SPIFFS", "Inicializando SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage", // Debe coincidir con tu partitions.csv
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE("SPIFFS", "Fallo al montar o formatear el sistema de archivos");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE("SPIFFS", "No se encontró la partición SPIFFS");
        } else {
            ESP_LOGE("SPIFFS", "Error al inicializar SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE("SPIFFS", "Fallo al obtener información de la partición (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI("SPIFFS", "Partición OK. Total: %d, Usado: %d", total, used);
    }
    return ESP_OK;
}


// --- TAREA: SINCRONIZACIÓN CON FIREBASE ---
void tarea_sincronizacion_inicial(void *pvParameters) {
    // Esperamos un par de segundos para asegurar que el stack TCP/IP y MQTT estén tranquilos
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "Iniciando auditoria de configuracion (Ambito: %s)...", MQTT_CLIENT_ID);
    
    // 1. Obtener la lista de equipos definidos para este ESP32
    char path_config[128];
	// Construimos la ruta: "Nodos_Config/ESP32_Escritorio"
	snprintf(path_config, sizeof(path_config), "%s/%s", "Nodos_Config", MQTT_CLIENT_ID);
	
	char *json_lista = con_firebase_get_json(path_config);
    
    if (json_lista) {
        printf("\n**********************************\n");
        printf("DATOS RECIBIDOS DE FIREBASE:\n%s\n", json_lista);
        printf("**********************************\n\n");
        
        cJSON *lista = cJSON_Parse(json_lista);
        if (cJSON_IsArray(lista)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, lista) {
                const char *id_disp = item->valuestring;

                if (id_disp) {
                    // Verificamos si ya existe en el db_ir.json local (RAM)
                    if (!funciones_db_existe_dispositivo(id_disp)) {
                        ESP_LOGW(TAG, "Equipo '%s' NO encontrado. Descargando...", id_disp);
                        
                        char path_disp[128];
                        snprintf(path_disp, sizeof(path_disp), "CRemotos/%s", id_disp);
                        char *json_disp = con_firebase_get_json(path_disp);
                        
                        if (json_disp) {
                            cJSON *obj_disp = cJSON_Parse(json_disp);
                            if (obj_disp) {
                                // Esta función guarda el nuevo equipo en RAM y persiste en SPIFFS
                                funciones_db_fusionar_dispositivo(id_disp, obj_disp);
                                cJSON_Delete(obj_disp);
                            }
                            free(json_disp);
                        }
                    } else {
                        ESP_LOGI(TAG, "Equipo '%s' ya está en memoria local.", id_disp);
                    }
                }
            }
        }
        cJSON_Delete(lista);
        free(json_lista);
    }

    ESP_LOGI(TAG, "Auditoria finalizada. Memoria libre: %d bytes", esp_get_free_heap_size());
    vTaskDelete(NULL); // La tarea termina y libera sus recursos
}


