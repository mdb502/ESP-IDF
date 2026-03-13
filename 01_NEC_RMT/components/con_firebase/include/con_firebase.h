#ifndef CON_FIREBASE_H
#define CON_FIREBASE_H

#include "esp_err.h"
#include <stdint.h>

esp_err_t con_firebase_patch_comando(const char* path_db, const char* btn, uint16_t addr, uint16_t cmd);

#endif