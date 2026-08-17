#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Vinculação automática de bibliotecas para MSVC (no GCC é feito via flags)
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

#define TIMER_ID 1
#define BUFFER_SIZE 16384 // Buffer alargado para 16KB para evitar qualquer overflow
#define MAX_PROCESSES 2048
#define MAX_ALERT_RANGES 32
#define ID_EXPORT_SNAPSHOT 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define MAX_CORES 64 // Limite de núcleos monitorizados individualmente (suficiente para uso doméstico)

// Limites que disparam alerta visual
#define LIMITE_RAM_PERCENT 90.0
#define LIMITE_DISCO_PERCENT 95.0

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

// Um intervalo de texto (início/fim em caracteres) que deve ser colorido de vermelho
typedef struct {
    long inicio;
    long fim;
} IntervaloAlerta;

#define MAX_HISTORICO 2048
static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;

// Variáveis Globais de Estado
HWND hMainWindow = NULL;
HWND hEdit;
PDH_HQUERY hQuery;
PDH_HCOUNTER hCounterCPU;
PDH_HCOUNTER hCounterCoresCPU[MAX_CORES];
PDH_HCOUNTER hCounterDiskRead = NULL;
PDH_HCOUNTER hCounterDiskWrite = NULL;
HFONT hFontMonitor = NULL;
HMODULE hRichEditLib = NULL;
NOTIFYICONDATAA nid;
int trayIconAtivo = 0;
DWORDLONG lastIn = 0;
DWORDLONG lastOut = 0;
int firstNetworkRead = 1;
int numProcessadores = 1;
int numNucleosMonitorizados = 0; // min(numProcessadores, MAX_CORES) — limita o array de contadores por núcleo

// Estado do último ciclo, usado para o export de snapshot (Ctrl+S) e para o tooltip do tray
static char ultimoSnapshot[BUFFER_SIZE];
static double ultimoRamPercent = 0.0;
static double ultimoCpuPercent = 0.0;

// Intervalos de alerta detetados no ciclo atual (para colorir a vermelho)
static IntervaloAlerta intervalosAlerta[MAX_ALERT_RANGES];
static int totalIntervalosAlerta = 0;
static int alertaGlobalAtivo = 0;

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

// Regista um intervalo de texto [inicio, fim) a colorir de vermelho por estar em alerta
static void RegistarAlerta(long inicio, long fim) {
    if (totalIntervalosAlerta < MAX_ALERT_RANGES) {
        intervalosAlerta[totalIntervalosAlerta].inicio = inicio;
        intervalosAlerta[totalIntervalosAlerta].fim = fim;
        totalIntervalosAlerta++;
    }
    alertaGlobalAtivo = 1;
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

// 2. Leitura da CPU via PDH (total + detalhe por núcleo)
void MonitorarCPU(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE counterVal;
    // Este PdhCollectQueryData atualiza TODOS os contadores da query (total, por núcleo, disco I/O)
    PdhCollectQueryData(hQuery);
    PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal);

    double cpuAtual = (counterVal.CStatus == ERROR_SUCCESS) ? counterVal.doubleValue : 0.0;
    ultimoCpuPercent = cpuAtual;

    *offset += snprintf(buffer + *offset, size - *offset,
                        "=== [ PROCESSADOR ] ===\r\n"
                        "Uso Atual da CPU (Total): %.1f%%\r\n",
                        cpuAtual);

    // Detalhe por núcleo, 4 valores por linha para não ficar demasiado comprido
    if (numNucleosMonitorizados > 0) {
        *offset += snprintf(buffer + *offset, size - *offset, "Por Nucleo: ");
        for (int i = 0; i < numNucleosMonitorizados; i++) {
            PDH_FMT_COUNTERVALUE coreVal;
            double corePercent = 0.0;
            if (PdhGetFormattedCounterValue(hCounterCoresCPU[i], PDH_FMT_DOUBLE, NULL, &coreVal) == ERROR_SUCCESS
                && coreVal.CStatus == ERROR_SUCCESS) {
                corePercent = coreVal.doubleValue;
            }
            *offset += snprintf(buffer + *offset, size - *offset, "C%d:%5.1f%%  ", i, corePercent);
            if ((i + 1) % 4 == 0 && (i + 1) < numNucleosMonitorizados) {
                *offset += snprintf(buffer + *offset, size - *offset, "\r\n            ");
            }
        }
        *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");
}

// Velocidade de leitura/escrita de disco (total de todos os discos físicos), via PDH
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

// 3. Memória RAM Global (com deteção de alerta se uso > LIMITE_RAM_PERCENT)
void MonitorarRAM(char *buffer, size_t size, size_t *offset) {
    MEMORYSTATUSEX memInfo = {.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usadaRAM = totalRAM - livreRAM;
        ultimoRamPercent = (double)memInfo.dwMemoryLoad;

        long inicioLinha = (long)*offset;
        *offset += snprintf(buffer + *offset, size - *offset,
                            "=== [ MEMORIA RAM GERAL ] ===\r\n"
                            "Uso: %ld%% | Usada: %llu MB | Livre: %llu MB (Total: %llu MB)\r\n\r\n",
                            memInfo.dwMemoryLoad, usadaRAM, livreRAM, totalRAM);
        long fimLinha = (long)*offset;

        if (memInfo.dwMemoryLoad > LIMITE_RAM_PERCENT) {
            RegistarAlerta(inicioLinha, fimLinha);
        }
    }
}

// 4. Discos de Armazenamento (com deteção de alerta por disco se uso > LIMITE_DISCO_PERCENT)
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

// 5. Tráfego de Rede (GetIfTable — API clássica, amplamente suportada em qualquer toolchain)
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

// Aplica a coloração de vermelho aos intervalos de alerta detetados neste ciclo,
// e repõe a cor por omissão (preto) no resto do texto.
void AplicarCoresDeAlerta() {
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

// Atualiza o título da janela principal para refletir se há algum alerta ativo
void AtualizarTituloJanela() {
    if (alertaGlobalAtivo) {
        SetWindowTextA(hMainWindow, "Monitor de Hardware & Sistema (Win32) v5  —  [!] ALERTA: RAM ou disco acima do limite");
    } else {
        SetWindowTextA(hMainWindow, "Monitor de Hardware & Sistema (Win32) v5");
    }
}

// Atualiza o tooltip do ícone do tray com CPU/RAM atuais (só relevante quando minimizado para o tray)
void AtualizarTooltipTray() {
    if (!trayIconAtivo) return;
    snprintf(nid.szTip, sizeof(nid.szTip), "Monitor de Hardware\r\nCPU: %.1f%% | RAM: %.0f%%",
             ultimoCpuPercent, ultimoRamPercent);
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

// Grava o snapshot atual (texto simples, sem formatação) num ficheiro .txt com timestamp
void ExportarSnapshot() {
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

// Função principal de montagem dos dados e envio para a janela
void AtualizarMonitor() {
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
    AtualizarTooltipTray();
}

// Trata os Eventos da Janela (Win32 Message Loop)
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

            // Inicialização do PDH
            PdhOpenQuery(NULL, 0, &hQuery);
            PdhAddEnglishCounter(hQuery, "\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);

            // Contador por núcleo (um por processador lógico, até MAX_CORES)
            numNucleosMonitorizados = (numProcessadores < MAX_CORES) ? numProcessadores : MAX_CORES;
            for (int i = 0; i < numNucleosMonitorizados; i++) {
                char pathContador[64];
                snprintf(pathContador, sizeof(pathContador), "\\Processor(%d)\\%% Processor Time", i);
                PdhAddEnglishCounter(hQuery, pathContador, 0, &hCounterCoresCPU[i]);
            }

            // Contadores de velocidade de leitura/escrita em disco (todos os discos físicos)
            PdhAddEnglishCounter(hQuery, "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &hCounterDiskRead);
            PdhAddEnglishCounter(hQuery, "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &hCounterDiskWrite);

            PdhCollectQueryData(hQuery);

            // Preparação do ícone do tray (só é adicionado quando a janela é minimizada)
            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = sizeof(NOTIFYICONDATAA);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            strncpy_s(nid.szTip, sizeof(nid.szTip), "Monitor de Hardware", _TRUNCATE);

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
                ShowWindow(hwnd, SW_HIDE);
                if (!trayIconAtivo) {
                    Shell_NotifyIconA(NIM_ADD, &nid);
                    trayIconAtivo = 1;
                }
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_TRAYICON: {
            // Clique (simples ou duplo) no ícone do tray restaura a janela
            if (lParam == WM_LBUTTONDBLCLK || lParam == WM_LBUTTONUP) {
                if (trayIconAtivo) {
                    Shell_NotifyIconA(NIM_DELETE, &nid);
                    trayIconAtivo = 0;
                }
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
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
            if (trayIconAtivo) {
                Shell_NotifyIconA(NIM_DELETE, &nid);
                trayIconAtivo = 0;
            }
            if (hRichEditLib != NULL) {
                FreeLibrary(hRichEditLib);
                hRichEditLib = NULL;
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

    const char CLASS_NAME[] = "HardwareMonitorClassV5";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // WS_THICKFRAME e WS_MAXIMIZEBOX adicionados para permitir redimensionar a janela
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Monitor de Hardware & Sistema (Win32) v5",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 616, 780,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    // Tabela de aceleradores: Ctrl+S para exportar o snapshot atual
    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'S', ID_EXPORT_SNAPSHOT }
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, 1);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}