#include "globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <iphlpapi.h>
#include "utils.h"
#include "monitor_rede.h"

void MonitorarRede(char *buffer, size_t size, size_t *offset) {
    ULONG outBufLen = 0;
    GetIfTable(NULL, &outBufLen, FALSE);
    PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(outBufLen);

    if (pIfTable && GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR) {
        DWORDLONG currentIn = 0, currentOut = 0;
        for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }

        if (!firstNetworkRead) {
            char downStr[32], upStr[32];
            FormatarBytes((double)(currentIn - lastIn), downStr, sizeof(downStr));
            FormatarBytes((double)(currentOut - lastOut), upStr, sizeof(upStr));

            *offset += snprintf(buffer + *offset, size - *offset,
                                "=== [ REDE EM TEMPO REAL ] ===\r\n"
                                "Download: %-10s/s | Upload: %-10s/s\r\n\r\n",
                                downStr, upStr);
        }

        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
    }
    if (pIfTable) free(pIfTable);
}
