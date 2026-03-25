#ifndef CON_NTP_H
#define CON_NTP_H

/**
 * @brief Inicializa el servicio SNTP y sincroniza la hora local.
 * Esta función configura el servidor y aplica la zona horaria de Chile.
 */
void con_ntp_init(void);

#endif