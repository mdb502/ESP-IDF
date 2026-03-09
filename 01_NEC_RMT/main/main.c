#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ir_tx.h"
#include "ir_rx.h"
#include "funciones.h"
#include "config.h"

static const char *TAG = "MAIN_APP";

void app_main(void) {
    rmt_channel_handle_t tx_chan = NULL;
    rmt_channel_handle_t rx_chan = NULL;
    rmt_encoder_handle_t nec_encoder = NULL;
    QueueHandle_t rx_queue = NULL;

    // 1. Inicialización de componentes
    ESP_LOGI(TAG, "Inicializando periféricos IR...");
    ESP_ERROR_CHECK(ir_tx_init(&tx_chan, &nec_encoder));
    ESP_ERROR_CHECK(ir_rx_init(&rx_chan, &rx_queue));

    // 2. Habilitar canales
    ESP_ERROR_CHECK(rmt_enable(tx_chan));
    ESP_ERROR_CHECK(rmt_enable(rx_chan));

    // 3. Preparar recepción
    rmt_symbol_word_t raw_symbols[64];
    rmt_rx_done_event_data_t rx_data;
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 12000000,
    };

    ESP_LOGI(TAG, "Sistema listo. Esperando señal IR o transmitiendo cada 1s.");
    ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg));

    while (1) {
        // Esperamos 1 segundo por una señal IR
        if (xQueueReceive(rx_queue, &rx_data, pdMS_TO_TICKS(1000)) == pdPASS) {
            
            // Usamos la lógica de visualización que movimos a 'funciones'
            funciones_parse_nec_display(rx_data.received_symbols, rx_data.num_symbols);
            
            // Reiniciar escucha
            ESP_ERROR_CHECK(rmt_receive(rx_chan, raw_symbols, sizeof(raw_symbols), &receive_cfg));
        } else {
            // Si no recibe nada (Timeout), transmite un comando de prueba
            const ir_nec_scan_code_t scan_code = {
                .address = 0x7C87,
                .command = 0x7F80,
            };
            rmt_transmit_config_t transmit_cfg = {.loop_count = 0};
            ESP_ERROR_CHECK(rmt_transmit(tx_chan, nec_encoder, &scan_code, sizeof(scan_code), &transmit_cfg));
            ESP_LOGD(TAG, "Comando de prueba enviado");
        }
    }
}