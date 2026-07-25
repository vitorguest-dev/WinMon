#include <stdio.h>
#include <string.h>
#include "globals.h"
#include "tray.h"

void Tray_Inicializar(HWND hwnd) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strncpy_s(nid.szTip, sizeof(nid.szTip), "Monitor de Hardware", _TRUNCATE);
}

void Tray_Minimizar(HWND hwnd) {
    ShowWindow(hwnd, SW_HIDE);
    if (!trayIconAtivo) {
        Shell_NotifyIconA(NIM_ADD, &nid);
        trayIconAtivo = 1;
    }
}

void Tray_Restaurar(HWND hwnd) {
    if (trayIconAtivo) {
        Shell_NotifyIconA(NIM_DELETE, &nid);
        trayIconAtivo = 0;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

void Tray_AtualizarTooltip(void) {
    if (!trayIconAtivo) return;
    snprintf(nid.szTip, sizeof(nid.szTip), "Monitor de Hardware\r\nCPU: %.1f%% | RAM: %.0f%%",
             ultimoCpuPercent, ultimoRamPercent);
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void Tray_Destruir(void) {
    if (trayIconAtivo) {
        Shell_NotifyIconA(NIM_DELETE, &nid);
        trayIconAtivo = 0;
    }
}
