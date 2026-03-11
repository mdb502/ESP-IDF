#ifndef CON_FIREBASE_H
#define CON_FIREBASE_H

#include <stdbool.h>
#include "esp_err.h"

// Inicializa la conexión con Firebase y descarga el JSON de controles
esp_err_t con_firebase_sync_db(const char* dispositivo_id);

// Verifica si hay actualizaciones en la nube
bool con_firebase_check_updates(void);

#endif