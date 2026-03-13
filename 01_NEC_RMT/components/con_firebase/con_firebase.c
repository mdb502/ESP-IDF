#include "con_firebase.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"  // Ahora CMake sí lo encontrará
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "FIREBASE";

esp_err_t con_firebase_patch_comando(const char* path_db, const char* btn, uint16_t addr, uint16_t cmd) {
    char url[256];
    char payload[128];
    
    // URL apuntando al dispositivo
    snprintf(url, sizeof(url), "%s/CRemotos/%s.json", FIREBASE_HOST, path_db);
    // Payload: {"boton": "0xADDRCMD"}
    snprintf(payload, sizeof(payload), "{\"%s\": \"0x%04X%04X\"}", btn, addr, cmd);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err == ESP_OK && (status >= 200 && status < 300)) {
        ESP_LOGI(TAG, "Dato guardado en Firebase: %s", btn);
    } else {
        ESP_LOGE(TAG, "Error Firebase: %d", status);
        err = ESP_FAIL;
    }

    esp_http_client_cleanup(client);
    return err;
}