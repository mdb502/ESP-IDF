#pragma once
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

typedef struct {
    uint16_t address;
    uint16_t command;
} ir_nec_scan_code_t;

typedef struct {
    uint32_t resolution; 
} ir_nec_encoder_config_t;

// Prototipos
esp_err_t ir_tx_init(rmt_channel_handle_t *tx_chan, rmt_encoder_handle_t *nec_encoder);
esp_err_t rmt_new_ir_nec_encoder(const ir_nec_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder);