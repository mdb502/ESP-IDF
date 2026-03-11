idf_component_register(SRCS "funciones.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES json config esp_driver_rmt ir_tx ir_rx)