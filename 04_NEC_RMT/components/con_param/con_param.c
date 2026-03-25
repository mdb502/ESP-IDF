#include "con_param.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "PARAM";

// 1. Función auxiliar (fuera de la principal) para leer strings de NVS con logs
static esp_err_t leer_string_nvs(nvs_handle_t handle, const char* key, char* target, size_t max_size) {
    size_t size = max_size;
    esp_err_t err = nvs_get_str(handle, key, target, &size);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Cargado [%s]: %s", key, target);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Clave [%s] no encontrada en NVS.", key);
    } else {
        ESP_LOGE(TAG, "Error (%s) al leer [%s]", esp_err_to_name(err), key);
    }
    return err;
}

esp_err_t param_load_config(app_config_t *dest) {
    nvs_handle_t handle;
    ESP_LOGI(TAG, "Abriendo NVS para lectura...");
    
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo abrir la NVS (Namespace 'storage'). ¿Es la primera vez?");
        return err;
    }

    // Leemos cada parámetro usando la función auxiliar
    esp_err_t e1 = leer_string_nvs(handle, "wifi_ssid", dest->wifi_ssid, sizeof(dest->wifi_ssid));
    ESP_ERROR_CHECK(leer_string_nvs(handle, "wifi_pass", dest->wifi_pass, sizeof(dest->wifi_pass)));
	ESP_ERROR_CHECK(leer_string_nvs(handle, "fb_host",   dest->fb_host,   sizeof(dest->fb_host)));
    esp_err_t e3 = leer_string_nvs(handle, "mqtt_id",   dest->mqtt_id,   sizeof(dest->mqtt_id));

    nvs_close(handle);

    // Si alguna de las lecturas críticas falló, devolvemos error para activar provisión
    if (e1 != ESP_OK || e3 != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t param_save_config(const app_config_t *src) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Guardando nueva configuracion en NVS...");

    err = nvs_set_str(handle, "wifi_ssid", src->wifi_ssid);
    err |= nvs_set_str(handle, "wifi_pass", src->wifi_pass);
    err |= nvs_set_str(handle, "mqtt_id",   src->mqtt_id);
    err |= nvs_set_str(handle, "fb_host",   src->fb_host);

    if (err == ESP_OK) {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "¡Cambios guardados exitosamente!");
    } else {
        ESP_LOGE(TAG, "Error al escribir en NVS.");
    }

    nvs_close(handle);
    return err;
}

esp_err_t param_init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS truncada o nueva version. Borrando...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}