#include <stdio.h>
#include "globals.h"
#include "utils.h"
#include "alertas.h"
#include "monitor_disco.h"

void MonitorarDiscoIO_Inicializar(PDH_HQUERY query) {
    PdhAddEnglishCounter(query, "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &hCounterDiskRead);
    PdhAddEnglishCounter(query, "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &hCounterDiskWrite);
}

void MonitorarDiscos(char *buffer, size_t size, size_t *offset) {
    *offset += snprintf(buffer + *offset, size - *offset, "=== [ DISCOS DE ARMAZENAMENTO ] ===\r\n");

    DWORD drives = GetLogicalDrives();
    char driveLetter[] = "A:\\";

    for (int i = 0; i < 26; i++) {
        if (drives & (1 << i)) {
            driveLetter[0] = 'A' + i;
            if (GetDriveTypeA(driveLetter) == DRIVE_FIXED) {
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
                if (GetDiskFreeSpaceExA(driveLetter, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
                    double totalGB = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                    double livreGB = (double)totalFreeBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);
                    double usadaGB = totalGB - livreGB;
                    double percentUsado = (usadaGB / totalGB) * 100.0;

                    long inicioLinha = (long)*offset;
                    *offset += snprintf(buffer + *offset, size - *offset,
                                        "Drive %s  Uso: %5.1f%%  (%.1f GB usad. de %.1f GB)\r\n",
                                        driveLetter, percentUsado, usadaGB, totalGB);
                    long fimLinha = (long)*offset;

                    if (percentUsado > LIMITE_DISCO_PERCENT) {
                        RegistarAlerta(inicioLinha, fimLinha);
                    }
                }
            }
        }
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
}

void MonitorarDiscoIO(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE readVal, writeVal;
    double bytesLeitura = 0.0, bytesEscrita = 0.0;

    if (hCounterDiskRead && PdhGetFormattedCounterValue(hCounterDiskRead, PDH_FMT_DOUBLE, NULL, &readVal) == ERROR_SUCCESS
        && readVal.CStatus == ERROR_SUCCESS) {
        bytesLeitura = readVal.doubleValue;
    }
    if (hCounterDiskWrite && PdhGetFormattedCounterValue(hCounterDiskWrite, PDH_FMT_DOUBLE, NULL, &writeVal) == ERROR_SUCCESS
        && writeVal.CStatus == ERROR_SUCCESS) {
        bytesEscrita = writeVal.doubleValue;
    }

    char leituraStr[32], escritaStr[32];
    FormatarBytes(bytesLeitura, leituraStr, sizeof(leituraStr));
    FormatarBytes(bytesEscrita, escritaStr, sizeof(escritaStr));

    *offset += snprintf(buffer + *offset, size - *offset,
                        "=== [ DISCO - VELOCIDADE I/O (TOTAL) ] ===\r\n"
                        "Leitura: %-10s/s | Escrita: %-10s/s\r\n\r\n",
                        leituraStr, escritaStr);
}
