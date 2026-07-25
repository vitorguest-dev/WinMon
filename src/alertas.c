#include <richedit.h>
#include "globals.h"
#include "alertas.h"

void RegistarAlerta(long inicio, long fim) {
    if (totalIntervalosAlerta < MAX_ALERT_RANGES) {
        intervalosAlerta[totalIntervalosAlerta].inicio = inicio;
        intervalosAlerta[totalIntervalosAlerta].fim = fim;
        totalIntervalosAlerta++;
    }
    alertaGlobalAtivo = 1;
}

void AplicarCoresDeAlerta(void) {
    CHARFORMAT2A cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;

    // 1. Repõe a cor preta em todo o texto
    SendMessage(hEdit, EM_SETSEL, 0, -1);
    cf.crTextColor = RGB(0, 0, 0);
    cf.dwEffects = 0;
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    // 2. Colore de vermelho cada intervalo em alerta
    cf.crTextColor = RGB(200, 0, 0);
    for (int i = 0; i < totalIntervalosAlerta; i++) {
        SendMessage(hEdit, EM_SETSEL, intervalosAlerta[i].inicio, intervalosAlerta[i].fim);
        SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }

    // 3. Remove a seleção visível (volta ao início, sem texto selecionado)
    SendMessage(hEdit, EM_SETSEL, 0, 0);
}

void AtualizarTituloJanela(void) {
    if (alertaGlobalAtivo) {
        SetWindowTextA(hMainWindow, "Monitor de Hardware & Sistema (Win32) v6  —  [!] ALERTA: RAM ou disco acima do limite");
    } else {
        SetWindowTextA(hMainWindow, "Monitor de Hardware & Sistema (Win32) v6");
    }
}
