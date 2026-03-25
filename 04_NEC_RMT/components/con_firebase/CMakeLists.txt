idf_component_register(SRCS "con_firebase.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_http_client json config mbedtls)