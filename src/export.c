#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "export.h"

void ExportarSnapshot(void) {
    SYSTEMTIME st;
    GetLocalTime(&st);

    char nomeFicheiro[MAX_PATH];
    snprintf(nomeFicheiro, sizeof(nomeFicheiro),
             "snapshot_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE *f = NULL;
    errno_t err = fopen_s(&f, nomeFicheiro, "wb");
    if (err == 0 && f != NULL) {
        fwrite(ultimoSnapshot, 1, strlen(ultimoSnapshot), f);
        fclose(f);

        char msg[MAX_PATH + 64];
        snprintf(msg, sizeof(msg), "Snapshot gravado como:\r\n%s", nomeFicheiro);
        MessageBoxA(hMainWindow, msg, "Export concluído", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxA(hMainWindow, "Não foi possível gravar o ficheiro de snapshot.", "Erro", MB_OK | MB_ICONERROR);
    }
}
