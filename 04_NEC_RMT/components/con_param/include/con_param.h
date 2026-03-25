#ifndef CON_PARAM_H
#define CON_PARAM_H

#include "esp_err.h"
#include "config.h"

// Funciones de gestión
esp_err_t param_init_nvs(void);
esp_err_t param_load_config(app_config_t *dest);
esp_err_t param_save_config(const app_config_t *src);

#endif