#ifndef CON_FIREBASE_H
#define CON_FIREBASE_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Sube un comando IR a Firebase (PATCH).
 */
esp_err_t con_firebase_patch_comando(const char* dispositivo, const char* btn, uint16_t addr, uint16_t cmd, const char* norma);

/**
 * @brief Obtiene un JSON desde Firebase (GET). 
 * @param path Ruta en la base de datos (sin .json).
 * @return Puntero a string con el JSON. DEBE SER LIBERADO CON free().
 */
char* con_firebase_get_json(const char* path);

#endif