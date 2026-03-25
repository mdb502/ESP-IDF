#include "config.h"

// Aquí es donde vive físicamente la variable en la memoria RAM
app_config_t g_config = {
    .wifi_ssid = WIFI_SSID,
    .wifi_pass = WIFI_PASS,
    .fb_host = FIREBASE_HOST,
    .mqtt_id = MQTT_CLIENT_ID
};