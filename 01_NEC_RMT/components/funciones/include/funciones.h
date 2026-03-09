#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/rmt_types.h"

// Prototipos de validación
bool funciones_check_in_range(uint32_t signal_duration, uint32_t spec_duration);
bool funciones_nec_parse_logic0(rmt_symbol_word_t *rmt_symbols);
bool funciones_nec_parse_logic1(rmt_symbol_word_t *rmt_symbols);

// Prototipos de parseo de frames completos
bool funciones_nec_parse_frame(rmt_symbol_word_t *rmt_symbols, uint16_t *addr, uint16_t *cmd);
void funciones_parse_nec_display(rmt_symbol_word_t *rmt_symbols, size_t symbol_num);