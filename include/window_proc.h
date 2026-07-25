#ifndef WINDOW_PROC_H
#define WINDOW_PROC_H

#include <windows.h>

// Procedimento de janela principal (tratamento de todas as mensagens)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Corre um ciclo completo de recolha de dados e atualização da janela (chamado por WM_TIMER)
void AtualizarMonitor(void);

#endif // WINDOW_PROC_H
