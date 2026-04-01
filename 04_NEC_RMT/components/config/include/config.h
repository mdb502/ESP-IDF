#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h" // Necesario para QueueHandle_t
#include "freertos/queue.h"    // Necesario para QueueHandle_t
#include "driver/rmt_types.h"  // Necesario para rmt_channel_handle_t y rmt_encoder_handle_t

// --- WiFi ---
#define WIFI_SSID      "mdb2.4"
#define WIFI_PASS      "mdb2.4 segundo piso"

// --- HiveMQ Cloud ---
#define MQTT_URL       "mqtts://ce8c66f8369340a68d67bd9634f441c4.s2.eu.hivemq.cloud"
#define MQTT_PORT      8883
#define MQTT_USER      "mdbEsp32"
#define MQTT_PASS      "#Felipe2025"	
#define MQTT_CLIENT_ID "CREM_Escritorio"

// --- Firebase ---
#define FIREBASE_HOST "https://mdb-net.firebaseio.com"
#define FIREBASE_AUTH "YjRsmKp11bPmHbOTTIh3lgxSDR2LtxQawXqhQNqr"
#define FIREBASE_PATH_UBICACIONES "Ubicaciones"
#define FIREBASE_PATH_REMOTOS "CRemotos"

// --- PROTOCOLO DE COMUNICACIÓN (MQTT) ---
// Llaves que el ESP32 espera recibir en el JSON por MQTT
#define KEY_MQTT_DISP    "disp"
#define KEY_MQTT_BTN     "btn"
#define KEY_MQTT_CMD     "cmd"

// Valores posibles para la llave "cmd"
#define VAL_CMD_DISP     "CMD"			// Envío de señal IR normal
#define VAL_CMD_SYNC     "SYNC"			// Sincronización con Firebase
#define VAL_CMD_LEARN    "LEARN"		// Modo Aprendizaje

// --- ATRIBUTOS DE BASE DE DATOS (Firebase / JSON Local) ---
// Llaves que el ESP32 busca dentro de /CRemotos/Nombre_Dispositivo/
#define KEY_DB_NORMA     "norma"
#define VAL_NORMA_NEC    "NEC"
#define VAL_NORMA_SHARP  "SHARP48"

// --- TÓPICOS MQTT ---
#define MQTT_TOPIC_COMANDO  "mdbHome/cremoto/" MQTT_CLIENT_ID "/comando"
#define MQTT_TOPIC_EVENTO   "mdbHome/cremoto/" MQTT_CLIENT_ID "/evento"

// --- Configuración IR Hardware ---
#define IR_RESOLUTION_HZ     1000000 
#define IR_TX_GPIO_NUM       18
#define IR_RX_GPIO_NUM       19
#define IR_NEC_DECODE_MARGIN 500

// --- ESTRUCTURAS GLOBALES ---

typedef struct {
    uint16_t address;
    uint16_t command;
    char norma[20];
    bool encontrado;
} resultado_busqueda_ir_t;

typedef struct {
    char dispositivo[32];
    char boton[20];
} context_aprendizaje_t;

typedef struct {
    rmt_channel_handle_t tx_chan;
    rmt_encoder_handle_t encoder;
    rmt_channel_handle_t rx_chan;
    QueueHandle_t rx_queue;
} ir_core_t;

typedef enum {
    MODO_REMOTO_CONTROL,
    MODO_APRENDIZAJE
} modo_sistema_t;

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    char fb_host[128];
    char mqtt_id[32];
} app_config_t;

extern app_config_t g_config;

// --- Timings Protocolos ---

// NEC timing
#define NEC_LEADING_CODE_DURATION_0  9000
#define NEC_LEADING_CODE_DURATION_1  4500
#define NEC_PAYLOAD_ZERO_DURATION_0  560
#define NEC_PAYLOAD_ZERO_DURATION_1  560
#define NEC_PAYLOAD_ONE_DURATION_0   560
#define NEC_PAYLOAD_ONE_DURATION_1   1690

// Sharp 48-bit timing
#define SHARP_H_LEAD_0               3260
#define SHARP_H_LEAD_1               1780
#define SHARP_BIT_MARK               305
#define SHARP_BIT_ONE_SPACE          1375
#define SHARP_BIT_ZERO_SPACE         535
#define SHARP_DECODE_MARGIN          150