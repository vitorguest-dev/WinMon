#include <stdio.h>
#include "globals.h"
#include "monitor_cpu.h"

void MonitorarCPU_Inicializar(PDH_HQUERY query) {
    PdhAddEnglishCounter(query, "\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);

    // Contador por núcleo (um por processador lógico, até MAX_CORES)
    numNucleosMonitorizados = (numProcessadores < MAX_CORES) ? numProcessadores : MAX_CORES;
    for (int i = 0; i < numNucleosMonitorizados; i++) {
        char pathContador[64];
        snprintf(pathContador, sizeof(pathContador), "\\Processor(%d)\\%% Processor Time", i);
        PdhAddEnglishCounter(query, pathContador, 0, &hCounterCoresCPU[i]);
    }
}

void MonitorarCPU(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE counterVal;
    // Este PdhCollectQueryData atualiza TODOS os contadores da query (total, por núcleo, disco I/O)
    PdhCollectQueryData(hQuery);
    PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal);

    double cpuAtual = (counterVal.CStatus == ERROR_SUCCESS) ? counterVal.doubleValue : 0.0;
    ultimoCpuPercent = cpuAtual;

    *offset += snprintf(buffer + *offset, size - *offset,
                        "=== [ PROCESSADOR ] ===\r\n"
                        "Uso Atual da CPU (Total): %.1f%%\r\n",
                        cpuAtual);

    // Detalhe por núcleo, 4 valores por linha para não ficar demasiado comprido
    if (numNucleosMonitorizados > 0) {
        *offset += snprintf(buffer + *offset, size - *offset, "Por Nucleo: ");
        for (int i = 0; i < numNucleosMonitorizados; i++) {
            PDH_FMT_COUNTERVALUE coreVal;
            double corePercent = 0.0;
            if (PdhGetFormattedCounterValue(hCounterCoresCPU[i], PDH_FMT_DOUBLE, NULL, &coreVal) == ERROR_SUCCESS
                && coreVal.CStatus == ERROR_SUCCESS) {
                corePercent = coreVal.doubleValue;
            }
            *offset += snprintf(buffer + *offset, size - *offset, "C%d:%5.1f%%  ", i, corePercent);
            if ((i + 1) % 4 == 0 && (i + 1) < numNucleosMonitorizados) {
                *offset += snprintf(buffer + *offset, size - *offset, "\r\n            ");
            }
        }
        *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
}
