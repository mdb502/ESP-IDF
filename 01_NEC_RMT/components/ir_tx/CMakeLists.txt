idf_component_register(SRCS "ir_tx.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ir_nec_encoder config esp_driver_rmt)