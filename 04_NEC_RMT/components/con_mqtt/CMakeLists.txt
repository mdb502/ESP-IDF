idf_component_register(SRCS "con_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt funciones config json esp_driver_rmt
                       EMBED_TXTFILES "isrg_root_x1.pem")