#include "con_provision.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "con_param.h"
#include <string.h>
#include <ctype.h>  // Para isxdigit
#include "esp_mac.h"


static const char *TAG = "PROVISION";
static httpd_handle_t server = NULL;

// --- FUNCIÓN DE DECODIFICACIÓN ---
// Convierte "+" en espacio y "%XX" en caracteres reales
void url_decode(char *src) {
    char *dst = src;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}


// Formulario HTML embebido (Sin cambios)
static const char* html_page = 
"<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif; background:#f4f4f4; padding:20px;} .card{background:white; padding:20px; border-radius:10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); max-width:400px; margin:auto;}"
"input{width:100%; padding:10px; margin:10px 0; border:1px solid #ccc; border-radius:5px; box-sizing: border-box;}"
"button{width:100%; padding:10px; background:#007bff; color:white; border:none; border-radius:5px; cursor:pointer; font-weight:bold;}</style>"
"</head><body><div class='card'>"
"<h2>Configuración Nodo IR</h2>"
"<form action='/save' method='POST'>"
"SSID WiFi:<input name='ssid' type='text' required>"
"Password:<input name='pass' type='password' required>"
"Nombre Nodo (MQTT ID):<input name='node' type='text' placeholder='Ej: Living_ESP32' required>"
"Firebase Host:<input name='fb' type='text' placeholder='https://tu-proyecto.firebaseio.com' required>"
"<button type='submit'>Guardar y Reiniciar</button>"
"</form></div></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

// Handler para recibir y procesar los datos (POST /save)
static esp_err_t save_post_handler(httpd_req_t *req) {
    char buf[512]; 
    int ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Datos RAW recibidos: %s", buf);

    app_config_t new_cfg;
    
    // 1. Extraemos los valores
    httpd_query_key_value(buf, "ssid", new_cfg.wifi_ssid, sizeof(new_cfg.wifi_ssid));
    httpd_query_key_value(buf, "pass", new_cfg.wifi_pass, sizeof(new_cfg.wifi_pass));
    httpd_query_key_value(buf, "node", new_cfg.mqtt_id,   sizeof(new_cfg.mqtt_id));
    httpd_query_key_value(buf, "fb",   new_cfg.fb_host,   sizeof(new_cfg.fb_host));

    // 2. LIMPIAMOS los valores (Decodificación URL)
    url_decode(new_cfg.wifi_ssid);
    url_decode(new_cfg.wifi_pass);
    url_decode(new_cfg.mqtt_id);
    url_decode(new_cfg.fb_host);

    ESP_LOGI(TAG, "Configuración limpia: SSID:%s, Pass:%s, ID:%s", 
             new_cfg.wifi_ssid, new_cfg.wifi_pass, new_cfg.mqtt_id);

    // 3. Guardamos en NVS
    param_save_config(&new_cfg);

    const char* resp = "<html><body><h1>Configuracion guardada</h1><p>Reiniciando...</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGW(TAG, "Reiniciando sistema...");
    esp_restart();
    return ESP_OK;
}

// Registro de URIs (C puro)
static const httpd_uri_t uri_get = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_post = {
    .uri       = "/save",
    .method    = HTTP_POST,
    .handler   = save_post_handler,
    .user_ctx  = NULL
};

void provision_start(void) {
    ESP_LOGI(TAG, "Iniciando Access Point...");
       
    // 1. Configuración de Red AP
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "Config_Control_IR",
            .ssid_len = strlen("Config_Control_IR"),
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN // Sin clave para fácil acceso
        }
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP Listo. Red: %s", wifi_config.ap.ssid);

    // 2. Iniciar servidor web
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        ESP_LOGI(TAG, "Servidor Web en marcha.");
    }
}