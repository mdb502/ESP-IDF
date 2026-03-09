#include "ir_tx.h"
#include "config.h"
#include "esp_log.h"

esp_err_t ir_tx_init(rmt_channel_handle_t *tx_chan, rmt_encoder_handle_t *nec_encoder) {
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = EXAMPLE_IR_TX_GPIO_NUM,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, tx_chan));

    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000, 
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(*tx_chan, &carrier_cfg));

    ir_nec_encoder_config_t nec_cfg = {.resolution = EXAMPLE_IR_RESOLUTION_HZ};
    return rmt_new_ir_nec_encoder(&nec_cfg, nec_encoder);
}