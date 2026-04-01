idf_component_register(
    SRCS "config.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_rmt  # <--- Esto es vital para que encuentre rmt_types.h
)