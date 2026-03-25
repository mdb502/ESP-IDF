/*-------------------------------------
 Proyecto:		Control de luces
 Modulo:		config.c
 Version:		1.0 - 18.02.2026
 Objetivo:		Crea array de luces
 Descripcion:	Inicializa los datos de estructura de cada luz.
 				
 --------------------------------------
*/

#include "config.h"

volatile bool hora_sincronizada = false; // indicador de sincronización de tiempo, para intentos infinitos.

// Inicialización del array con datos específicos de luces
luz_t luces[] = {
    {
        .triac_pin = 16, 
        .switch_pin = 13, 
        .topic_base = "casa/exterior/switch07/", 
        .estado = false, 
        .modo = 1,
        .cant_actual = 0,
        .timeline_count = 0
    },
    {
        .triac_pin = 17, 
        .switch_pin = 14, 
        .topic_base = "casa/interior/switch03/", 
        .estado = false, 
        .modo = 1,
        .cant_actual = 0,
        .timeline_count = 0
    },
    {
        .triac_pin = 18, 
        .switch_pin = 21, 
        .topic_base = "casa/interior/switch05/", 
        .estado = false, 
        .modo = 1,
        .cant_actual = 0,
        .timeline_count = 0
    }
};

// Cálculo automático: Tamaño total en bytes dividido por el tamaño de una estructura
int TOTAL_LUCES = sizeof(luces) / sizeof(luz_t);
