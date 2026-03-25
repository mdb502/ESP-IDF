#pragma once


//WiFi
#define WIFI_SSID      "mdb2.4"
#define WIFI_PASS      "mdb2.4 segundo piso"

//HiveMQ Cloud
#define MQTT_URL       "mqtts://ce8c66f8369340a68d67bd9634f441c4.s2.eu.hivemq.cloud"
#define MQTT_PORT      8883
#define MQTT_USER      "mdbEsp32"
#define MQTT_PASS      "#Felipe2025"	
#define MQTT_CLIENT_ID "CREM_Escritorio"

// --- Tópicos ---
#define MQTT_TOPIC_COMANDO  "casa/cremoto/" MQTT_CLIENT_ID "/comando"
#define MQTT_TOPIC_EVENTO   "casa/cremoto/" MQTT_CLIENT_ID "/evento"


// --- Definiciones de Modos del Sistema ---
typedef enum {
    MODO_REMOTO_CONTROL,
    MODO_APRENDIZAJE
} modo_sistema_t;

// Variable de configuración inicial (Paso 1)
#define MODO_CONFIGURADO_INICIAL  MODO_REMOTO_CONTROL

#define EXAMPLE_IR_RESOLUTION_HZ     1000000 // 1MHz, 1 tick = 1us
#define EXAMPLE_IR_TX_GPIO_NUM       18
#define EXAMPLE_IR_RX_GPIO_NUM       19
#define EXAMPLE_IR_NEC_DECODE_MARGIN 500		// MDB (antes 200)     

// NEC timing spec
#define NEC_LEADING_CODE_DURATION_0  9000
#define NEC_LEADING_CODE_DURATION_1  4500
#define NEC_PAYLOAD_ZERO_DURATION_0  560
#define NEC_PAYLOAD_ZERO_DURATION_1  560
#define NEC_PAYLOAD_ONE_DURATION_0   560
#define NEC_PAYLOAD_ONE_DURATION_1   1690
#define NEC_REPEAT_CODE_DURATION_0   9000
#define NEC_REPEAT_CODE_DURATION_1   2250

// Sharp Receiver (48-bit) timing spec
#define SHARP_H_LEAD_0               3260  // El pulso largo inicial
#define SHARP_H_LEAD_1               1780  // El espacio inicial
#define SHARP_BIT_MARK               305   // Pulso constante (antes 320)
#define SHARP_BIT_ONE_SPACE          1375  // Espacio para '1' (antes 1600)
#define SHARP_BIT_ZERO_SPACE         535   // Espacio para '0' (antes 680)
#define SHARP_DECODE_MARGIN          150   // Margen de tolerancia


// --- Firebase Config ---
#define FIREBASE_HOST "https://mdb-net.firebaseio.com"
#define FIREBASE_AUTH "YjRsmKp11bPmHbOTTIh3lgxSDR2LtxQawXqhQNqr"
// Tópico para recibir la configuración de aprendizaje (JSON con nombre_dispositivo y nombre_boton)
//#define TOPIC_CONFIG_LEARN "v1/esp32/ir/config/learn_setup"
#define FIREBASE_PATH_NODOS "Ubicaciones"
#define FIREBASE_PATH_REMOTOS "CRemotos"

typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    char fb_host[128];
    char mqtt_id[32];
} app_config_t;

// Declaramos que la variable existirá en algún lugar del proyecto
extern app_config_t g_config;


// --- Estructura para el contexto de aprendizaje ---
typedef struct {
    char dispositivo[32];
    char boton[20];
} context_aprendizaje_t;