#ifndef MONITOR_SISTEMA_H
#define MONITOR_SISTEMA_H

#include <stddef.h>

// Escreve no buffer a secção de uptime e estado da bateria
void MonitorarSistema(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_SISTEMA_H
