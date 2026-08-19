/*
 * WinMon — variante endurecida, ainda monolítica.
 *
 * Objetivos:
 *   - eliminar o padrão perigoso "offset += snprintf(...)";
 *   - reduzir privilégios pedidos a processos monitorizados;
 *   - carregar Msftedit.dll apenas a partir de System32;
 *   - tratar falhas de alocação/API e overflow de contadores;
 *   - corrigir ciclo de vida de alguns objetos GDI;
 *   - manter a arquitetura num único .c.
 *
 * Recomendações de compilação (MSVC x64):
 *   /O2 /W4 /WX /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT
 *   Linker: /GUARD:CF /CETCOMPAT /DYNAMICBASE /NXCOMPAT
 *
 * IMPORTANTE:
 *   Estas alterações não "assinam" o executável e não contornam o
 *   Smart App Control. Para distribuição, use assinatura Authenticode
 *   com um certificado de assinatura de código confiável.
 */
#include <windows.h>
#include <commctrl.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

#define TIMER_ID 1
#define BUFFER_SIZE 16384
#define MAX_PROCESSES 2048
#define MAX_ALERT_RANGES 32
#define ID_EXPORT_SNAPSHOT 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define MAX_CORES 64

#define LIMITE_RAM_PERCENT 90.0
#define LIMITE_DISCO_PERCENT 95.0

/* Historico dos graficos: 120 segundos, um ponto por segundo. */
#define HISTORICO_PONTOS 120

/* IDs das abas */
#define TAB_RESUMO     2001
#define TAB_CPU        2002
#define TAB_MEMORIA    2003
#define TAB_DISCO      2004
#define TAB_REDE       2005
#define TAB_PROCESSOS  2006

typedef struct {
    DWORD pid;
    SIZE_T memUsageMB;
    double cpuPercent;
    char exeFile[MAX_PATH];
} ProcessoInfo;

typedef struct {
    DWORD pid;
    ULONGLONG lastKernelTime;
    ULONGLONG lastUserTime;
    int valido;
} ProcessoCpuHistorico;

typedef struct {
    long inicio;
    long fim;
} IntervaloAlerta;

/* Estado dos graficos */
typedef struct {
    double cpu[HISTORICO_PONTOS];
    double ram[HISTORICO_PONTOS];
    double diskRead[HISTORICO_PONTOS];
    double diskWrite[HISTORICO_PONTOS];
    double netDown[HISTORICO_PONTOS];
    double netUp[HISTORICO_PONTOS];
    double core[MAX_CORES][HISTORICO_PONTOS];

    int pos;
    int count;
} HistoricoMonitor;

#define MAX_HISTORICO 2048

static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;

HWND hMainWindow = NULL;
HWND hEdit = NULL;
HWND hTab = NULL;
HWND hGraphCPU = NULL;
HWND hGraphRAM = NULL;
HWND hGraphDisk = NULL;
HWND hGraphNet = NULL;
HWND hGraphProcesses = NULL;

PDH_HQUERY hQuery = NULL;
PDH_HCOUNTER hCounterCPU = NULL;
PDH_HCOUNTER hCounterCoresCPU[MAX_CORES];
PDH_HCOUNTER hCounterDiskRead = NULL;
PDH_HCOUNTER hCounterDiskWrite = NULL;

HFONT hFontMonitor = NULL;
HFONT hFontUI = NULL;
HMODULE hRichEditLib = NULL;

NOTIFYICONDATAA nid;
int trayIconAtivo = 0;

DWORDLONG lastIn = 0;
DWORDLONG lastOut = 0;
int firstNetworkRead = 1;

int numProcessadores = 1;
int numNucleosMonitorizados = 0;

static char ultimoSnapshot[BUFFER_SIZE];
static double ultimoRamPercent = 0.0;
static double ultimoCpuPercent = 0.0;
static double ultimoDiskRead = 0.0;
static double ultimoDiskWrite = 0.0;
static double ultimoNetDown = 0.0;
static double ultimoNetUp = 0.0;

static IntervaloAlerta intervalosAlerta[MAX_ALERT_RANGES];
static int totalIntervalosAlerta = 0;
static int alertaGlobalAtivo = 0;

static ProcessoInfo listaProcessos[MAX_PROCESSES];
static HistoricoMonitor historico;

static int abaAtual = 0;

/* Dados do processo principal para o grafico de processos. */
static double processoCpuAtual = 0.0;
static double processoRamAtual = 0.0;
static double coresCpuAtuais[MAX_CORES];
static double processoCpuTop[HISTORICO_PONTOS];
static double processoRamTop[HISTORICO_PONTOS];

/* ------------------------------------------------------------------------- */
/* Utilitarios                                                               */
/* ------------------------------------------------------------------------- */

/*
 * Escrita limitada segura:
 * snprintf() devolve o tamanho que teria escrito mesmo quando trunca.
 * Somar esse valor diretamente a offset pode fazer size - offset sofrer
 * underflow e transformar uma truncagem numa escrita fora do buffer.
 */
static int AppendFormat(char *buffer, size_t capacity, size_t *offset,
                        const char *format, ...) {
    va_list args;
    int written;

    if (!buffer || !offset || !format || *offset >= capacity)
        return 0;

    va_start(args, format);
    written = _vsnprintf_s(
        buffer + *offset,
        capacity - *offset,
        _TRUNCATE,
        format,
        args);
    va_end(args);

    if (written < 0) {
        *offset = capacity - 1;
        buffer[*offset] = '\0';
        return 0;
    }

    *offset += (size_t)written;
    if (*offset >= capacity)
        *offset = capacity - 1;

    buffer[*offset] = '\0';
    return 1;
}

static void InicializarBuffer(char *buffer, size_t capacity, size_t *offset) {
    if (buffer && capacity > 0) {
        buffer[0] = '\0';
        if (offset)
            *offset = 0;
    }
}


void FormatarBytes(double bytes, char *buffer, size_t size) {
    if (bytes >= 1073741824.0) snprintf(buffer, size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576.0) snprintf(buffer, size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024.0) snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    else snprintf(buffer, size, "%.0f B", bytes);
}

static ULONGLONG FileTimeToU64(FILETIME ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

static ProcessoCpuHistorico *EncontrarHistorico(DWORD pid) {
    int i;
    for (i = 0; i < totalHistorico; i++) {
        if (historicoCpu[i].pid == pid) return &historicoCpu[i];
    }
    return NULL;
}

static void RegistarAlerta(long inicio, long fim) {
    if (totalIntervalosAlerta < MAX_ALERT_RANGES) {
        intervalosAlerta[totalIntervalosAlerta].inicio = inicio;
        intervalosAlerta[totalIntervalosAlerta].fim = fim;
        totalIntervalosAlerta++;
    }
    alertaGlobalAtivo = 1;
}

int CompararProcessosPorRAM(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->memUsageMB < p2->memUsageMB) return 1;
    if (p1->memUsageMB > p2->memUsageMB) return -1;
    return 0;
}

int CompararProcessosPorCPU(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->cpuPercent < p2->cpuPercent) return 1;
    if (p1->cpuPercent > p2->cpuPercent) return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Historico circular                                                        */
/* ------------------------------------------------------------------------- */

static int HistoricoIndex(int offset) {
    int idx;
    if (historico.count == 0) return 0;
    idx = historico.pos - (historico.count - 1) + offset;
    while (idx < 0) idx += HISTORICO_PONTOS;
    while (idx >= HISTORICO_PONTOS) idx -= HISTORICO_PONTOS;
    return idx;
}

static void AdicionarHistorico(double cpu, double ram,
                               double diskRead, double diskWrite,
                               double netDown, double netUp) {
    int i;

    historico.pos = (historico.pos + 1) % HISTORICO_PONTOS;
    historico.cpu[historico.pos] = cpu;
    historico.ram[historico.pos] = ram;
    historico.diskRead[historico.pos] = diskRead;
    historico.diskWrite[historico.pos] = diskWrite;
    historico.netDown[historico.pos] = netDown;
    historico.netUp[historico.pos] = netUp;

    for (i = 0; i < numNucleosMonitorizados; i++) {
        /* Inicializar sempre a 0; AtualizarMonitor escreve o valor real a seguir. */
        historico.core[i][historico.pos] = 0.0;
    }

    /* Inicializar; AtualizarMonitor escreve processoCpuAtual/processoRamAtual
     * neste mesmo pos logo a seguir a chamar AdicionarHistorico. */
    processoCpuTop[historico.pos] = 0.0;
    processoRamTop[historico.pos] = 0.0;

    if (historico.count < HISTORICO_PONTOS)
        historico.count++;
}

/* ------------------------------------------------------------------------- */
/* Recolha de dados                                                           */
/* ------------------------------------------------------------------------- */

void MonitorarSistema(char *buffer, size_t size, size_t *offset) {
    ULONGLONG uptimeMs = GetTickCount64();
    int dias = (int)(uptimeMs / (1000ULL * 60 * 60 * 24));
    int horas = (int)((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int minutos = (int)((uptimeMs / (1000ULL * 60)) % 60);

    AppendFormat(buffer, size, offset, "=== [ SISTEMA & UPTIME ] ===\r\n"
        "Tempo de Atividade: %d dias, %d horas, %d minutos\r\n",
        dias, horas, minutos);

    SYSTEM_POWER_STATUS statusEnergia;
    if (GetSystemPowerStatus(&statusEnergia)) {
        if (statusEnergia.BatteryLifePercent != 255) {
            const char *fonte = (statusEnergia.ACLineStatus == 1)
                ? "Carregador Conectado" : "Em Bateria";
            AppendFormat(buffer, size, offset, "Energia: %d%% (%s)\r\n",
                statusEnergia.BatteryLifePercent, fonte);
        } else {
            AppendFormat(buffer, size, offset, "Energia: Desktop (Sem Bateria)\r\n");
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarCPU(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE counterVal;
    double cpuAtual = 0.0;
    int i;

    PdhCollectQueryData(hQuery);

    if (hCounterCPU &&
        PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS &&
        counterVal.CStatus == ERROR_SUCCESS) {
        cpuAtual = counterVal.doubleValue;
    }

    ultimoCpuPercent = cpuAtual;

    AppendFormat(buffer, size, offset, "=== [ PROCESSADOR ] ===\r\n"
        "Uso Atual da CPU (Total): %.1f%%\r\n", cpuAtual);

    if (numNucleosMonitorizados > 0) {
        AppendFormat(buffer, size, offset, "Por Nucleo: ");

        for (i = 0; i < numNucleosMonitorizados; i++) {
            PDH_FMT_COUNTERVALUE coreVal;
            double corePercent = 0.0;

            if (hCounterCoresCPU[i] &&
                PdhGetFormattedCounterValue(hCounterCoresCPU[i], PDH_FMT_DOUBLE,
                                             NULL, &coreVal) == ERROR_SUCCESS &&
                coreVal.CStatus == ERROR_SUCCESS) {
                corePercent = coreVal.doubleValue;
            }

            coresCpuAtuais[i] = corePercent;

            AppendFormat(buffer, size, offset, "C%d:%5.1f%%  ", i, corePercent);

            if ((i + 1) % 4 == 0 && (i + 1) < numNucleosMonitorizados)
                AppendFormat(buffer, size, offset, "\r\n            ");
        }

        AppendFormat(buffer, size, offset, "\r\n");
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarDiscoIO(char *buffer, size_t size, size_t *offset) {
    PDH_FMT_COUNTERVALUE readVal, writeVal;
    double bytesLeitura = 0.0, bytesEscrita = 0.0;

    if (hCounterDiskRead &&
        PdhGetFormattedCounterValue(hCounterDiskRead, PDH_FMT_DOUBLE, NULL, &readVal) == ERROR_SUCCESS &&
        readVal.CStatus == ERROR_SUCCESS) {
        bytesLeitura = readVal.doubleValue;
    }

    if (hCounterDiskWrite &&
        PdhGetFormattedCounterValue(hCounterDiskWrite, PDH_FMT_DOUBLE, NULL, &writeVal) == ERROR_SUCCESS &&
        writeVal.CStatus == ERROR_SUCCESS) {
        bytesEscrita = writeVal.doubleValue;
    }

    ultimoDiskRead = bytesLeitura;
    ultimoDiskWrite = bytesEscrita;

    {
        char leituraStr[32], escritaStr[32];
        FormatarBytes(bytesLeitura, leituraStr, sizeof(leituraStr));
        FormatarBytes(bytesEscrita, escritaStr, sizeof(escritaStr));

        AppendFormat(buffer, size, offset, "=== [ DISCO - VELOCIDADE I/O (TOTAL) ] ===\r\n"
            "Leitura: %-10s/s | Escrita: %-10s/s\r\n\r\n",
            leituraStr, escritaStr);
    }
}

void MonitorarRAM(char *buffer, size_t size, size_t *offset) {
    MEMORYSTATUSEX memInfo;
    ZeroMemory(&memInfo, sizeof(memInfo));
    memInfo.dwLength = sizeof(memInfo);

    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usadaRAM = totalRAM - livreRAM;

        ultimoRamPercent = (double)memInfo.dwMemoryLoad;

        {
            long inicioLinha = (long)*offset;

            AppendFormat(buffer, size, offset, "=== [ MEMORIA RAM GERAL ] ===\r\n"
                "Uso: %ld%% | Usada: %llu MB | Livre: %llu MB (Total: %llu MB)\r\n\r\n",
                memInfo.dwMemoryLoad, usadaRAM, livreRAM, totalRAM);

            if (memInfo.dwMemoryLoad > LIMITE_RAM_PERCENT)
                RegistarAlerta(inicioLinha, (long)*offset);
        }
    }
}

void MonitorarDiscos(char *buffer, size_t size, size_t *offset) {
    DWORD drives = GetLogicalDrives();
    char driveLetter[] = "A:\\";
    int i;

    AppendFormat(buffer, size, offset, "=== [ DISCOS DE ARMAZENAMENTO ] ===\r\n");

    for (i = 0; i < 26; i++) {
        if (drives & (1UL << i)) {
            driveLetter[0] = (char)('A' + i);

            if (GetDriveTypeA(driveLetter) == DRIVE_FIXED) {
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;

                if (GetDiskFreeSpaceExA(driveLetter, &freeBytesAvailable,
                                        &totalBytes, &totalFreeBytes)) {
                    double totalGB = (double)totalBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double livreGB = (double)totalFreeBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double usadaGB = totalGB - livreGB;
                    double percentUsado = totalGB > 0.0
                        ? (usadaGB / totalGB) * 100.0 : 0.0;

                    long inicioLinha = (long)*offset;

                    AppendFormat(buffer, size, offset, "Drive %s  Uso: %5.1f%%  (%.1f GB usad. de %.1f GB)\r\n",
                        driveLetter, percentUsado, usadaGB, totalGB);

                    if (percentUsado > LIMITE_DISCO_PERCENT)
                        RegistarAlerta(inicioLinha, (long)*offset);
                }
            }
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarRede(char *buffer, size_t size, size_t *offset) {
    ULONG outBufLen = 0;
    PMIB_IFTABLE pIfTable;
    DWORDLONG currentIn = 0, currentOut = 0;
    DWORD i;

    if (GetIfTable(NULL, &outBufLen, FALSE) != ERROR_INSUFFICIENT_BUFFER ||
        outBufLen == 0 ||
        outBufLen > (1024UL * 1024UL * 8UL)) {
        return;
    }

    pIfTable = (PMIB_IFTABLE)malloc(outBufLen);
    if (!pIfTable)
        return;

    if (GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR) {
        for (i = 0; i < pIfTable->dwNumEntries; i++) {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }

        if (!firstNetworkRead) {
            double down = (double)(
                currentIn >= lastIn ? currentIn - lastIn : 0);
            double up = (double)(
                currentOut >= lastOut ? currentOut - lastOut : 0);

            ultimoNetDown = down;
            ultimoNetUp = up;

            {
                char downStr[32], upStr[32];
                FormatarBytes(down, downStr, sizeof(downStr));
                FormatarBytes(up, upStr, sizeof(upStr));

                AppendFormat(buffer, size, offset, "=== [ REDE EM TEMPO REAL ] ===\r\n"
                    "Download: %-10s/s | Upload: %-10s/s\r\n\r\n",
                    downStr, upStr);
            }
        } else {
            ultimoNetDown = 0.0;
            ultimoNetUp = 0.0;
        }

        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
    }

    free(pIfTable);
}

void MonitorarProcessos(char *buffer, size_t size, size_t *offset) {
    FILETIME ftIdle, ftKernelSys, ftUserSys;
    ULONGLONG currentSystemTime, deltaSystemTime;
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;
    int totalProcessos = 0;
    ProcessoCpuHistorico novoHistorico[MAX_HISTORICO];
    int totalNovoHistorico = 0;
    int i;

    AppendFormat(buffer, size, offset, "=== [ TOP 12 PROCESSOS (MAIOR CONSUMO CPU) ] ===\r\n");

    GetSystemTimes(&ftIdle, &ftKernelSys, &ftUserSys);
    currentSystemTime = FileTimeToU64(ftKernelSys) +
                         FileTimeToU64(ftUserSys);
    deltaSystemTime = (lastSystemTime != 0)
        ? (currentSystemTime - lastSystemTime) : 0;

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        AppendFormat(buffer, size, offset, "Nao foi possivel enumerar processos.\r\n\r\n");
        return;
    }

    ZeroMemory(&pe32, sizeof(pe32));
    pe32.dwSize = sizeof(pe32);

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            if (pe32.th32ProcessID != 0 && totalProcessos < MAX_PROCESSES) {
                HANDLE hProcess = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, pe32.th32ProcessID);

                SIZE_T memUsageMB = 0;
                double cpuPercent = 0.0;

                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;

                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
                        memUsageMB = pmc.WorkingSetSize / (1024 * 1024);

                    {
                        FILETIME ftCreate, ftExit, ftKernel, ftUser;

                        if (GetProcessTimes(hProcess, &ftCreate, &ftExit,
                                            &ftKernel, &ftUser)) {
                            ULONGLONG kernelU64 = FileTimeToU64(ftKernel);
                            ULONGLONG userU64 = FileTimeToU64(ftUser);
                            ULONGLONG totalProcTime = kernelU64 + userU64;
                            ProcessoCpuHistorico *hist =
                                EncontrarHistorico(pe32.th32ProcessID);

                            if (hist && deltaSystemTime > 0) {
                                ULONGLONG anterior =
                                    hist->lastKernelTime + hist->lastUserTime;
                                ULONGLONG deltaProc =
                                    totalProcTime >= anterior
                                    ? totalProcTime - anterior : 0;

                                cpuPercent =
                                    ((double)deltaProc /
                                     (double)deltaSystemTime) *
                                    100.0 * numProcessadores;

                                if (cpuPercent < 0.0) cpuPercent = 0.0;
                                if (cpuPercent > 100.0 * numProcessadores)
                                    cpuPercent = 100.0 * numProcessadores;
                            }

                            if (totalNovoHistorico < MAX_HISTORICO) {
                                novoHistorico[totalNovoHistorico].pid =
                                    pe32.th32ProcessID;
                                novoHistorico[totalNovoHistorico].lastKernelTime =
                                    kernelU64;
                                novoHistorico[totalNovoHistorico].lastUserTime =
                                    userU64;
                                novoHistorico[totalNovoHistorico].valido = 1;
                                totalNovoHistorico++;
                            }
                        }
                    }

                    CloseHandle(hProcess);
                }

                listaProcessos[totalProcessos].pid = pe32.th32ProcessID;
                listaProcessos[totalProcessos].memUsageMB = memUsageMB;
                listaProcessos[totalProcessos].cpuPercent = cpuPercent;

                strncpy_s(listaProcessos[totalProcessos].exeFile, MAX_PATH,
                          pe32.szExeFile, _TRUNCATE);

                totalProcessos++;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);

    memcpy(historicoCpu, novoHistorico,
           sizeof(ProcessoCpuHistorico) * totalNovoHistorico);
    totalHistorico = totalNovoHistorico;
    lastSystemTime = currentSystemTime;

    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo),
          CompararProcessosPorCPU);

    processoCpuAtual =
        (totalProcessos > 0) ? listaProcessos[0].cpuPercent : 0.0;

    {
        int limite = (totalProcessos < 12) ? totalProcessos : 12;

        for (i = 0; i < limite; i++) {
            AppendFormat(buffer, size, offset, "PID: %-6u | CPU: %5.1f%% | RAM: %5lu MB | %s\r\n",
                listaProcessos[i].pid, listaProcessos[i].cpuPercent,
                (unsigned long)listaProcessos[i].memUsageMB,
                listaProcessos[i].exeFile);
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");

    AppendFormat(buffer, size, offset, "=== [ TOP 5 PROCESSOS (MAIOR CONSUMO RAM) ] ===\r\n");

    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo),
          CompararProcessosPorRAM);

    processoRamAtual =
        (totalProcessos > 0) ? (double)listaProcessos[0].memUsageMB : 0.0;

    {
        int limiteRAM = (totalProcessos < 5) ? totalProcessos : 5;

        for (i = 0; i < limiteRAM; i++) {
            AppendFormat(buffer, size, offset, "PID: %-6u | RAM: %5lu MB | CPU: %5.1f%% | %s\r\n",
                listaProcessos[i].pid,
                (unsigned long)listaProcessos[i].memUsageMB,
                listaProcessos[i].cpuPercent,
                listaProcessos[i].exeFile);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Graficos GDI                                                              */
/* ------------------------------------------------------------------------- */

typedef enum {
    GRAPH_CPU,
    GRAPH_RAM,
    GRAPH_DISK,
    GRAPH_NET,
    GRAPH_PROCESS
} GraphType;

typedef struct {
    GraphType type;
} GraphContext;

/*
 * Os cinco controlos de grafico usam o mesmo WindowProc. O tipo e guardado
 * no GWLP_USERDATA como GraphType+1.
 */

static void DrawTextCentered(HDC hdc, const char *text, RECT *r) {
    DrawTextA(hdc, text, -1, r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawGraphGrid(HDC hdc, RECT rc, double maxY) {
    HPEN penGrid = CreatePen(PS_SOLID, 1, RGB(225, 228, 232));
    HPEN penAxis = CreatePen(PS_SOLID, 1, RGB(180, 185, 190));
    HPEN oldPen;
    HFONT oldFont;
    RECT labelRect;
    int i;

    oldPen = (HPEN)SelectObject(hdc, penGrid);

    for (i = 0; i <= 4; i++) {
        int y = rc.top + ((rc.bottom - rc.top) * i) / 4;
        MoveToEx(hdc, rc.left, y, NULL);
        LineTo(hdc, rc.right, y);

        labelRect.left = 2;
        labelRect.right = rc.left - 6;
        labelRect.top = y - 8;
        labelRect.bottom = y + 8;

        {
            char label[32];
            double value = maxY * (1.0 - (double)i / 4.0);

            if (maxY <= 100.0)
                snprintf(label, sizeof(label), "%.0f", value);
            else if (maxY >= 1048576.0)
                snprintf(label, sizeof(label), "%.0f MB", value / 1048576.0);
            else
                snprintf(label, sizeof(label), "%.0f", value);

            oldFont = (HFONT)SelectObject(hdc,
                GetStockObject(DEFAULT_GUI_FONT));
            SetTextColor(hdc, RGB(105, 110, 116));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, label, -1, &labelRect,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
        }
    }

    SelectObject(hdc, penAxis);
    MoveToEx(hdc, rc.left, rc.top, NULL);
    LineTo(hdc, rc.left, rc.bottom);
    LineTo(hdc, rc.right, rc.bottom);

    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);
    DeleteObject(penAxis);
}

static void DrawSeries(HDC hdc, RECT rc, const double *data,
                       int count, double maxY, COLORREF color,
                       int thickness) {
    HPEN pen;
    HPEN oldPen;
    int i;
    int lastX = 0, lastY = 0;

    if (count <= 0 || maxY <= 0.0) return;

    pen = CreatePen(PS_SOLID, thickness, color);
    if (!pen) return;

    oldPen = (HPEN)SelectObject(hdc, pen);

    for (i = 0; i < count; i++) {
        int idx = HistoricoIndex(i);
        double value = data[idx];

        if (value < 0.0) value = 0.0;
        if (value > maxY) value = maxY;

        {
            int x = rc.left +
                (count == 1 ? 0 :
                (int)(((double)i / (double)(count - 1)) *
                      (rc.right - rc.left)));

            int y = rc.bottom -
                (int)((value / maxY) * (rc.bottom - rc.top));

            if (i == 0)
                MoveToEx(hdc, x, y, NULL);
            else
                LineTo(hdc, x, y);

            lastX = x;
            lastY = y;
        }
    }

    (void)lastX;
    (void)lastY;

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawGraphLegend(HDC hdc, RECT *rc, const char *title,
                            const char **names, COLORREF *colors, int n,
                            const char *currentText) {
    HFONT oldFont;
    RECT titleRect;
    int x;
    int i;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(40, 44, 48));

    oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));

    titleRect = *rc;
    titleRect.left += 6;
    titleRect.top += 4;
    titleRect.bottom = titleRect.top + 24;
    DrawTextA(hdc, title, -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    x = rc->left + 10;

    for (i = 0; i < n; i++) {
        HBRUSH brush = CreateSolidBrush(colors[i]);
        RECT box = { x, rc->top + 34, x + 10, rc->top + 44 };
        RECT text = { x + 15, rc->top + 29, x + 130, rc->top + 49 };

        FillRect(hdc, &box, brush);
        DeleteObject(brush);

        DrawTextA(hdc, names[i], -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        x += 125;
    }

    if (currentText) {
        RECT current = *rc;
        current.left = rc->right - 260;
        current.top += 5;
        current.bottom = current.top + 22;
        DrawTextA(hdc, currentText, -1, &current,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

static void DrawTimeLabels(HDC hdc, RECT rc, int count) {
    char text[64];
    int i;
    int marks[5];

    if (count <= 0) return;

    for (i = 0; i < 5; i++) {
        int x = rc.left + ((rc.right - rc.left) * i) / 4;
        int secondsAgo = (count - 1) * (4 - i) / 4;

        if (i == 4)
            strcpy_s(text, sizeof(text), "agora");
        else
            snprintf(text, sizeof(text), "-%ds", secondsAgo);

        {
            RECT tr = { x - 35, rc.bottom + 6, x + 35, rc.bottom + 24 };
            SetTextColor(hdc, RGB(115, 120, 126));
            DrawTextA(hdc, text, -1, &tr,
                      DT_CENTER | DT_SINGLELINE);
        }

        marks[i] = x;
    }

    (void)marks;
}

static void PaintGraph(HWND hwnd, HDC hdc, GraphType type) {
    RECT client;
    RECT graph;
    HBRUSH bg = CreateSolidBrush(RGB(250, 251, 252));
    double maxY = 100.0;
    char currentText[128];
    const char *names[3];
    COLORREF colors[3];
    int n = 0;
    int i;

    GetClientRect(hwnd, &client);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    graph.left = 58;
    graph.top = 58;
    graph.right = client.right - 20;
    graph.bottom = client.bottom - 40;

    if (graph.right <= graph.left + 20 || graph.bottom <= graph.top + 20)
        return;

    currentText[0] = '\0';

    switch (type) {
        case GRAPH_CPU: {
            names[0] = "CPU total";
            colors[0] = RGB(35, 115, 210);
            n = 1;

            snprintf(currentText, sizeof(currentText),
                     "Atual: %.1f%%", ultimoCpuPercent);

            DrawGraphLegend(hdc, &client, "CPU — ultimos 120 segundos",
                            names, colors, n, currentText);
            DrawGraphGrid(hdc, graph, 100.0);
            DrawSeries(hdc, graph, historico.cpu, historico.count,
                       100.0, colors[0], 2);

            /* Pequenos graficos dos nucleos na parte inferior se houver espaco. */
            if (numNucleosMonitorizados > 0 && client.bottom > 430) {
                int smallTop = graph.bottom + 42;
                int smallBottom = client.bottom - 12;
                int smallHeight = smallBottom - smallTop;

                if (smallHeight > 45) {
                    int cols = 4;
                    int rows = (numNucleosMonitorizados + cols - 1) / cols;
                    int cw = (client.right - 72) / cols;
                    int rh = smallHeight / rows;

                    for (i = 0; i < numNucleosMonitorizados; i++) {
                        int col = i % cols;
                        int row = i / cols;
                        RECT sr;

                        sr.left = 60 + col * cw;
                        sr.top = smallTop + row * rh;
                        sr.right = sr.left + cw - 8;
                        sr.bottom = sr.top + rh - 8;

                        DrawGraphGrid(hdc, sr, 100.0);
                        DrawSeries(hdc, sr, historico.core[i],
                                   historico.count, 100.0,
                                   RGB(85, 145, 95), 1);

                        {
                            char label[32];
                            snprintf(label, sizeof(label), "C%d", i);
                            SetTextColor(hdc, RGB(70, 75, 80));
                            SetBkMode(hdc, TRANSPARENT);
                            TextOutA(hdc, sr.left + 4, sr.top + 3,
                                     label, (int)strlen(label));
                        }
                    }
                }
            }

            DrawTimeLabels(hdc, graph, historico.count);
            break;
        }

        case GRAPH_RAM: {
            names[0] = "RAM usada";
            colors[0] = RGB(145, 75, 185);
            n = 1;

            snprintf(currentText, sizeof(currentText),
                     "Atual: %.0f%%", ultimoRamPercent);

            DrawGraphLegend(hdc, &client, "Memoria RAM — uso percentual",
                            names, colors, n, currentText);
            DrawGraphGrid(hdc, graph, 100.0);
            DrawSeries(hdc, graph, historico.ram, historico.count,
                       100.0, colors[0], 2);
            DrawTimeLabels(hdc, graph, historico.count);
            break;
        }

        case GRAPH_DISK: {
            double maxVal = 1024.0 * 1024.0;

            for (i = 0; i < HISTORICO_PONTOS; i++) {
                if (historico.diskRead[i] > maxVal)
                    maxVal = historico.diskRead[i];
                if (historico.diskWrite[i] > maxVal)
                    maxVal = historico.diskWrite[i];
            }

            names[0] = "Leitura";
            names[1] = "Escrita";
            colors[0] = RGB(40, 120, 200);
            colors[1] = RGB(215, 120, 45);
            n = 2;

            snprintf(currentText, sizeof(currentText),
                     "R %.2f MB/s   W %.2f MB/s",
                     ultimoDiskRead / 1048576.0,
                     ultimoDiskWrite / 1048576.0);

            DrawGraphLegend(hdc, &client, "Disco — velocidade I/O",
                            names, colors, n, currentText);
            DrawGraphGrid(hdc, graph, maxVal);
            DrawSeries(hdc, graph, historico.diskRead, historico.count,
                       maxVal, colors[0], 2);
            DrawSeries(hdc, graph, historico.diskWrite, historico.count,
                       maxVal, colors[1], 2);
            DrawTimeLabels(hdc, graph, historico.count);
            break;
        }

        case GRAPH_NET: {
            double maxVal = 1024.0 * 1024.0;

            for (i = 0; i < HISTORICO_PONTOS; i++) {
                if (historico.netDown[i] > maxVal)
                    maxVal = historico.netDown[i];
                if (historico.netUp[i] > maxVal)
                    maxVal = historico.netUp[i];
            }

            names[0] = "Download";
            names[1] = "Upload";
            colors[0] = RGB(35, 145, 95);
            colors[1] = RGB(205, 80, 80);
            n = 2;

            snprintf(currentText, sizeof(currentText),
                     "↓ %.2f MB/s   ↑ %.2f MB/s",
                     ultimoNetDown / 1048576.0,
                     ultimoNetUp / 1048576.0);

            DrawGraphLegend(hdc, &client, "Rede — trafego",
                            names, colors, n, currentText);
            DrawGraphGrid(hdc, graph, maxVal);
            DrawSeries(hdc, graph, historico.netDown, historico.count,
                       maxVal, colors[0], 2);
            DrawSeries(hdc, graph, historico.netUp, historico.count,
                       maxVal, colors[1], 2);
            DrawTimeLabels(hdc, graph, historico.count);
            break;
        }

        case GRAPH_PROCESS: {
            double maxCpu = 100.0;
            double maxRam = 1024.0;

            for (i = 0; i < HISTORICO_PONTOS; i++) {
                if (processoCpuTop[i] > maxCpu)
                    maxCpu = processoCpuTop[i];
                if (processoRamTop[i] > maxRam)
                    maxRam = processoRamTop[i];
            }

            /*
             * O grafico principal mostra CPU do processo lider. A RAM do
             * processo lider e mostrada numa segunda escala visual.
             */
            names[0] = "Processo #1 CPU";
            colors[0] = RGB(205, 70, 70);
            n = 1;

            snprintf(currentText, sizeof(currentText),
                     "Pico atual: %.1f%% CPU",
                     processoCpuTop[historico.pos]);

            DrawGraphLegend(hdc, &client, "Processos — processo com maior CPU",
                            names, colors, n, currentText);
            DrawGraphGrid(hdc, graph, maxCpu);
            DrawSeries(hdc, graph, processoCpuTop, historico.count,
                       maxCpu, colors[0], 2);
            DrawTimeLabels(hdc, graph, historico.count);

            if (client.bottom > 360) {
                RECT info = {
                    70, graph.bottom + 34,
                    client.right - 30, client.bottom - 8
                };
                char line[256];

                snprintf(line, sizeof(line),
                    "RAM do processo lider neste instante: %.0f MB",
                    processoRamTop[historico.pos]);

                SetTextColor(hdc, RGB(75, 80, 86));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, line, -1, &info,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            (void)maxRam;
            break;
        }
    }
}

LRESULT CALLBACK GraphProc(HWND hwnd, UINT msg,
                           WPARAM wParam, LPARAM lParam) {
    GraphType type = (GraphType)(GetWindowLongPtr(hwnd, GWLP_USERDATA) - 1);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintGraph(hwnd, hdc, type);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static HWND CriarGrafico(HWND parent, GraphType type) {
    HWND hwnd = CreateWindowExA(
        0,
        "HardwareMonitorGraph",
        "",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        NULL,
        GetModuleHandle(NULL),
        NULL);

    if (hwnd)
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)type + 1);

    return hwnd;
}

/* ------------------------------------------------------------------------- */
/* RichEdit e interface                                                      */
/* ------------------------------------------------------------------------- */

void AplicarCoresDeAlerta() {
    CHARFORMAT2A cf;
    int i;

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;

    SendMessage(hEdit, EM_SETSEL, 0, -1);
    cf.crTextColor = RGB(0, 0, 0);
    cf.dwEffects = 0;
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    cf.crTextColor = RGB(200, 0, 0);

    for (i = 0; i < totalIntervalosAlerta; i++) {
        SendMessage(hEdit, EM_SETSEL,
                    intervalosAlerta[i].inicio,
                    intervalosAlerta[i].fim);
        SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }

    SendMessage(hEdit, EM_SETSEL, 0, 0);
}

void AtualizarTituloJanela() {
    if (alertaGlobalAtivo) {
        SetWindowTextA(hMainWindow,
            "Monitor de Hardware & Sistema (Win32) v6  —  [!] ALERTA");
    } else {
        SetWindowTextA(hMainWindow,
            "Monitor de Hardware & Sistema (Win32) v6");
    }
}

void AtualizarTooltipTray() {
    if (!trayIconAtivo) return;

    snprintf(nid.szTip, sizeof(nid.szTip),
        "Monitor de Hardware\r\nCPU: %.1f%% | RAM: %.0f%%",
        ultimoCpuPercent, ultimoRamPercent);

    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void ExportarSnapshot() {
    SYSTEMTIME st;
    char nomeFicheiro[MAX_PATH];
    FILE *f = NULL;
    errno_t err;
    char msg[MAX_PATH + 64];

    GetLocalTime(&st);

    snprintf(nomeFicheiro, sizeof(nomeFicheiro),
        "snapshot_%04d%02d%02d_%02d%02d%02d.txt",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    err = fopen_s(&f, nomeFicheiro, "wb");

    if (err == 0 && f != NULL) {
        fwrite(ultimoSnapshot, 1, strlen(ultimoSnapshot), f);
        fclose(f);

        snprintf(msg, sizeof(msg),
            "Snapshot gravado como:\r\n%s", nomeFicheiro);

        MessageBoxA(hMainWindow, msg,
                    "Export concluido", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxA(hMainWindow,
            "Nao foi possivel gravar o ficheiro de snapshot.",
            "Erro", MB_OK | MB_ICONERROR);
    }
}

/* ------------------------------------------------------------------------- */
/* Abas                                                                      */
/* ------------------------------------------------------------------------- */

static void MostrarAba(int indice) {
    int i;
    HWND graficos[] = {
        hGraphCPU, hGraphRAM, hGraphDisk, hGraphNet, hGraphProcesses
    };

    abaAtual = indice;

    if (hEdit)
        ShowWindow(hEdit, indice == 0 ? SW_SHOW : SW_HIDE);

    for (i = 0; i < 5; i++) {
        if (graficos[i])
            ShowWindow(graficos[i], indice == i + 1 ? SW_SHOW : SW_HIDE);
    }

    InvalidateRect(hMainWindow, NULL, TRUE);
}

static void RedimensionarConteudo(HWND hwnd) {
    RECT rc;
    int top = 0;

    GetClientRect(hwnd, &rc);

    if (hTab) {
        MoveWindow(hTab, 0, 0, rc.right, rc.bottom, TRUE);

        TabCtrl_AdjustRect(hTab, FALSE, &rc);
        top = rc.top;

        if (hEdit)
            MoveWindow(hEdit, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphCPU)
            MoveWindow(hGraphCPU, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphRAM)
            MoveWindow(hGraphRAM, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphDisk)
            MoveWindow(hGraphDisk, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphNet)
            MoveWindow(hGraphNet, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphProcesses)
            MoveWindow(hGraphProcesses, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        (void)top;
    }
}

static void CriarAbas(HWND hwnd) {
    TCITEMA item;
    const char *nomes[] = {
        "Resumo", "CPU", "Memoria", "Disco", "Rede", "Processos"
    };
    int i;

    hTab = CreateWindowExA(
        0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT;

    for (i = 0; i < 6; i++) {
        item.pszText = (LPSTR)nomes[i];
        TabCtrl_InsertItem(hTab, i, &item);
    }

    hEdit = CreateWindowExA(
        0, "RICHEDIT50W", "A recolher dados do sistema...",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY,
        0, 0, 100, 100,
        hwnd, NULL, NULL, NULL);

    hFontMonitor = CreateFontA(
        15, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
        "Consolas");

    if (hFontMonitor)
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMonitor, TRUE);

    SendMessage(hEdit, EM_EXLIMITTEXT, 0,
                (LPARAM)(BUFFER_SIZE * 2));

    hGraphCPU = CriarGrafico(hwnd, GRAPH_CPU);
    hGraphRAM = CriarGrafico(hwnd, GRAPH_RAM);
    hGraphDisk = CriarGrafico(hwnd, GRAPH_DISK);
    hGraphNet = CriarGrafico(hwnd, GRAPH_NET);
    hGraphProcesses = CriarGrafico(hwnd, GRAPH_PROCESS);

    MostrarAba(0);
}

void AtualizarMonitor() {
    static char buffer[BUFFER_SIZE];
    size_t offset = 0;

    InicializarBuffer(buffer, sizeof(buffer), &offset);

    totalIntervalosAlerta = 0;
    alertaGlobalAtivo = 0;

    MonitorarSistema(buffer, BUFFER_SIZE, &offset);
    MonitorarCPU(buffer, BUFFER_SIZE, &offset);
    MonitorarRAM(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscos(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscoIO(buffer, BUFFER_SIZE, &offset);
    MonitorarRede(buffer, BUFFER_SIZE, &offset);
    MonitorarProcessos(buffer, BUFFER_SIZE, &offset);

    /*
     * AdicionarHistorico avanca historico.pos para o novo slot e inicializa
     * os arrays de nucleos e tops a 0. De seguida escrevemos os valores reais
     * nesse mesmo slot, garantindo que todos os dados do mesmo ciclo ficam
     * alinhados na mesma posicao do buffer circular.
     */
    AdicionarHistorico(
        ultimoCpuPercent,
        ultimoRamPercent,
        ultimoDiskRead,
        ultimoDiskWrite,
        ultimoNetDown,
        ultimoNetUp);

    {
        int i;
        for (i = 0; i < numNucleosMonitorizados; i++)
            historico.core[i][historico.pos] = coresCpuAtuais[i];
    }

    processoCpuTop[historico.pos] = processoCpuAtual;
    processoRamTop[historico.pos] = processoRamAtual;

    strncpy_s(ultimoSnapshot, BUFFER_SIZE, buffer, _TRUNCATE);

    SetWindowTextA(hEdit, buffer);
    AplicarCoresDeAlerta();
    AtualizarTituloJanela();
    AtualizarTooltipTray();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);
}

/* ------------------------------------------------------------------------- */
/* WindowProc                                                                */
/* ------------------------------------------------------------------------- */

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg,
                            WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            SYSTEM_INFO sysInfo;
            INITCOMMONCONTROLSEX icc;

            hMainWindow = hwnd;

            icc.dwSize = sizeof(icc);
            icc.dwICC = ICC_TAB_CLASSES;
            InitCommonControlsEx(&icc);

            GetSystemInfo(&sysInfo);
            numProcessadores = (int)sysInfo.dwNumberOfProcessors;
            if (numProcessadores < 1) numProcessadores = 1;

            ZeroMemory(&historico, sizeof(historico));

            hRichEditLib = LoadLibraryExW(L"Msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            CriarAbas(hwnd);

            /* Inicializacao do PDH. */
            if (PdhOpenQuery(NULL, 0, &hQuery) == ERROR_SUCCESS) {
                PdhAddEnglishCounterA(
                    hQuery, "\\Processor(_Total)\\% Processor Time",
                    0, &hCounterCPU);

                numNucleosMonitorizados =
                    (numProcessadores < MAX_CORES)
                    ? numProcessadores : MAX_CORES;

                {
                    int i;
                    for (i = 0; i < numNucleosMonitorizados; i++) {
                        char pathContador[64];

                        snprintf(pathContador, sizeof(pathContador),
                                 "\\Processor(%d)\\%% Processor Time", i);

                        PdhAddEnglishCounterA(
                            hQuery, pathContador, 0,
                            &hCounterCoresCPU[i]);
                    }
                }

                PdhAddEnglishCounterA(
                    hQuery,
                    "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec",
                    0, &hCounterDiskRead);

                PdhAddEnglishCounterA(
                    hQuery,
                    "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec",
                    0, &hCounterDiskWrite);

                PdhCollectQueryData(hQuery);
            }

            ZeroMemory(&nid, sizeof(nid));
            nid.cbSize = sizeof(NOTIFYICONDATAA);
            nid.hWnd = hwnd;
            nid.uID = ID_TRAY_ICON;
            nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
            nid.uCallbackMessage = WM_TRAYICON;
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            strncpy_s(nid.szTip, sizeof(nid.szTip),
                      "Monitor de Hardware", _TRUNCATE);

            SetTimer(hwnd, TIMER_ID, 1000, NULL);
            return 0;
        }

        case WM_SIZE:
            RedimensionarConteudo(hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO *mmi = (MINMAXINFO *)lParam;
            mmi->ptMinTrackSize.x = 620;
            mmi->ptMinTrackSize.y = 420;
            return 0;
        }

        case WM_NOTIFY: {
            NMHDR *hdr = (NMHDR *)lParam;

            if (hdr && hdr->hwndFrom == hTab &&
                hdr->code == TCN_SELCHANGE) {
                int indice = TabCtrl_GetCurSel(hTab);
                MostrarAba(indice);
                return 0;
            }

            break;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE);

                if (!trayIconAtivo) {
                    Shell_NotifyIconA(NIM_ADD, &nid);
                    trayIconAtivo = 1;
                }

                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK ||
                lParam == WM_LBUTTONUP) {
                if (trayIconAtivo) {
                    Shell_NotifyIconA(NIM_DELETE, &nid);
                    trayIconAtivo = 0;
                }

                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_EXPORT_SNAPSHOT) {
                ExportarSnapshot();
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TIMER:
            if (wParam == TIMER_ID)
                AtualizarMonitor();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);

            if (hQuery) {
                PdhCloseQuery(hQuery);
                hQuery = NULL;
            }

            if (hFontMonitor) {
                DeleteObject(hFontMonitor);
                hFontMonitor = NULL;
            }

            if (hFontUI) {
                DeleteObject(hFontUI);
                hFontUI = NULL;
            }

            if (trayIconAtivo) {
                Shell_NotifyIconA(NIM_DELETE, &nid);
                trayIconAtivo = 0;
            }

            if (hRichEditLib) {
                FreeLibrary(hRichEditLib);
                hRichEditLib = NULL;
            }

            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/* ------------------------------------------------------------------------- */
/* WinMain                                                                   */
/* ------------------------------------------------------------------------- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "HardwareMonitorClassV6";
    WNDCLASSA wc;
    HWND hwnd;
    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'S', ID_EXPORT_SNAPSHOT }
    };
    HACCEL hAccel;
    MSG msg;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    HANDLE hInstanceMutex = NULL;

    (void)hPrevInstance;
    (void)lpCmdLine;

    hInstanceMutex = CreateMutexW(
        NULL, TRUE, L"Local\\WinMon-HardwareMonitor-V6");

    if (!hInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hInstanceMutex)
            CloseHandle(hInstanceMutex);
        return 0;
    }

    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiCtxFunc)(DPI_AWARENESS_CONTEXT);
        SetDpiCtxFunc pSetDpiCtx =
            (SetDpiCtxFunc)GetProcAddress(
                hUser32, "SetProcessDpiAwarenessContext");

        if (pSetDpiCtx)
            pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else
            SetProcessDPIAware();
    } else {
        SetProcessDPIAware();
    }

    /* Regista a classe dos graficos. */
    {
        WNDCLASSA graphClass;
        ZeroMemory(&graphClass, sizeof(graphClass));

        graphClass.lpfnWndProc = GraphProc;
        graphClass.hInstance = hInstance;
        graphClass.lpszClassName = "HardwareMonitorGraph";
        graphClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        graphClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        RegisterClassA(&graphClass);
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassA(&wc);

    hwnd = CreateWindowExA(
        0, CLASS_NAME,
        "Monitor de Hardware & Sistema (Win32) v6",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        980, 760,
        NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return 0;

    hAccel = CreateAcceleratorTableA(accels, 1);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    ZeroMemory(&msg, sizeof(msg));

    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!TranslateAcceleratorA(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    if (hAccel)
        DestroyAcceleratorTable(hAccel);

    if (hInstanceMutex)
        CloseHandle(hInstanceMutex);

    return (int)msg.wParam;
}