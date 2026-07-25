#include <stdio.h>
#include "globals.h"
#include "monitor_sistema.h"

void MonitorarSistema(char *buffer, size_t size, size_t *offset) {
    ULONGLONG uptimeMs = GetTickCount64();
    int dias = (int)(uptimeMs / (1000ULL * 60 * 60 * 24));
    int horas = (int)((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int minutos = (int)((uptimeMs / (1000ULL * 60)) % 60);

    *offset += snprintf(buffer + *offset, size - *offset,
                        "=== [ SISTEMA & UPTIME ] ===\r\n"
                        "Tempo de Atividade: %d dias, %d horas, %d minutos\r\n",
                        dias, horas, minutos);

    SYSTEM_POWER_STATUS statusEnergia;
    if (GetSystemPowerStatus(&statusEnergia)) {
        if (statusEnergia.BatteryLifePercent != 255) {
            const char *fonte = (statusEnergia.ACLineStatus == 1) ? "Carregador Conectado" : "Em Bateria";
            *offset += snprintf(buffer + *offset, size - *offset,
                                "Energia: %d%% (%s)\r\n", statusEnergia.BatteryLifePercent, fonte);
        } else {
            *offset += snprintf(buffer + *offset, size - *offset, "Energia: Desktop (Sem Bateria)\r\n");
        }
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
}
