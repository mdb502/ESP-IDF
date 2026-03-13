idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES config json funciones ir_tx ir_rx con_wifi con_mqtt con_firebase nvs_flash spiffs esp_driver_rmt)