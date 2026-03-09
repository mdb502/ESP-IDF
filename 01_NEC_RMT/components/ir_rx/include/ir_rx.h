#pragma once
#include "driver/rmt_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

esp_err_t ir_rx_init(rmt_channel_handle_t *rx_chan, QueueHandle_t *rx_queue);
void ir_rx_parse_frame(rmt_symbol_word_t *rmt_nec_symbols, size_t symbol_num);