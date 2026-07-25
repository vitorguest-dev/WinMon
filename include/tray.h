#ifndef TRAY_H
#define TRAY_H

#include <windows.h>

// Prepara a estrutura NOTIFYICONDATA (chamado uma vez em WM_CREATE, não adiciona já o ícone)
void Tray_Inicializar(HWND hwnd);

// Esconde a janela e mostra o ícone no tray (chamado ao minimizar)
void Tray_Minimizar(HWND hwnd);

// Remove o ícone do tray e restaura a janela (chamado ao clicar no ícone)
void Tray_Restaurar(HWND hwnd);

// Atualiza o tooltip do ícone com CPU/RAM atuais (sem efeito se o ícone não estiver ativo)
void Tray_AtualizarTooltip(void);

// Remove o ícone do tray se estiver ativo (chamado em WM_DESTROY)
void Tray_Destruir(void);

#endif // TRAY_H
