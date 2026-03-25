#ifndef CON_PROVISION_H
#define CON_PROVISION_H

#include "esp_err.h"

// Inicia el modo Access Point y el Servidor Web
void provision_start(void);

// Detiene todo y reinicia el ESP32
void provision_stop_and_restart(void);

#endif