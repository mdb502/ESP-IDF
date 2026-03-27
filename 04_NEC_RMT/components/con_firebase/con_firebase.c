// components/con_firebase/con_firebase.c
#include "con_firebase.h"
#include "config.h"
#include "funciones.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"  // Ahora CMake sí lo encontrará
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "FIREBASE";

esp_err_t con_firebase_patch_comando(const char* dispositivo, const char* btn, uint16_t addr, uint16_t cmd, const char* norma) {
    char url[512];
    char payload[256]; // Aumentamos un poco el tamaño para que quepa todo el JSON
    char disp_limpio[64]; // Variable temporal para el nombre sin espacios
    
    // --- APLICAMOS EL TRIM AQUÍ ---
    utils_trim(disp_limpio, dispositivo, sizeof(disp_limpio));
    
    // Ahora usamos 'disp_limpio' en la URL
    snprintf(url, sizeof(url), "%s/CRemotos/%s.json?auth=%s", 
             FIREBASE_HOST, disp_limpio, FIREBASE_AUTH);
    
    // El JSON ahora lleva el botón Y la norma al mismo nivel
    // Resultado esperado: {"power": "0x1234ABCD", "norma": "NEC"}
    snprintf(payload, sizeof(payload), "{\"%s\": \"0x%04X%04X\", \"norma\": \"%s\"}", 
             btn, addr, cmd, norma);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err == ESP_OK && (status >= 200 && status < 300)) {
        ESP_LOGI(TAG, "Dato y Norma (%s) guardados en: %s", norma, dispositivo);
    } else {
        ESP_LOGE(TAG, "Error Firebase PATCH: %d", status);
        err = ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    return err;
}




// Esta estructura nos ayuda a pasar el buffer al manejador de eventos
typedef struct {
    char *buffer;
    int buffer_idx;
} response_data_t;

// El "Manejador de Eventos" que captura los datos de Firebase
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        response_data_t *data = (response_data_t *)evt->user_data;
        if (data && data->buffer) {
            // Copiamos los datos que van llegando al buffer
            memcpy(data->buffer + data->buffer_idx, evt->data, evt->data_len);
            data->buffer_idx += evt->data_len;
        }
    }
    return ESP_OK;
}

char* con_firebase_get_json(const char* path) {
    char url[512];
    // También añadimos auth aquí
    snprintf(url, sizeof(url), "%s/%s.json?auth=%s", 
             FIREBASE_HOST, path, FIREBASE_AUTH);

    char *response_buffer = malloc(4096);
    if (!response_buffer) return NULL;
    memset(response_buffer, 0, 4096);

    response_data_t user_data = { .buffer = response_buffer, .buffer_idx = 0 };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler, // <--- Conectamos el capturador
        .user_data = &user_data,              // <--- Pasamos nuestro buffer
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) {
            response_buffer[user_data.buffer_idx] = 0; // Asegurar fin de string
            ESP_LOGI("FIREBASE", "GET Exitoso: %s (%d bytes)", path, user_data.buffer_idx);
        } else {
            ESP_LOGE("FIREBASE", "Error HTTP: %d", status);
            free(response_buffer);
            response_buffer = NULL;
        }
    } else {
        ESP_LOGE("FIREBASE", "Error de conexion: %s", esp_err_to_name(err));
        free(response_buffer);
        response_buffer = NULL;
    }

    esp_http_client_cleanup(client);
    return response_buffer;
}




