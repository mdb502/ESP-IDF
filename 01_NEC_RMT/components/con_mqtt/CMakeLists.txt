idf_component_register(SRCS "con_mqtt.c"
                       INCLUDE_DIRS "include"
                       REQUIRES mqtt driver funciones config
                       EMBED_TXTFILES "isrg_root_x1.pem")