#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <stdio.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")

#define TIMER_ID 1

// Variáveis globais para a Interface e PDH
HWND hEdit;
PDH_HQUERY hQuery;
PDH_HCOUNTER hCounterCPU;

// Variáveis para calcular a velocidade de rede (Bytes/sec)
DWORDLONG lastIn = 0;
DWORDLONG lastOut = 0;
int firstNetworkRead = 1;

// Função para formatar bytes num formato legível
void FormatarBytes(double bytes, char* buffer, size_t size) {
    if (bytes > 1048576) snprintf(buffer, size, "%.2f MB", bytes / 1048576.0);
    else if (bytes > 1024) snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    else snprintf(buffer, size, "%.0f B", bytes);
}

// Função principal de atualização de dados
void AtualizarMonitor() {
    char buffer[4096] = {0};
    char temp[256];
    
    // 1. CPU
    PDH_FMT_COUNTERVALUE counterVal;
    PdhCollectQueryData(hQuery);
    PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal);
    snprintf(temp, sizeof(temp), "=== [ PROCESSADOR ] ===\r\nUso Atual da CPU: %.1f%%\r\n\r\n", counterVal.doubleValue);
    strcat(buffer, temp);

    // 2. Memória RAM
    MEMORYSTATUSEX memInfo = { .dwLength = sizeof(MEMORYSTATUSEX) };
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        snprintf(temp, sizeof(temp), "=== [ MEMORIA RAM ] ===\r\nUso: %ld%% | Usada: %llu MB | Livre: %llu MB\r\n\r\n", 
                 memInfo.dwMemoryLoad, totalRAM - livreRAM, livreRAM);
        strcat(buffer, temp);
    }

    // 3. Rede (Download / Upload speed)
    ULONG outBufLen = 0;
    GetIfTable(NULL, &outBufLen, FALSE);
    PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(outBufLen);
    
    if (GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR) {
        DWORDLONG currentIn = 0, currentOut = 0;
        for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }
        
        if (!firstNetworkRead) {
            char downStr[32], upStr[32];
            FormatarBytes((double)(currentIn - lastIn), downStr, sizeof(downStr));
            FormatarBytes((double)(currentOut - lastOut), upStr, sizeof(upStr));
            snprintf(temp, sizeof(temp), "=== [ REDE ] ===\r\nDownload: %s/s | Upload: %s/s\r\n\r\n", downStr, upStr);
            strcat(buffer, temp);
        }
        
        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
    }
    free(pIfTable);
    snprintf(temp, sizeof(temp), "=== [ TOP PROCESSOS ATIVOS ] ===\r\n");
    strcat(buffer, temp);
    
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe32 = { .dwSize = sizeof(PROCESSENTRY32) };
        if (Process32First(hProcessSnap, &pe32)) {
            int count = 0;
            do {
                if (pe32.th32ProcessID != 0) { 
                    snprintf(temp, sizeof(temp), "PID: %-6u | Nome: %s\r\n", pe32.th32ProcessID, pe32.szExeFile);
                    strcat(buffer, temp);
                    count++;
                }
            } while (Process32Next(hProcessSnap, &pe32) && count < 15);
        }
        CloseHandle(hProcessSnap);
    }

    SetWindowTextA(hEdit, buffer);
}

// Procedimento da Janela (Trata os eventos)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            hEdit = CreateWindowEx(0, "EDIT", "A carregar dados...",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                                   10, 10, 460, 540, hwnd, NULL, NULL, NULL);
            
            HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                     FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

            PdhOpenQuery(NULL, 0, &hQuery);
            PdhAddEnglishCounter(hQuery, "\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);
            PdhCollectQueryData(hQuery);

            SetTimer(hwnd, TIMER_ID, 1000, NULL);
            return 0;

        case WM_TIMER:
            AtualizarMonitor();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PdhCloseQuery(hQuery);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "HardwareMonitorClass";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Monitor de Sistema (C Win32)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Janela fixa (sem redimensionamento para simplificar)
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 600,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);

    // Loop de Mensagens da Win32
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}