#include <stdio.h>
#include "globals.h"
#include "alertas.h"
#include "monitor_ram.h"

void MonitorarRAM(char *buffer, size_t size, size_t *offset) {
    MEMORYSTATUSEX memInfo = {.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usadaRAM = totalRAM - livreRAM;
        ultimoRamPercent = (double)memInfo.dwMemoryLoad;

        long inicioLinha = (long)*offset;
        *offset += snprintf(buffer + *offset, size - *offset,
                            "=== [ MEMORIA RAM GERAL ] ===\r\n"
                            "Uso: %ld%% | Usada: %llu MB | Livre: %llu MB (Total: %llu MB)\r\n\r\n",
                            memInfo.dwMemoryLoad, usadaRAM, livreRAM, totalRAM);
        long fimLinha = (long)*offset;

        if (memInfo.dwMemoryLoad > LIMITE_RAM_PERCENT) {
            RegistarAlerta(inicioLinha, fimLinha);
        }
    }
}
