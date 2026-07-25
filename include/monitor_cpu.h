#ifndef MONITOR_CPU_H
#define MONITOR_CPU_H

#include <stddef.h>
#include <pdh.h>

// Regista na query PDH partilhada os contadores de CPU (total + um por núcleo lógico).
// Deve ser chamado uma vez em WM_CREATE, ANTES do primeiro PdhCollectQueryData.
void MonitorarCPU_Inicializar(PDH_HQUERY query);

// Atualiza TODOS os contadores da query PDH partilhada (CPU total, por núcleo, disco I/O)
// e escreve no buffer a secção de CPU. Por causa deste PdhCollectQueryData interno,
// esta função deve ser chamada ANTES de MonitorarDiscoIO no ciclo de atualização.
void MonitorarCPU(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_CPU_H
