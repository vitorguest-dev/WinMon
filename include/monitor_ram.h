#ifndef MONITOR_RAM_H
#define MONITOR_RAM_H

#include <stddef.h>

// Escreve no buffer a secção de RAM e regista alerta se o uso > LIMITE_RAM_PERCENT
void MonitorarRAM(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_RAM_H
