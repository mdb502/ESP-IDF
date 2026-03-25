idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES con_wifi con_ntp con_mqtt con_luces funciones config)
                       