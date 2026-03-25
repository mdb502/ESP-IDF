idf_component_register(SRCS "funciones.c"
                    INCLUDE_DIRS "include"
                    REQUIRES 
				        json
				        mqtt
				        con_mqtt 
				        con_firebase 
				        ir_tx 
				        ir_rx
				        config
				        esp_driver_rmt
				)
                    