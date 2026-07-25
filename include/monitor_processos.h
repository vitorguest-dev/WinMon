#ifndef MONITOR_PROCESSOS_H
#define MONITOR_PROCESSOS_H

#include <stddef.h>

// Faz snapshot dos processos, calcula % de CPU por processo (delta entre ciclos),
// e escreve no buffer o Top 12 por CPU e Top 5 por RAM.
void MonitorarProcessos(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_PROCESSOS_H
