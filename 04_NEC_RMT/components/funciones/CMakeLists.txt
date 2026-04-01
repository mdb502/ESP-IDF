idf_component_register(
    SRCS "funciones.c"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_rmt nvs_flash json ir_tx ir_rx con_firebase con_mqtt
)