idf_component_register(
    SRCS "con_param.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash config esp_driver_rmt  # <--- Agregamos esp_driver_rmt
)