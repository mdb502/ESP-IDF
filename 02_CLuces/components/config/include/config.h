/*-------------------------------------
 Proyecto:		Control de Luces
 Modulo:		config.h
 Ruta:			components/config/include/config.h
 Version:		1.0 - 18.02.2026
 Objetivo:		Define constantes de configuracion
 Descripcion:	concentra las claves de autenticacion y parametros
 
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

// --- Parámetros de Lógica de Luces ---
#define MAX_PROGS       10  			// reserva memoria para ciclos encendido/apagado

// 2. ESTRUCTURA DE DATOS para cada luz
typedef struct {
    uint8_t triac_pin;     				// 1 byte (0-255 es suficiente para pines)
    uint8_t switch_pin;    				// 1 byte
    const char* topic_base;
    bool estado;           				// 1 byte
    uint8_t modo;          				// 1 byte (0-5)
    // Datos de origen para Modo 4
    struct { 
        uint8_t hora;      				// 1 byte (0-23)
        uint8_t min;       				// 1 byte (0-59)
    } schedule_h[MAX_PROGS]; 
    uint16_t duraciones[MAX_PROGS]; 	// 2 bytes (hasta 65535 min)
    uint8_t cant_actual;
    // MOTOR UNIFICADO (Timeline)
    time_t timeline[MAX_PROGS * 2]; 	// Pares [Inicio, Fin, Inicio, Fin...]
    uint8_t timeline_count;
} luz_t;

// 3. DECLARACIÓN EXTERNA DEL ARRAY
// // IMPORTANTE: Declara que existe el array y la variable que guardará el conteo
extern luz_t luces[];
extern int TOTAL_LUCES;


//WiFi
#define WIFI_SSID      "mdb2.4"
#define WIFI_PASS      "mdb2.4 segundo piso"

//HiveMQ Cloud
#define MQTT_URL       "mqtts://ce8c66f8369340a68d67bd9634f441c4.s2.eu.hivemq.cloud"
#define MQTT_PORT      8883
#define MQTT_USER      "mdbEsp32"
#define MQTT_PASS      "#Felipe2025"	
#define MQTT_CLIENT_ID "ESP32_Escritorio"

// NTP
#define TZ_CHILE "CLT4CLS3,M9.1.0/0,M4.1.0/0" 		// CLST = Chile Summer Time, CLT = Chile Standard Time
extern volatile bool hora_sincronizada;

// --- Tópicos Globales de Control ---
#define TOPIC_INICIO    "casa/micros/" MQTT_CLIENT_ID "/inicio"
#define TOPIC_CONTROL   "casa/micros/" MQTT_CLIENT_ID "/control"
#define TOPIC_EVENTO    "casa/micros/" MQTT_CLIENT_ID "/evento"

#define MINUTOS_REPORTE 5

// Parámetros de Random
#define MIN_GAP_PROGRAMA   5     	// Espacio mínimo entre ciclos (minutos)
#define RANDOM_CICLOS_MIN  3     	// Ciclos mínimos en modo Random
#define RANDOM_CICLOS_MAX  6     	// Ciclos máximos en modo Random
#define RANDOM_OFFSET_MIN  10    	// Variación mínima inicio (minutos)
#define RANDOM_OFFSET_MAX  20    	// Variación máxima inicio (minutos)

#endif