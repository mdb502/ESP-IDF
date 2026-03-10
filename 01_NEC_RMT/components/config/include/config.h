#pragma once


//WiFi
#define WIFI_SSID      "mdb2.4-2"
#define WIFI_PASS      "mdb2.4-2 segundo piso"

//HiveMQ Cloud
#define MQTT_URL       "mqtts://ce8c66f8369340a68d67bd9634f441c4.s2.eu.hivemq.cloud"
#define MQTT_PORT      8883
#define MQTT_USER      "mdbEsp32"
#define MQTT_PASS      "#Felipe2025"	
#define MQTT_CLIENT_ID "ESP32_Escritorio"

// --- Tópicos ---
#define TOPIC_MODO      "v1/esp32/ir/config/modo"
#define TOPIC_LEARN     "v1/esp32/ir/data/aprendido"
#define TOPIC_CMD       "v1/esp32/ir/control/comando"


// --- Definiciones de Modos del Sistema ---
typedef enum {
    MODO_REMOTO_CONTROL,
    MODO_APRENDIZAJE
} modo_sistema_t;

// Variable de configuración inicial (Paso 1)
#define MODO_CONFIGURADO_INICIAL  MODO_APRENDIZAJE

#define EXAMPLE_IR_RESOLUTION_HZ     1000000 // 1MHz, 1 tick = 1us
#define EXAMPLE_IR_TX_GPIO_NUM       18
#define EXAMPLE_IR_RX_GPIO_NUM       19
#define EXAMPLE_IR_NEC_DECODE_MARGIN 200     

// NEC timing spec
#define NEC_LEADING_CODE_DURATION_0  9000
#define NEC_LEADING_CODE_DURATION_1  4500
#define NEC_PAYLOAD_ZERO_DURATION_0  560
#define NEC_PAYLOAD_ZERO_DURATION_1  560
#define NEC_PAYLOAD_ONE_DURATION_0   560
#define NEC_PAYLOAD_ONE_DURATION_1   1690
#define NEC_REPEAT_CODE_DURATION_0   9000
#define NEC_REPEAT_CODE_DURATION_1   2250