#include <richedit.h>
#include "globals.h"
#include "window_proc.h"
#include "alertas.h"
#include "tray.h"
#include "export.h"
#include "monitor_sistema.h"
#include "monitor_cpu.h"
#include "monitor_ram.h"
#include "monitor_disco.h"
#include "monitor_rede.h"
#include "monitor_processos.h"

// Monta os dados de todos os módulos, atualiza a janela e aplica alertas/tooltip/título.
// A ordem das chamadas importa: MonitorarCPU tem de vir antes de MonitorarDiscoIO,
// porque é o PdhCollectQueryData dentro de MonitorarCPU que atualiza todos os
// contadores PDH partilhados (CPU total, por núcleo, E disco I/O).
void AtualizarMonitor(void) {
    static char buffer[BUFFER_SIZE];
    size_t offset = 0;

    totalIntervalosAlerta = 0;
    alertaGlobalAtivo = 0;

    MonitorarSistema(buffer, BUFFER_SIZE, &offset);
    MonitorarCPU(buffer, BUFFER_SIZE, &offset);
    MonitorarRAM(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscos(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscoIO(buffer, BUFFER_SIZE, &offset);
    MonitorarRede(buffer, BUFFER_SIZE, &offset);
    MonitorarProcessos(buffer, BUFFER_SIZE, &offset);

    // Guarda cópia para o export de snapshot (Ctrl+S)
    strncpy_s(ultimoSnapshot, BUFFER_SIZE, buffer, _TRUNCATE);

    // Atualiza o controlo da janela numa única operação
    SetWindowTextA(hEdit, buffer);

    // Aplica cores de alerta (RichEdit) e atualiza título/tooltip
    AplicarCoresDeAlerta();
    AtualizarTituloJanela();
    Tray_AtualizarTooltip();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            hMainWindow = hwnd;

            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            numProcessadores = (int)sysInfo.dwNumberOfProcessors;
            if (numProcessadores < 1) numProcessadores = 1;

            RECT rc;
            GetClientRect(hwnd, &rc);

            // Carrega a biblioteca do RichEdit (necessária para o controlo RICHEDIT50W)
            hRichEditLib = LoadLibraryA("Msftedit.dll");

            hEdit = CreateWindowExA(0, "RICHEDIT50W", "A recolher dados do sistema...",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                                   10, 10, rc.right - 20, rc.bottom - 20, hwnd, NULL, NULL, NULL);

            hFontMonitor = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMonitor, TRUE);
            // Limite alto de texto (por omissão o RichEdit corta ~32KB em alguns casos)
            SendMessage(hEdit, EM_EXLIMITTEXT, 0, (LPARAM)(BUFFER_SIZE * 2));

            // Inicialização do PDH: abre a query partilhada e cada módulo regista os seus contadores
            PdhOpenQuery(NULL, 0, &hQuery);
            MonitorarCPU_Inicializar(hQuery);
            MonitorarDiscoIO_Inicializar(hQuery);
            PdhCollectQueryData(hQuery);

            // Preparação do ícone do tray (só é adicionado quando a janela é minimizada)
            Tray_Inicializar(hwnd);

            // Timer de 1000ms (1 segundo)
            SetTimer(hwnd, TIMER_ID, 1000, NULL);
            return 0;
        }

        case WM_SIZE: {
            // Redimensiona o controlo de edição para acompanhar o tamanho da janela
            if (hEdit != NULL) {
                int largura = LOWORD(lParam);
                int altura = HIWORD(lParam);
                MoveWindow(hEdit, 10, 10, largura - 20, altura - 20, TRUE);
            }
            return 0;
        }

        case WM_GETMINMAXINFO: {
            // Define um tamanho mínimo razoável para a janela
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            mmi->ptMinTrackSize.x = 400;
            mmi->ptMinTrackSize.y = 300;
            return 0;
        }

        case WM_SYSCOMMAND: {
            // Intercepta o botão de minimizar: esconde a janela e mostra o ícone no tray.
            // O botão de fechar (X) continua a ter o comportamento normal (não passa por aqui).
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                Tray_Minimizar(hwnd);
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_TRAYICON: {
            // Clique (simples ou duplo) no ícone do tray restaura a janela
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                Tray_Restaurar(hwnd);
            }
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == ID_EXPORT_SNAPSHOT) {
                ExportarSnapshot();
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_TIMER:
            AtualizarMonitor();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PdhCloseQuery(hQuery);
            if (hFontMonitor != NULL) {
                DeleteObject(hFontMonitor);
                hFontMonitor = NULL;
            }
            Tray_Destruir();
            if (hRichEditLib != NULL) {
                FreeLibrary(hRichEditLib);
                hRichEditLib = NULL;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
