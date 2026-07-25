#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Vinculação automática de bibliotecas para MSVC (no GCC é feito via flags)
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")

#define TIMER_ID 1
#define BUFFER_SIZE 16384 // Buffer alargado para 16KB para evitar qualquer overflow
#define MAX_PROCESSES 2048

// Estrutura para armazenar dados de cada processo na ordenação
typedef struct {
    DWORD pid;
    SIZE_T memUsageMB;
    double cpuPercent;
    char exeFile[MAX_PATH];
} ProcessoInfo;

// Estrutura para guardar os tempos de CPU anteriores de cada processo (para calcular delta)
typedef struct {
    DWORD pid;
    ULONGLONG lastKernelTime;
    ULONGLONG lastUserTime;
    int valido;
} ProcessoCpuHistorico;

#define MAX_HISTORICO 2048
static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;

// Variáveis Globais de Estado
HWND hEdit;
PDH_HQUERY hQuery;
PDH_HCOUNTER hCounterCPU;
HFONT hFontMonitor = NULL;
DWORDLONG lastIn = 0;
DWORDLONG lastOut = 0;
int firstNetworkRead = 1;
int numProcessadores = 1;

// Buffer estático para armazenar a lista de processos sem alocação dinâmica no heap a cada ciclo
static ProcessoInfo listaProcessos[MAX_PROCESSES];

// Helper para formatar tamanhos de bytes em unidades legíveis
void FormatarBytes(double bytes, char *buffer, size_t size) {
    if (bytes >= 1073741824.0) snprintf(buffer, size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576.0) snprintf(buffer, size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024.0) snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    else snprintf(buffer, size, "%.0f B", bytes);
}

// Converte FILETIME para um único valor de 64 bits (100-ns intervals)
static ULONGLONG FileTimeToU64(FILETIME ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Procura o histórico de CPU de um PID; devolve NULL se não existir
static ProcessoCpuHistorico *EncontrarHistorico(DWORD pid) {
    for (int i = 0; i < totalHistorico; i++) {
        if (historicoCpu[i].pid == pid) return &historicoCpu[i];
    }
    return NULL;
}

// Comparador para o qsort (ordem decrescente de RAM)
int CompararProcessosPorRAM(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->memUsageMB < p2->memUsageMB) return 1;
    if (p1->memUsageMB > p2->memUsageMB) return -1;
    return 0;
}

// Comparador para o qsort (ordem decrescente de CPU)
int CompararProcessosPorCPU(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->cpuPercent < p2->cpuPercent) return 1;
    if (p1->cpuPercent > p2->cpuPercent) return -1;
    return 0;
}

// 1. Informações Básicas do Sistema, Uptime e Bateria
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

// 2. Leitura da CPU via PDH
void MonitorarCPU(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE counterVal;
    PdhCollectQueryData(hQuery);
    PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal);

    *offset += snprintf(buffer + *offset, size - *offset,
                        "=== [ PROCESSADOR ] ===\r\n"
                        "Uso Atual da CPU: %.1f%%\r\n\r\n",
                        (counterVal.CStatus == ERROR_SUCCESS) ? counterVal.doubleValue : 0.0);
}

// 3. Memória RAM Global
void MonitorarRAM(char *buffer, size_t size, size_t *offset) {
    MEMORYSTATUSEX memInfo = {.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usadaRAM = totalRAM - livreRAM;

        *offset += snprintf(buffer + *offset, size - *offset,
                            "=== [ MEMORIA RAM GERAL ] ===\r\n"
                            "Uso: %ld%% | Usada: %llu MB | Livre: %llu MB (Total: %llu MB)\r\n\r\n",
                            memInfo.dwMemoryLoad, usadaRAM, livreRAM, totalRAM);
    }
}

// 4. Discos de Armazenamento
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

                    *offset += snprintf(buffer + *offset, size - *offset,
                                        "Drive %s  Uso: %5.1f%%  (%.1f GB usad. de %.1f GB)\r\n",
                                        driveLetter, percentUsado, usadaGB, totalGB);
                }
            }
        }
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
}

// 5. Tráfego de Rede (GetIfTable — API clássica, mas amplamente suportada em qualquer toolchain)
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

// 6. Processos + Ordenação por RAM e cálculo de % CPU por processo
void MonitorarProcessos(char *buffer, size_t size, size_t *offset) {
    *offset += snprintf(buffer + *offset, size - *offset, "=== [ TOP 12 PROCESSOS (MAIOR CONSUMO CPU) ] ===\r\n");

    // Tempo de sistema atual (para calcular delta de tempo decorrido)
    FILETIME ftIdle, ftKernelSys, ftUserSys;
    GetSystemTimes(&ftIdle, &ftKernelSys, &ftUserSys);
    ULONGLONG currentSystemTime = FileTimeToU64(ftKernelSys) + FileTimeToU64(ftUserSys);
    ULONGLONG deltaSystemTime = (lastSystemTime != 0) ? (currentSystemTime - lastSystemTime) : 0;

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe32 = {.dwSize = sizeof(PROCESSENTRY32)};
    int totalProcessos = 0;
    ProcessoCpuHistorico novoHistorico[MAX_HISTORICO];
    int totalNovoHistorico = 0;

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            if (pe32.th32ProcessID != 0 && totalProcessos < MAX_PROCESSES) {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                SIZE_T memUsageMB = 0;
                double cpuPercent = 0.0;

                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        memUsageMB = pmc.WorkingSetSize / (1024 * 1024);
                    }

                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        ULONGLONG kernelU64 = FileTimeToU64(ftKernel);
                        ULONGLONG userU64 = FileTimeToU64(ftUser);
                        ULONGLONG totalProcTime = kernelU64 + userU64;

                        ProcessoCpuHistorico *hist = EncontrarHistorico(pe32.th32ProcessID);
                        if (hist && deltaSystemTime > 0) {
                            ULONGLONG deltaProc = totalProcTime - (hist->lastKernelTime + hist->lastUserTime);
                            // Percentagem relativa ao total do sistema, multiplicada pelo nº de núcleos
                            cpuPercent = ((double)deltaProc / (double)deltaSystemTime) * 100.0 * numProcessadores;
                            if (cpuPercent < 0.0) cpuPercent = 0.0;
                            if (cpuPercent > 100.0 * numProcessadores) cpuPercent = 100.0 * numProcessadores;
                        }

                        // Guarda para o próximo ciclo
                        if (totalNovoHistorico < MAX_HISTORICO) {
                            novoHistorico[totalNovoHistorico].pid = pe32.th32ProcessID;
                            novoHistorico[totalNovoHistorico].lastKernelTime = kernelU64;
                            novoHistorico[totalNovoHistorico].lastUserTime = userU64;
                            novoHistorico[totalNovoHistorico].valido = 1;
                            totalNovoHistorico++;
                        }
                    }
                    CloseHandle(hProcess);
                }

                listaProcessos[totalProcessos].pid = pe32.th32ProcessID;
                listaProcessos[totalProcessos].memUsageMB = memUsageMB;
                listaProcessos[totalProcessos].cpuPercent = cpuPercent;
                strncpy_s(listaProcessos[totalProcessos].exeFile, MAX_PATH, pe32.szExeFile, _TRUNCATE);

                totalProcessos++;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }
    CloseHandle(hProcessSnap);

    // Atualiza o histórico global para o próximo ciclo
    memcpy(historicoCpu, novoHistorico, sizeof(ProcessoCpuHistorico) * totalNovoHistorico);
    totalHistorico = totalNovoHistorico;
    lastSystemTime = currentSystemTime;

    // Ordena por CPU (mais relevante que RAM para identificar picos de uso)
    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo), CompararProcessosPorCPU);

    int limite = (totalProcessos < 12) ? totalProcessos : 12;
    for (int i = 0; i < limite; i++) {
        *offset += snprintf(buffer + *offset, size - *offset,
                            "PID: %-6u | CPU: %5.1f%% | RAM: %5lu MB | %s\r\n",
                            listaProcessos[i].pid, listaProcessos[i].cpuPercent,
                            (unsigned long)listaProcessos[i].memUsageMB, listaProcessos[i].exeFile);
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");

    // Também mostra o top 5 por RAM, para não perder essa informação
    *offset += snprintf(buffer + *offset, size - *offset, "=== [ TOP 5 PROCESSOS (MAIOR CONSUMO RAM) ] ===\r\n");
    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo), CompararProcessosPorRAM);
    int limiteRAM = (totalProcessos < 5) ? totalProcessos : 5;
    for (int i = 0; i < limiteRAM; i++) {
        *offset += snprintf(buffer + *offset, size - *offset,
                            "PID: %-6u | RAM: %5lu MB | CPU: %5.1f%% | %s\r\n",
                            listaProcessos[i].pid, (unsigned long)listaProcessos[i].memUsageMB,
                            listaProcessos[i].cpuPercent, listaProcessos[i].exeFile);
    }
}

// Função principal de montagem dos dados e envio para a janela
void AtualizarMonitor() {
    static char buffer[BUFFER_SIZE];
    size_t offset = 0;

    MonitorarSistema(buffer, BUFFER_SIZE, &offset);
    MonitorarCPU(buffer, BUFFER_SIZE, &offset);
    MonitorarRAM(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscos(buffer, BUFFER_SIZE, &offset);
    MonitorarRede(buffer, BUFFER_SIZE, &offset);
    MonitorarProcessos(buffer, BUFFER_SIZE, &offset);

    // Atualiza o controlo da janela numa única operação
    SetWindowTextA(hEdit, buffer);
}

// Trata os Eventos da Janela (Win32 Message Loop)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            numProcessadores = (int)sysInfo.dwNumberOfProcessors;
            if (numProcessadores < 1) numProcessadores = 1;

            RECT rc;
            GetClientRect(hwnd, &rc);

            hEdit = CreateWindowEx(0, "EDIT", "A recolher dados do sistema...",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                                   10, 10, rc.right - 20, rc.bottom - 20, hwnd, NULL, NULL, NULL);

            hFontMonitor = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     FIXED_PITCH | FF_MODERN, "Consolas");
            SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMonitor, TRUE);

            // Inicialização do PDH
            PdhOpenQuery(NULL, 0, &hQuery);
            PdhAddEnglishCounter(hQuery, "\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);
            PdhCollectQueryData(hQuery);

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
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // DPI awareness: evita que a janela fique desfocada em ecrãs de alto DPI.
    // SetProcessDpiAwarenessContext é a API mais recente (Windows 10 1703+);
    // se não existir (versões antigas), cai-se em SetProcessDPIAware como fallback.
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiCtxFunc)(DPI_AWARENESS_CONTEXT);
        SetDpiCtxFunc pSetDpiCtx = (SetDpiCtxFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpiCtx) {
            pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }

    const char CLASS_NAME[] = "HardwareMonitorClassV4";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // WS_THICKFRAME e WS_MAXIMIZEBOX adicionados para permitir redimensionar a janela
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Monitor de Hardware & Sistema (Win32) v4",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 616, 780,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}