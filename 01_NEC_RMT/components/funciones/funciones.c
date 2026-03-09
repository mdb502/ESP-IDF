#include <stdio.h>
#include "funciones.h"
#include "config.h"

bool funciones_check_in_range(uint32_t signal_duration, uint32_t spec_duration) {
    return (signal_duration < (spec_duration + EXAMPLE_IR_NEC_DECODE_MARGIN)) &&
           (signal_duration > (spec_duration - EXAMPLE_IR_NEC_DECODE_MARGIN));
}

bool funciones_nec_parse_logic0(rmt_symbol_word_t *rmt_nec_symbols) {
    return funciones_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ZERO_DURATION_0) &&
           funciones_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ZERO_DURATION_1);
}

bool funciones_nec_parse_logic1(rmt_symbol_word_t *rmt_nec_symbols) {
    return funciones_check_in_range(rmt_nec_symbols->duration0, NEC_PAYLOAD_ONE_DURATION_0) &&
           funciones_check_in_range(rmt_nec_symbols->duration1, NEC_PAYLOAD_ONE_DURATION_1);
}

bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd) {
    rmt_symbol_word_t *cur = rmt_symbols;
    uint16_t address = 0;
    uint16_t command = 0;

    // Validar cabecera (Leading code)
    if (!funciones_check_in_range(cur->duration0, NEC_LEADING_CODE_DURATION_0) ||
        !funciones_check_in_range(cur->duration1, NEC_LEADING_CODE_DURATION_1)) {
        return false;
    }
    cur++;

    // Parsear Dirección (16 bits) e Instrucción (16 bits)
    for (int i = 0; i < 16; i++, cur++) {
        if (funciones_nec_parse_logic1(cur)) address |= (1 << i);
    }
    for (int i = 0; i < 16; i++, cur++) {
        if (funciones_nec_parse_logic1(cur)) command |= (1 << i);
    }

    *addr = address;
    *cmd = command;
    return true;
}

void funciones_parse_nec_display(rmt_symbol_word_t *rmt_symbols, size_t symbol_num) {
    uint16_t addr = 0, cmd = 0;
    if (symbol_num == 34 && funciones_nec_parse_frame(rmt_symbols, &addr, &cmd)) {
        printf("NEC Recibido: Addr=0x%04X, Cmd=0x%04X\n", addr, cmd);
    } else if (symbol_num == 2) {
        printf("NEC: Repeat Code\n");
    } else {
        printf("NEC: Frame Desconocido (%d símbolos)\n", symbol_num);
    }
}