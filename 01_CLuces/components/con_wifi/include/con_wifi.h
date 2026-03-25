#ifndef CON_WIFI_H
#define CON_WIFI_H

#include "esp_err.h"

/* Inicializa el stack de red y conecta como Station */
esp_err_t wifi_init_sta(void);

#endif