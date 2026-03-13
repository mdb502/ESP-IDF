idf_component_register(SRCS "funciones.c"
                    INCLUDE_DIRS "include"
                    REQUIRES json config ir_tx ir_rx esp_driver_rmt)