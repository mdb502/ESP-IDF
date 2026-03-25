// components/ir_rx/ir_rx.c
#include "ir_rx.h"
#include "config.h"
#include "funciones.h"
#include "esp_log.h"
#include "driver/rmt_rx.h"


static bool example_rmt_rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data) {
    BaseType_t high_task_wakeup = pdFALSE;
    xQueueSendFromISR((QueueHandle_t)user_data, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

void ir_rx_parse_frame(rmt_symbol_word_t *rmt_nec_symbols, size_t symbol_num) {
    // Aquí implementas la lógica de 'example_parse_nec_frame' 
    // Usando las funciones del componente 'funciones' (ej: funciones_nec_parse_logic1)
    // ...
}


esp_err_t ir_rx_init(rmt_channel_handle_t *rx_chan, QueueHandle_t *rx_queue) {
    rmt_rx_channel_config_t rx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
        .mem_block_symbols = 64, 
        .gpio_num = EXAMPLE_IR_RX_GPIO_NUM,
    };

    // 1. Crear el canal
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, rx_chan));

    // 2. Crear la cola y registrar callbacks
    *rx_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = example_rmt_rx_done_callback
    };
    
    // Devolvemos el resultado del registro de callbacks
    return rmt_rx_register_event_callbacks(*rx_chan, &cbs, *rx_queue);
    
    // --- BLOQUE 3 ELIMINADO ---
    // Ya no habilitamos aquí para evitar el mensaje "channel not in init state"
}
