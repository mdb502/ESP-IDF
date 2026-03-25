/*-------------------------------------
 Proyecto:		Control de luces
 Modulo:		funciones.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Concentra las funciones generales del proyecto
 Descripcion:	
 				
 --------------------------------------
*/

#include "config.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "funciones.h"
#include "con_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FUNC_LOGIC";


// --- MOTOR DE TIEMPOS (Timeline) ---

/**
 * Traduce los horarios programados (Modo 4) a marcas de tiempo Unix reales.
 * Maneja automáticamente si el ciclo pertenece a hoy o debe proyectarse al futuro.
 */
void func_generar_timeline_modo4(int i) {
    time_t ahora;
    time(&ahora);
    struct tm *tm_info = localtime(&ahora);
    luces[i].timeline_count = 0;

    ESP_LOGI(TAG, "--- GENERANDO TIMELINE (PROYECCION) LUZ %d ---", i + 1);

    for (int p = 0; p < luces[i].cant_actual; p++) {
        struct tm t_prog = *tm_info;
        t_prog.tm_hour = luces[i].schedule_h[p].hora;
        t_prog.tm_min  = luces[i].schedule_h[p].min;
        t_prog.tm_sec  = 0;

        time_t inicio = mktime(&t_prog);
        time_t fin    = inicio + (luces[i].duraciones[p] * 60);

        // AJUSTE CLAVE: Si el evento ya terminó hace más de 5 minutos,
        // significa que el próximo cumplimiento es MAÑANA (+86400 segundos).
        if (ahora > (fin + 300)) { 
            inicio += 86400;
            fin += 86400;
        }

        if (luces[i].timeline_count < (MAX_PROGS * 2)) {
            luces[i].timeline[luces[i].timeline_count++] = inicio;
            luces[i].timeline[luces[i].timeline_count++] = fin;

            // Debug para verificar fechas
            struct tm tm_ini;
            localtime_r(&inicio, &tm_ini);
            ESP_LOGI(TAG, "Prog %d: %02d/%02d %02d:%02d -> %02d min", 
                     p + 1, tm_ini.tm_mday, tm_ini.tm_mon + 1, 
                     tm_ini.tm_hour, tm_ini.tm_min, luces[i].duraciones[p]);
        }
    }
    ESP_LOGI(TAG, "---------------------------------------------");
}

/**
 * Verifica si el tiempo actual cae dentro de alguna de las ventanas del timeline.
 */
bool func_check_timeline(int i, time_t ahora) {
    for (int p = 0; p < luces[i].timeline_count; p += 2) {
        if (ahora >= luces[i].timeline[p] && ahora < luces[i].timeline[p+1]) {
            return true; // Debería estar ON
        }
    }
    return false; // Debería estar OFF
}



void func_generar_timeline_random(int i) {
    time_t ahora;
    time(&ahora);
    struct tm *tm_info = localtime(&ahora);
    luces[i].timeline_count = 0;

    // 1. Determinar 'n' (ciclos)
    uint8_t n = RANDOM_CICLOS_MIN + (esp_random() % (RANDOM_CICLOS_MAX - RANDOM_CICLOS_MIN + 1));

    // 2. Calcular Referencias con Cruce de Medianoche
    struct tm t_ini = *tm_info;
    t_ini.tm_hour = luces[i].schedule_h[0].hora;
    t_ini.tm_min  = luces[i].schedule_h[0].min;
    t_ini.tm_sec  = 0;
    
    struct tm t_fin = *tm_info;
    t_fin.tm_hour = luces[i].schedule_h[1].hora;
    t_fin.tm_min  = luces[i].schedule_h[1].min;
    t_fin.tm_sec  = 0;

    time_t inicio_ref = mktime(&t_ini);
    time_t fin_ref = mktime(&t_fin);
    if (fin_ref <= inicio_ref) fin_ref += 86400; // Salto de día

    // 3. Aplicar Offsets Aleatorios
    int off_ini = RANDOM_OFFSET_MIN + (esp_random() % (RANDOM_OFFSET_MAX - RANDOM_OFFSET_MIN + 1));
    if (esp_random() % 2) off_ini *= -1;
    time_t t_inicio_real = inicio_ref + (off_ini * 60);

    int off_fin = RANDOM_OFFSET_MIN + (esp_random() % (RANDOM_OFFSET_MAX - RANDOM_OFFSET_MIN + 1));
    if (esp_random() % 2) off_fin *= -1;
    time_t t_fin_objetivo = fin_ref + (off_fin * 60);

    time_t tiempo_total_disponible = t_fin_objetivo - t_inicio_real;

    // 4. Cálculo de Factor de Escala inicial
    uint32_t suma_minutos_raw = 0;
    uint16_t d_on_raw[RANDOM_CICLOS_MAX], d_off_raw[RANDOM_CICLOS_MAX];

    for (int j = 0; j < n; j++) {
        d_on_raw[j] = luces[i].duraciones[0] + (esp_random() % (luces[i].duraciones[1] - luces[i].duraciones[0] + 1));
        d_off_raw[j] = luces[i].duraciones[0] + (esp_random() % (luces[i].duraciones[1] - luces[i].duraciones[0] + 1));
        suma_minutos_raw += (d_on_raw[j] + d_off_raw[j]);
    }


	// CÁLCULO DE FACTOR CON RED DE SEGURIDAD
    float factor = 1.0; 
    if (suma_minutos_raw > 0 && tiempo_total_disponible > 0) {
        factor = (float)tiempo_total_disponible / (float)(suma_minutos_raw * 60);
    } else {
        ESP_LOGW(TAG, "Rango de tiempo insuficiente, usando factor 1.0");
    }


    //float factor = (float)tiempo_total_disponible / (float)(suma_minutos_raw * 60);
    
    ESP_LOGI(TAG, "--- GENERANDO TIMELINE RANDOM LUZ %d ---", i + 1);
    ESP_LOGI(TAG, "Ciclos calculados (n): %d", n);
    ESP_LOGI(TAG, "Factor escala: %.2f | Gap min: %d min", factor, MIN_GAP_PROGRAMA);

    // 5. Construcción del Timeline con AJUSTE DE EMPUJE (Cascada)
    time_t tiempo_acumulado = t_inicio_real;
    uint32_t gap_segundos_min = MIN_GAP_PROGRAMA * 60;

    for (int c = 0; c < n; c++) {
        // --- Duración ON ---
        uint32_t dur_ajustada_on = (uint32_t)(d_on_raw[c] * 60 * factor);
        if (dur_ajustada_on < gap_segundos_min) {
            dur_ajustada_on = gap_segundos_min; // Forzar mínimo
        }

        time_t t_encendido = tiempo_acumulado;
        time_t t_apagado = t_encendido + dur_ajustada_on;

        if (luces[i].timeline_count < (MAX_PROGS * 2)) {
            luces[i].timeline[luces[i].timeline_count++] = t_encendido;
            luces[i].timeline[luces[i].timeline_count++] = t_apagado;
            
            struct tm log_i, log_f;
            localtime_r(&t_encendido, &log_i);
            localtime_r(&t_apagado, &log_f);
            ESP_LOGI(TAG, "Ciclo %d: [%02d:%02d] -> [%02d:%02d] (%ld min)", 
                     c+1, log_i.tm_hour, log_i.tm_min, log_f.tm_hour, log_f.tm_min, dur_ajustada_on/60);
        }
        
        // --- Duración OFF (Tiempo hasta el próximo ciclo) ---
        uint32_t dur_ajustada_off = (uint32_t)(d_off_raw[c] * 60 * factor);
        if (dur_ajustada_off < gap_segundos_min) {
            dur_ajustada_off = gap_segundos_min; // Forzar mínimo
        }

        // El 'tiempo_acumulado' para el PRÓXIMO ciclo se basa en el apagado actual + gap off
        tiempo_acumulado = t_apagado + dur_ajustada_off;
    }

    // 6. Proyección Final
    if (ahora > luces[i].timeline[luces[i].timeline_count - 1]) {
        ESP_LOGW(TAG, "Periodo Random ya paso. Proyectando a mañana.");
        for (int j = 0; j < luces[i].timeline_count; j++) {
            luces[i].timeline[j] += 86400;
        }
    }
    ESP_LOGI(TAG, "---------------------------------------------");
}



// --- TAREA DE CONTROL PRINCIPAL ---

void tarea_control_luces(void *pvParameters) {
    ESP_LOGI(TAG, "Tarea de control de luces iniciada (Ventana de Tiempo).");
    
    while (1) {
        if (hora_sincronizada) {
            time_t ahora;
            time(&ahora);

            for (int i = 0; i < TOTAL_LUCES; i++) {
                bool estado_objetivo = luces[i].estado; // Por defecto mantenemos el estado actual

                switch (luces[i].modo) {
                    case 1: // MODO NORMAL: El estado solo cambia por interrupción o MQTT
                        break;

                    case 4: // MODO PROGRAMADO: Seguir el Timeline
                        // Si el timeline está vacío o el último evento ya expiró, regeneramos.
                        if (luces[i].timeline_count == 0 || ahora > luces[i].timeline[luces[i].timeline_count - 1]) {
                            func_generar_timeline_modo4(i);
                        }
                        estado_objetivo = func_check_timeline(i, ahora);
                        break;

                    case 5: // MODO RANDOM: (Estructura lista para implementar)
                        if (luces[i].timeline_count == 0 || ahora > luces[i].timeline[luces[i].timeline_count - 1]) {
				            func_generar_timeline_random(i);
				        }
				        estado_objetivo = func_check_timeline(i, ahora);
				        break;

                    default:
                        break;
                }

                // APLICACIÓN DE CAMBIOS (Hysteresis)
                // Solo actúa si el hardware difiere de lo que dicta el Modo actual
                if (luces[i].estado != estado_objetivo) {
                    luces[i].estado = estado_objetivo;
                    gpio_set_level(luces[i].triac_pin, luces[i].estado);
                    
                    // Notificación de evento estandarizada
                    char t_str[25], msg[85];
                    func_obtener_timestamp(t_str, sizeof(t_str));
                    snprintf(msg, sizeof(msg), "%s > estado = %d, Luz %d (Modo %u)", 
                             t_str, luces[i].estado, i + 1, luces[i].modo);
                    
                    func_publicar_mensaje(TOPIC_EVENTO, msg);
                    ESP_LOGI(TAG, "Cambio automatico: Luz %d -> %s", i + 1, luces[i].estado ? "ON" : "OFF");
                }
            }
        }
        // Verificación cada 30 segundos (Precisión suficiente para cambios por minuto)
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void func_actualizar_logica_modo(int i, const char* json_data) {
    cJSON *root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Error al parsear JSON para Luz %d", i + 1);
        return;
    }

    cJSON *m = cJSON_GetObjectItem(root, "m");
    if (cJSON_IsNumber(m)) {
        luces[i].modo = (uint8_t)m->valueint;

        // AHORA: Entramos si es Modo 4 O Modo 5
        if (luces[i].modo == 4 || luces[i].modo == 5) {
            cJSON *h_arr = cJSON_GetObjectItem(root, "h");
            cJSON *d_arr = cJSON_GetObjectItem(root, "d");

            if (cJSON_IsArray(h_arr) && cJSON_IsArray(d_arr)) {
                int count = 0;
                cJSON *h_item;
                cJSON_ArrayForEach(h_item, h_arr) {
                    if (count < MAX_PROGS && cJSON_IsString(h_item)) {
                        sscanf(h_item->valuestring, "%hhu:%hhu",
                               &luces[i].schedule_h[count].hora, 
                               &luces[i].schedule_h[count].min);
                        
                        cJSON *d_item = cJSON_GetArrayItem(d_arr, count);
                        luces[i].duraciones[count] = cJSON_IsNumber(d_item) ? (uint16_t)d_item->valueint : 0;
                        count++;
                    }
                }
                luces[i].cant_actual = (uint8_t)count;
                luces[i].timeline_count = 0; // Reset fundamental

                // Disparador inmediato según el modo
                if (hora_sincronizada) {
                    if (luces[i].modo == 4) func_generar_timeline_modo4(i);
                    else if (luces[i].modo == 5) func_generar_timeline_random(i);
                }
            }
        }
        ESP_LOGI(TAG, "Luz %d actualizada: Modo %u", i + 1, luces[i].modo);
    }
    cJSON_Delete(root);
}


void func_obtener_timestamp(char *dest, size_t max_len) {
    // Si la variable global de config.h es falsa, devolvemos el texto de espera
    if (!hora_sincronizada) {
        snprintf(dest, max_len, "aun sin tiempo real");
        return;
    }

    // Si hay hora sincronizada, procedemos normal
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    strftime(dest, max_len, "%Y-%m-%d %H:%M:%S", &timeinfo);
}

void func_publicar_mensaje(const char *topic, const char *msg) {
    char fecha[25];
    char mensaje_final[128];
    
    // 1. Obtenemos el tiempo real
    func_obtener_timestamp(fecha, sizeof(fecha));
    
    // 2. Formateamos: "2026-02-21 17:56:07 > mensaje"
    snprintf(mensaje_final, sizeof(mensaje_final), "%s > %s", fecha, msg);
    
    // 3. Enviamos al driver MQTT
    mqtt_enviar_raw(topic, mensaje_final);
}

