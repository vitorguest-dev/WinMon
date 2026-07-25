#ifndef MONITOR_REDE_H
#define MONITOR_REDE_H

#include <stddef.h>

// Escreve no buffer a velocidade de download/upload (via GetIfTable)
void MonitorarRede(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_REDE_H
