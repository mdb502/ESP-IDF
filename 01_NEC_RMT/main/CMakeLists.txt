idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES config funciones ir_tx ir_rx con_wifi con_mqtt nvs_flash spiffs)