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

/* Nota: o manifesto UAC (requireAdministrator) e embutido via winmon.rc + windres,
 * pois o #pragma comment(linker,/manifestuac) so funciona com MSVC. */
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "advapi32.lib")

/* WMI para temperatura.
 * Definir os GUIDs inline evita depender de wbemuuid.lib/-luuid no MinGW,
 * onde CLSID_WbemLocator e IID_IWbemLocator nao estao incluidos em -luuid. */
#define _WIN32_DCOM
#include <wbemidl.h>

/* GUIDs necessarios para o WMI — definidos aqui para compatibilidade MinGW. */
static const CLSID LOCAL_CLSID_WbemLocator =
    {0x4590f811,0x1d3a,0x11d0,{0x89,0x1f,0x00,0xaa,0x00,0x4b,0x2e,0x24}};
static const IID   LOCAL_IID_IWbemLocator  =
    {0xdc12a687,0x737f,0x11cf,{0x88,0x4d,0x00,0xaa,0x00,0x4b,0x2e,0x24}};

#define CLSID_WbemLocator LOCAL_CLSID_WbemLocator
#define IID_IWbemLocator  LOCAL_IID_IWbemLocator

#define TIMER_ID 1
#define BUFFER_SIZE 16384
#define MAX_PROCESSES 2048
#define MAX_ALERT_RANGES 32
#define ID_EXPORT_SNAPSHOT 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define MAX_CORES 64

/* Limites de alerta — podem ser alterados em runtime via dialogo. */
#define LIMITE_RAM_PERCENT_DEFAULT    90.0
#define LIMITE_DISCO_PERCENT_DEFAULT  95.0
#define LIMITE_CPU_PERCENT_DEFAULT    90.0

/* Logging CSV */
#define LOG_INTERVALO_SEGUNDOS  10      /* escreve no CSV a cada N ticks */
#define ID_TOGGLE_LOG          1002
#define ID_CONFIG_ALERTAS      1003

/* Temperatura CPU (WMI — MSAcpi_ThermalZoneTemperature, root\wmi) */
#define MAX_TEMP_ZONAS  16

/* Notificacoes balloon */
#define BALLOON_COOLDOWN_SEGUNDOS  30   /* minimo de segundos entre balloons */

/* Historico dos graficos: 120 segundos, um ponto por segundo. */
#define HISTORICO_PONTOS 120

/* ---- Overlay alargado (estilo RTSS) ---- */
#define ID_TOGGLE_OVERLAY   1004
#define ID_CONFIG_OVERLAY   1005
#define OVERLAY_CLASS_NAME  "WinMonOverlayClass"

/* Número de colunas de métricas no overlay */
#define OV_NUM_COLUNAS  5   /* CPU | RAM | NET | TEMP | DISCO */

/* Limites do tamanho de fonte do overlay */
#define OV_FONT_MIN  8
#define OV_FONT_MAX  32

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

/* ---- Limites de alerta configuráveis em runtime ---- */
static double limiteCpuPercent   = LIMITE_CPU_PERCENT_DEFAULT;
static double limiteRamPercent   = LIMITE_RAM_PERCENT_DEFAULT;
static double limiteDiscoPercent = LIMITE_DISCO_PERCENT_DEFAULT;

/* ---- Logging CSV ---- */
static FILE   *hLogCSV       = NULL;
static int     logAtivo      = 0;
static int     logTickContador = 0;
static char    logNomeFicheiro[MAX_PATH] = {0};

/* ---- Temperatura CPU (WMI) ---- */
static int    numZonasTemp = 0;
static double tempAtual[MAX_TEMP_ZONAS];
static char   tempNome[MAX_TEMP_ZONAS][64];

/* ---- Notificacoes balloon ---- */
static ULONGLONG ultimoBalloonTick = 0;

static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;

HWND hMainWindow  = NULL;
HWND hEdit        = NULL;
HWND hTab         = NULL;
HWND hBtnOverlay  = NULL;
HWND hGraphCPU    = NULL;
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

/* ---- Overlay alargado — configuração em runtime ---- */
static HWND  hOverlay         = NULL;
static int   overlayAtivo     = 0;

/* Configurações ajustáveis (persistidas no Registry) */
static int   ovFontSize       = 13;          /* tamanho da fonte em pt */
static char  ovFontName[64]   = "Consolas";  /* nome da fonte */
static BYTE  ovOpacity        = 210;         /* 0-255 */
static int   ovMostrarTemp    = 1;           /* mostrar coluna de temperatura */
static int   ovMostrarDisco   = 1;           /* mostrar coluna de disco */

/* Dimensões calculadas dinamicamente em RecalcOverlaySize() */
static int   ovColW           = 120;  /* largura de cada coluna */
static int   ovRowH           = 0;    /* altura de linha (calculada) */
static int   ovTotalW         = 0;
static int   ovTotalH         = 0;

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

    {
        long inicioLinha = (long)*offset;
        AppendFormat(buffer, size, offset, "=== [ PROCESSADOR ] ===\r\n"
            "Uso Atual da CPU (Total): %.1f%%\r\n", cpuAtual);
        if (cpuAtual > limiteCpuPercent)
            RegistarAlerta(inicioLinha, (long)*offset);
    }

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

            if ((double)memInfo.dwMemoryLoad > limiteRamPercent)
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

                    if (percentUsado > limiteDiscoPercent)
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
/* Declaracoes antecipadas (forward declarations)                            */
/* ------------------------------------------------------------------------- */

static void IniciarLogCSV(void);
static void FecharLogCSV(void);
static void EscreverLinhaLog(void);
static void MostrarDialogoAlertas(HWND hwndPai);
static void CarregarConfigIni(void);
static void GravarConfigIni(void);
static void InicializarTemperaturaCPU(void);
static void LerTemperaturaCPU(char *buffer, size_t size, size_t *offset);
static void TerminarWMI(void);
static void EnviarBalloon(const char *titulo, const char *msg);
static void VerificarAlertasBalloon(void);
static void CriarOverlay(void);
static void FecharOverlay(void);
static void ToggleOverlay(void);
static void AtualizarOverlay(void);
static void RecalcOverlaySize(void);
static void MostrarDialogoConfigOverlay(HWND hwndPai);
static void GravarConfigOverlay(void);
static void CarregarConfigOverlay(void);

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

    /* Botoes overlay: canto superior-direito, sobrepostos ao tab.
     *   [⚙][  Overlay [Ctrl+O]  ]
     */
    if (hBtnOverlay)
        MoveWindow(hBtnOverlay, rc.right - 122, 3, 118, 22, TRUE);

    /* Botão de configuração — à esquerda do toggle */
    {
        HWND hBtnCfg = GetDlgItem(hwnd, ID_CONFIG_OVERLAY);
        if (hBtnCfg)
            MoveWindow(hBtnCfg, rc.right - 150, 3, 26, 22, TRUE);
    }

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

    /* Botao "Overlay" no canto superior-direito da barra de abas.
     * Posição provisória — RedimensionarConteudo ajusta. */
    hBtnOverlay = CreateWindowExA(
        0, "BUTTON", "Overlay [Ctrl+O]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 118, 22,
        hwnd, (HMENU)(UINT_PTR)ID_TOGGLE_OVERLAY,
        GetModuleHandle(NULL), NULL);

    /* Botao de configuracao do overlay (engrenagem) */
    CreateWindowExA(
        0, "BUTTON", "\xE2\x9A\x99",   /* UTF-8 ⚙ (fallback: usa "CFG") */
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 26, 22,
        hwnd, (HMENU)(UINT_PTR)ID_CONFIG_OVERLAY,
        GetModuleHandle(NULL), NULL);

    if (hFontUI) {
        SendMessage(hBtnOverlay, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        /* O botao de config usa a fonte do sistema — sem forçar */
    }

    MostrarAba(0);
}

/* ========================================================================= */
/* FUNCIONALIDADE 1 — Logging CSV continuo                                   */
/* ========================================================================= */

static void IniciarLogCSV(void) {
    SYSTEMTIME st;
    errno_t err;

    if (hLogCSV) return; /* ja aberto */

    GetLocalTime(&st);
    snprintf(logNomeFicheiro, sizeof(logNomeFicheiro),
        "winmon_log_%04d%02d%02d_%02d%02d%02d.csv",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);

    err = fopen_s(&hLogCSV, logNomeFicheiro, "a");
    if (err != 0 || !hLogCSV) {
        MessageBoxA(hMainWindow,
            "Nao foi possivel criar o ficheiro de log.",
            "Erro de Log", MB_OK | MB_ICONERROR);
        hLogCSV  = NULL;
        logAtivo = 0;
        return;
    }

    /* Cabecalho CSV */
    fprintf(hLogCSV,
        "timestamp,cpu_pct,ram_pct,disk_read_bps,disk_write_bps,"
        "net_down_bps,net_up_bps");

    {
        int i;
        for (i = 0; i < numNucleosMonitorizados; i++)
            fprintf(hLogCSV, ",core%d_pct", i);
        for (i = 0; i < numZonasTemp; i++)
            fprintf(hLogCSV, ",temp_zona%d_c", i);
    }

    fprintf(hLogCSV, "\n");
    fflush(hLogCSV);

    logAtivo      = 1;
    logTickContador = 0;
}

static void FecharLogCSV(void) {
    if (hLogCSV) {
        fclose(hLogCSV);
        hLogCSV  = NULL;
    }
    logAtivo = 0;
}

static void EscreverLinhaLog(void) {
    SYSTEMTIME st;
    int i;

    if (!logAtivo || !hLogCSV) return;

    logTickContador++;
    if (logTickContador < LOG_INTERVALO_SEGUNDOS) return;
    logTickContador = 0;

    GetLocalTime(&st);

    fprintf(hLogCSV,
        "%04d-%02d-%02d %02d:%02d:%02d,"
        "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond,
        ultimoCpuPercent, ultimoRamPercent,
        ultimoDiskRead,   ultimoDiskWrite,
        ultimoNetDown,    ultimoNetUp);

    for (i = 0; i < numNucleosMonitorizados; i++)
        fprintf(hLogCSV, ",%.2f", coresCpuAtuais[i]);

    for (i = 0; i < numZonasTemp; i++)
        fprintf(hLogCSV, ",%.1f", tempAtual[i]);

    fprintf(hLogCSV, "\n");
    fflush(hLogCSV);
}

/* ========================================================================= */
/* FUNCIONALIDADE 2 — Alertas configuráveis + persistência .ini              */
/* ========================================================================= */

#define INI_FICHEIRO "winmon.ini"
#define INI_SECAO    "Alertas"

static void CarregarConfigIni(void) {
    char val[32];

    GetPrivateProfileStringA(INI_SECAO, "LimiteCPU",    "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteCpuPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteRAM",    "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteRamPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteDisco",  "95.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteDiscoPercent = atof(val);

    /* Validacao basica */
    if (limiteCpuPercent   < 1.0 || limiteCpuPercent   > 100.0) limiteCpuPercent   = LIMITE_CPU_PERCENT_DEFAULT;
    if (limiteRamPercent   < 1.0 || limiteRamPercent   > 100.0) limiteRamPercent   = LIMITE_RAM_PERCENT_DEFAULT;
    if (limiteDiscoPercent < 1.0 || limiteDiscoPercent > 100.0) limiteDiscoPercent = LIMITE_DISCO_PERCENT_DEFAULT;
}

static void GravarConfigIni(void) {
    char val[32];

    snprintf(val, sizeof(val), "%.1f", limiteCpuPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteCPU",   val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteRamPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteRAM",   val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteDiscoPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteDisco", val, INI_FICHEIRO);
}

/* IDs dos controlos do dialogo de alertas */
#define IDC_EDIT_CPU    3001
#define IDC_EDIT_RAM    3002
#define IDC_EDIT_DISCO  3003

static INT_PTR CALLBACK DialogoAlertasProc(HWND hDlg, UINT msg,
                                            WPARAM wParam, LPARAM lParam) {
    (void)lParam;

    switch (msg) {
        case WM_INITDIALOG: {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", limiteCpuPercent);
            SetDlgItemTextA(hDlg, IDC_EDIT_CPU, buf);

            snprintf(buf, sizeof(buf), "%.1f", limiteRamPercent);
            SetDlgItemTextA(hDlg, IDC_EDIT_RAM, buf);

            snprintf(buf, sizeof(buf), "%.1f", limiteDiscoPercent);
            SetDlgItemTextA(hDlg, IDC_EDIT_DISCO, buf);
            return TRUE;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                char buf[16];
                double v;

                GetDlgItemTextA(hDlg, IDC_EDIT_CPU,   buf, sizeof(buf)); v = atof(buf);
                if (v >= 1.0 && v <= 100.0) limiteCpuPercent   = v;

                GetDlgItemTextA(hDlg, IDC_EDIT_RAM,   buf, sizeof(buf)); v = atof(buf);
                if (v >= 1.0 && v <= 100.0) limiteRamPercent   = v;

                GetDlgItemTextA(hDlg, IDC_EDIT_DISCO, buf, sizeof(buf)); v = atof(buf);
                if (v >= 1.0 && v <= 100.0) limiteDiscoPercent = v;

                GravarConfigIni();
                EndDialog(hDlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

/*
 * Cria o dialogo de configuracao de alertas em memoria (sem ficheiro .rc).
 * Layout: 3 linhas de label + edit, botoes OK/Cancelar.
 */
static void MostrarDialogoAlertas(HWND hwndPai) {
    /*
     * Construir um DLGTEMPLATE em memoria.
     * Estrutura: DLGTEMPLATE | DLGITEMTEMPLATE* (alinhados a DWORD).
     */
    #define DLG_ITEMS 5   /* 3 labels + 3 edits + 2 botoes = 8 itens */

    /* Usamos um buffer estatico suficientemente grande. */
    static WORD dlgBuf[512];
    WORD *p = dlgBuf;

    /* Helper: escrever uma string UNICODE inline no template. */
    #define WRITE_STR_W(s) \
        do { \
            const wchar_t *_ws = (s); \
            while (*_ws) *p++ = (WORD)*_ws++; \
            *p++ = 0; \
        } while(0)

    #define ALIGN_DWORD() \
        if (((ULONG_PTR)p) & 2) p++

    /* ---- DLGTEMPLATE ---- */
    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style      = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit       = 8;   /* nr de itens */
    dt->x          = 0; dt->y = 0;
    dt->cx         = 200; dt->cy = 130;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);

    /* menu = 0, windowClass = 0, title */
    *p++ = 0; *p++ = 0;
    WRITE_STR_W(L"Configurar Limites de Alerta (%)");
    /* fonte */
    *p++ = 9; /* tamanho em pt */
    WRITE_STR_W(L"Segoe UI");

    /* ---- Macro para cada item ---- */
    #define ADD_ITEM(sty, ex, xx, yy, ww, hh, iid, cls, txt) \
        do { \
            ALIGN_DWORD(); \
            { DLGITEMTEMPLATE *_it = (DLGITEMTEMPLATE *)p; \
              _it->style = (sty); _it->dwExtendedStyle = (ex); \
              _it->x = (xx); _it->y = (yy); \
              _it->cx = (ww); _it->cy = (hh); \
              _it->id = (iid); \
              p += sizeof(DLGITEMTEMPLATE)/sizeof(WORD); } \
            *p++ = 0xFFFF; *p++ = (cls); /* classe pre-definida */ \
            WRITE_STR_W(txt); \
            *p++ = 0; /* sem dados extra */ \
        } while(0)

    /* Labels */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT, 0,
             7,  14, 70, 10, -1,  0x0082 /*STATIC*/, L"Limite CPU (%):");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT, 0,
             7,  34, 70, 10, -1,  0x0082,             L"Limite RAM (%):");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT, 0,
             7,  54, 70, 10, -1,  0x0082,             L"Limite Disco (%):");

    /* Edits */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 0,
             80, 12, 40, 12, IDC_EDIT_CPU,   0x0081 /*EDIT*/, L"");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 0,
             80, 32, 40, 12, IDC_EDIT_RAM,   0x0081,           L"");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER, 0,
             80, 52, 40, 12, IDC_EDIT_DISCO, 0x0081,           L"");

    /* Botoes */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 0,
             34, 108, 60, 14, IDOK,     0x0080 /*BUTTON*/, L"OK");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,    0,
             106,108, 60, 14, IDCANCEL, 0x0080,             L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL),
                       (LPDLGTEMPLATE)dlgBuf,
                       hwndPai,
                       DialogoAlertasProc);

    #undef WRITE_STR_W
    #undef ALIGN_DWORD
    #undef ADD_ITEM
    #undef DLG_ITEMS
}

/* ========================================================================= */
/* FUNCIONALIDADE 3 — Notificacoes balloon com cooldown                      */
/* ========================================================================= */

static void EnviarBalloon(const char *titulo, const char *msg) {
    ULONGLONG agora = GetTickCount64();

    /* Respeitar cooldown */
    if (agora - ultimoBalloonTick <
        (ULONGLONG)BALLOON_COOLDOWN_SEGUNDOS * 1000ULL) return;

    /* Garantir que o icone de tray esta ativo */
    if (!trayIconAtivo) {
        Shell_NotifyIconA(NIM_ADD, &nid);
        trayIconAtivo = 1;
    }

    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    strncpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), titulo, _TRUNCATE);
    strncpy_s(nid.szInfo,      sizeof(nid.szInfo),      msg,    _TRUNCATE);
    nid.uTimeout = 5000;

    Shell_NotifyIconA(NIM_MODIFY, &nid);

    nid.uFlags &= ~NIF_INFO; /* limpar para nao repetir */
    ultimoBalloonTick = agora;
}

static void VerificarAlertasBalloon(void) {
    if (!alertaGlobalAtivo) return;

    {
        char msgBalloon[256];
        char partes[3][64];
        int n = 0;

        partes[0][0] = partes[1][0] = partes[2][0] = '\0';

        if (ultimoCpuPercent > limiteCpuPercent)
            snprintf(partes[n++], 64, "CPU %.0f%%", ultimoCpuPercent);
        if (ultimoRamPercent > limiteRamPercent)
            snprintf(partes[n++], 64, "RAM %.0f%%", ultimoRamPercent);

        if (n == 0) return; /* so disco => sem balloon (ja visivel no titulo) */

        msgBalloon[0] = '\0';
        {
            int i;
            for (i = 0; i < n; i++) {
                if (i > 0) strncat_s(msgBalloon, sizeof(msgBalloon), " | ", _TRUNCATE);
                strncat_s(msgBalloon, sizeof(msgBalloon), partes[i], _TRUNCATE);
            }
        }

        EnviarBalloon("WinMon — Alerta de Recursos", msgBalloon);
    }
}

/* ========================================================================= */
/* FUNCIONALIDADE 4 — Temperatura da CPU via WMI                             */
/*                                                                           */
/* Usa MSAcpi_ThermalZoneTemperature em root\wmi. Requer elevacao UAC,       */
/* garantida pelo manifesto inline (/manifestuac:requireAdministrator).      */
/*                                                                           */
/* InicializarTemperaturaCPU() — chama CoInitializeEx uma vez no WM_CREATE. */
/* LerTemperaturaCPU()         — abre/fecha a query a cada tick para nao    */
/*                               manter IWbemServices vivo entre ciclos.    */
/* ========================================================================= */

static IWbemLocator  *g_pWbemLoc = NULL;
static IWbemServices *g_pWbemSvc = NULL;
static int            g_wmiPronto = 0;

static void InicializarTemperaturaCPU(void) {
    HRESULT hr;

    numZonasTemp = 0;
    g_wmiPronto  = 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return;

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    /* E_ALREADY_INITIALIZED e aceitavel */
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) return;

    hr = CoCreateInstance(
        &CLSID_WbemLocator, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IWbemLocator,
        (LPVOID *)&g_pWbemLoc);
    if (FAILED(hr) || !g_pWbemLoc) return;

    {
        BSTR bstrNs = SysAllocString(L"ROOT\\WMI");
        if (!bstrNs) return;

        hr = g_pWbemLoc->lpVtbl->ConnectServer(
            g_pWbemLoc, bstrNs,
            NULL, NULL, NULL, 0, NULL, NULL,
            &g_pWbemSvc);

        SysFreeString(bstrNs);
    }

    if (FAILED(hr) || !g_pWbemSvc) return;

    hr = CoSetProxyBlanket(
        (IUnknown *)g_pWbemSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE);

    if (FAILED(hr)) return;

    g_wmiPronto = 1;
}

static void LerTemperaturaCPU(char *buffer, size_t size, size_t *offset) {
    HRESULT          hr;
    BSTR             bstrQuery = NULL;
    BSTR             bstrWql   = NULL;
    IEnumWbemClassObject *pEnum = NULL;
    IWbemClassObject     *pObj  = NULL;
    ULONG            retorno;
    int              algumValido = 0;

    if (!g_wmiPronto || !g_pWbemSvc) return;

    numZonasTemp = 0;

    bstrWql   = SysAllocString(L"WQL");
    bstrQuery = SysAllocString(
        L"SELECT InstanceName, CurrentTemperature "
        L"FROM MSAcpi_ThermalZoneTemperature");

    if (!bstrWql || !bstrQuery) goto cleanup;

    hr = g_pWbemSvc->lpVtbl->ExecQuery(
        g_pWbemSvc, bstrWql, bstrQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnum);

    if (FAILED(hr) || !pEnum) goto cleanup;

    while (numZonasTemp < MAX_TEMP_ZONAS) {
        VARIANT vtTemp, vtName;
        double  celsius;

        hr = pEnum->lpVtbl->Next(pEnum,
            WBEM_INFINITE, 1, &pObj, &retorno);
        if (FAILED(hr) || retorno == 0) break;

        VariantInit(&vtTemp);
        VariantInit(&vtName);

        /* CurrentTemperature: decimos de Kelvin (ex: 3232 = 323.2 K) */
        hr = pObj->lpVtbl->Get(pObj,
            L"CurrentTemperature", 0, &vtTemp, NULL, NULL);
        if (SUCCEEDED(hr) && vtTemp.vt == VT_I4) {
            celsius = (vtTemp.lVal / 10.0) - 273.15;
        } else if (SUCCEEDED(hr) && vtTemp.vt == VT_R8) {
            celsius = (vtTemp.dblVal / 10.0) - 273.15;
        } else {
            VariantClear(&vtTemp);
            VariantClear(&vtName);
            pObj->lpVtbl->Release(pObj);
            continue;
        }

        /* Sanidade */
        if (celsius < -10.0 || celsius > 150.0) {
            VariantClear(&vtTemp);
            VariantClear(&vtName);
            pObj->lpVtbl->Release(pObj);
            continue;
        }

        tempAtual[numZonasTemp] = celsius;
        algumValido = 1;

        /* InstanceName para label */
        hr = pObj->lpVtbl->Get(pObj,
            L"InstanceName", 0, &vtName, NULL, NULL);
        if (SUCCEEDED(hr) && vtName.vt == VT_BSTR && vtName.bstrVal) {
            WideCharToMultiByte(CP_ACP, 0,
                vtName.bstrVal, -1,
                tempNome[numZonasTemp],
                sizeof(tempNome[numZonasTemp]),
                NULL, NULL);
        } else {
            snprintf(tempNome[numZonasTemp],
                     sizeof(tempNome[numZonasTemp]),
                     "Zona %d", numZonasTemp);
        }

        VariantClear(&vtTemp);
        VariantClear(&vtName);
        pObj->lpVtbl->Release(pObj);
        pObj = NULL;

        numZonasTemp++;
    }

cleanup:
    if (pObj)   pObj->lpVtbl->Release(pObj);
    if (pEnum)  pEnum->lpVtbl->Release(pEnum);
    SysFreeString(bstrQuery);
    SysFreeString(bstrWql);

    if (!algumValido || numZonasTemp == 0) return;

    if (buffer && size > 0 && offset) {
        int i;
        AppendFormat(buffer, size, offset,
            "=== [ TEMPERATURA CPU (WMI) ] ===\r\n");

        for (i = 0; i < numZonasTemp; i++) {
            /* Encurtar o InstanceName para mostrar so a parte final */
            const char *label = tempNome[i];
            const char *barra = strrchr(label, '\\');
            if (barra) label = barra + 1;

            AppendFormat(buffer, size, offset,
                "%-14s %.1f \xB0\x43\r\n", label, tempAtual[i]);
        }

        AppendFormat(buffer, size, offset, "\r\n");
    }
}

static void TerminarWMI(void) {
    if (g_pWbemSvc) {
        g_pWbemSvc->lpVtbl->Release(g_pWbemSvc);
        g_pWbemSvc = NULL;
    }
    if (g_pWbemLoc) {
        g_pWbemLoc->lpVtbl->Release(g_pWbemLoc);
        g_pWbemLoc = NULL;
    }
    CoUninitialize();
    g_wmiPronto = 0;
}

/* ========================================================================= */
/* OVERLAY ALARGADO (estilo RTSS) — horizontal, multi-coluna, configurável   */
/* ========================================================================= */

/*
 * Layout horizontal:
 *
 *   [ WinMon ][ CPU ][ RAM ][ NET ][ TEMP ][ DISCO ]
 *
 * Cada coluna tem:
 *   - Label colorido (label row)
 *   - Valor principal grande
 *   - Mini-barra de progresso (onde aplicável)
 *   - Sub-valor (ex.: MHz para CPU, MB usados para RAM, ↓↑ para NET)
 *
 * A janela redimensiona-se automaticamente consoante o tamanho de fonte
 * (ovFontSize). O botão direito abre o diálogo de configuração.
 * O botão esquerdo arrasta. Duplo-clique fecha.
 */

#define OV_PAD         6    /* padding interno horizontal/vertical */
#define OV_BAR_H       5    /* altura da mini-barra de progresso */
#define OV_MARGIN_SCR  12   /* distância ao canto do ecrã */
#define OV_BG          RGB(14, 14, 16)
#define OV_BORDA       RGB(60, 63, 70)
#define OV_SEP         RGB(45, 47, 52)  /* cor do separador entre colunas */

static BOOL  ovArrastar   = FALSE;
static POINT ovPtArrastar = {0, 0};

/* ---- Persistência no Registry ------------------------------------------ */

static void GravarConfigOverlay(void) {
    HKEY hk;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
            "Software\\WinMon\\Overlay", 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return;

    DWORD v;
    v = (DWORD)ovFontSize;
    RegSetValueExA(hk, "FontSize", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    RegSetValueExA(hk, "FontName", 0, REG_SZ,
                   (BYTE*)ovFontName, (DWORD)(strlen(ovFontName)+1));
    v = (DWORD)ovOpacity;
    RegSetValueExA(hk, "Opacity", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    v = (DWORD)ovMostrarTemp;
    RegSetValueExA(hk, "ShowTemp", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    v = (DWORD)ovMostrarDisco;
    RegSetValueExA(hk, "ShowDisk", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
    RegCloseKey(hk);
}

static void CarregarConfigOverlay(void) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\WinMon\\Overlay", 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return;

    DWORD v, sz = sizeof(DWORD);
    if (RegQueryValueExA(hk, "FontSize", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS)
        ovFontSize = (int)v;
    sz = sizeof(ovFontName);
    RegQueryValueExA(hk, "FontName", NULL, NULL, (BYTE*)ovFontName, &sz);
    sz = sizeof(DWORD);
    if (RegQueryValueExA(hk, "Opacity", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS)
        ovOpacity = (BYTE)v;
    if (RegQueryValueExA(hk, "ShowTemp", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS)
        ovMostrarTemp = (int)v;
    if (RegQueryValueExA(hk, "ShowDisk", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS)
        ovMostrarDisco = (int)v;
    RegCloseKey(hk);

    /* Limites de segurança */
    if (ovFontSize < OV_FONT_MIN) ovFontSize = OV_FONT_MIN;
    if (ovFontSize > OV_FONT_MAX) ovFontSize = OV_FONT_MAX;
    if (ovFontName[0] == '\0') strncpy_s(ovFontName, sizeof(ovFontName), "Consolas", _TRUNCATE);
    if (ovOpacity < 30)  ovOpacity = 30;
    if (ovOpacity > 255) ovOpacity = 255;
}

/* ---- Cálculo dinâmico do tamanho da janela ----------------------------- */

/*
 * Recalcula ovColW, ovRowH, ovTotalW, ovTotalH com base em ovFontSize.
 * Deve ser chamada sempre que ovFontSize ou as colunas visíveis mudam.
 * Se o overlay já existe, aplica o novo tamanho imediatamente.
 */
static void RecalcOverlaySize(void) {
    /* Estimativa proporcional: Consolas 13pt ≈ largura de char 8px, alt. linha 20px */
    int charW   = (ovFontSize * 8)  / 13;
    if (charW < 5) charW = 5;
    int lineH   = (ovFontSize * 20) / 13;
    if (lineH < 12) lineH = 12;

    ovRowH  = lineH;

    /* Largura da coluna: suficiente para ~11 chars + padding */
    ovColW  = charW * 11 + OV_PAD * 2;
    if (ovColW < 80) ovColW = 80;

    /* Número de colunas visíveis: CPU + RAM + NET + (TEMP?) + (DISCO?) */
    int ncols = 3;
    if (ovMostrarTemp)  ncols++;
    if (ovMostrarDisco) ncols++;

    /* Cada coluna: 1 linha label + 1 linha valor grande + barra + 1 linha sub-valor */
    int nlinhas = 3;   /* label | valor | sub */

    ovTotalW = OV_PAD + ncols * ovColW + OV_PAD;
    ovTotalH = OV_PAD + nlinhas * ovRowH + OV_BAR_H + OV_PAD * 2;

    if (hOverlay && overlayAtivo) {
        /* Preservar posição, só alterar tamanho */
        RECT wr;
        GetWindowRect(hOverlay, &wr);
        SetWindowPos(hOverlay, HWND_TOPMOST,
            wr.left, wr.top, ovTotalW, ovTotalH,
            SWP_NOACTIVATE);
    }
}

/* ---- Desenho de uma mini-barra de progresso ----------------------------- */

static void DrawMiniBar(HDC hdc, int x, int y, int w, double pct,
                        COLORREF corFill, COLORREF corBg) {
    RECT bar = { x, y, x + w, y + OV_BAR_H };
    HBRUSH bg = CreateSolidBrush(corBg);
    FillRect(hdc, &bar, bg);
    DeleteObject(bg);

    if (pct > 0.0) {
        int fill = (int)(w * (pct / 100.0));
        if (fill > w) fill = w;
        if (fill > 0) {
            RECT filled = { x, y, x + fill, y + OV_BAR_H };
            HBRUSH fg = CreateSolidBrush(corFill);
            FillRect(hdc, &filled, fg);
            DeleteObject(fg);
        }
    }
}

/* ---- Desenha uma coluna de métrica -------------------------------------- */

typedef struct {
    const char *label;
    COLORREF    corLabel;
    const char *valorStr;    /* linha principal */
    const char *subStr;      /* linha secundária (pode ser NULL) */
    double      barPct;      /* -1 = sem barra */
    COLORREF    corBar;
} OvColuna;

static void DrawOvColuna(HDC hdc, int x, int y,
                         HFONT fLabel, HFONT fValor, HFONT fSub,
                         const OvColuna *c) {
    int lh = ovRowH;
    int cw = ovColW;
    RECT tr;

    /* Label */
    SelectObject(hdc, fLabel);
    SetTextColor(hdc, c->corLabel);
    tr = (RECT){ x + OV_PAD, y, x + cw - OV_PAD, y + lh };
    DrawTextA(hdc, c->label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    /* Valor principal */
    SelectObject(hdc, fValor);
    SetTextColor(hdc, RGB(230, 232, 235));
    tr = (RECT){ x + OV_PAD, y, x + cw - OV_PAD, y + lh };
    DrawTextA(hdc, c->valorStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    /* Mini-barra */
    if (c->barPct >= 0.0) {
        DrawMiniBar(hdc, x + OV_PAD, y, cw - OV_PAD * 2,
                    c->barPct, c->corBar, RGB(40, 42, 46));
    }
    y += OV_BAR_H + 2;

    /* Sub-valor */
    if (c->subStr && c->subStr[0]) {
        SelectObject(hdc, fSub);
        SetTextColor(hdc, RGB(150, 153, 158));
        tr = (RECT){ x + OV_PAD, y, x + cw - OV_PAD, y + lh };
        DrawTextA(hdc, c->subStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

/* ---- OverlayProc -------------------------------------------------------- */

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT uMsg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        /* Fundo */
        HBRUSH bgBrush = CreateSolidBrush(OV_BG);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        /* Borda */
        HPEN penBorda = CreatePen(PS_SOLID, 1, OV_BORDA);
        HPEN penVelho = (HPEN)SelectObject(hdc, penBorda);
        MoveToEx(hdc, 0, 0, NULL);
        LineTo(hdc, rc.right-1, 0);
        LineTo(hdc, rc.right-1, rc.bottom-1);
        LineTo(hdc, 0, rc.bottom-1);
        LineTo(hdc, 0, 0);
        SelectObject(hdc, penVelho);
        DeleteObject(penBorda);

        SetBkMode(hdc, TRANSPARENT);

        /* Fontes */
        HFONT fLabel = CreateFontA(
            ovFontSize - 1, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, ovFontName);
        HFONT fValor = CreateFontA(
            ovFontSize + 1, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, ovFontName);
        HFONT fSub = CreateFontA(
            max(ovFontSize - 3, OV_FONT_MIN), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, ovFontName);

        HFONT fOrig = (HFONT)SelectObject(hdc, fLabel);

        int cx = OV_PAD;
        int cy = OV_PAD;
        OvColuna col;
        char vBuf[64], sBuf[64];
        char dBuf[32], uBuf[32];
        HPEN penSep = CreatePen(PS_SOLID, 1, OV_SEP);

        /* ---- CPU ---- */
        snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoCpuPercent);
        /* Sub: temperatura média se disponível */
        if (numZonasTemp > 0) {
            double t = 0.0;
            int i;
            for (i = 0; i < numZonasTemp; i++) t += tempAtual[i];
            t /= numZonasTemp;
            snprintf(sBuf, sizeof(sBuf), "%.0f\xB0" "C", t);
        } else {
            snprintf(sBuf, sizeof(sBuf), "%d cores", numNucleosMonitorizados);
        }
        col = (OvColuna){ "CPU", RGB(100, 190, 255), vBuf, sBuf,
                          ultimoCpuPercent, RGB(100, 190, 255) };
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        SelectObject(hdc, penSep);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, cx, rc.bottom - OV_PAD);

        /* ---- RAM ---- */
        {
            MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
            DWORDLONG usedMB = 0, totalMB = 0;
            if (GlobalMemoryStatusEx(&ms)) {
                totalMB = ms.ullTotalPhys  / (1024*1024);
                usedMB  = totalMB - ms.ullAvailPhys / (1024*1024);
            }
            snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoRamPercent);
            snprintf(sBuf, sizeof(sBuf), "%llu/%llu MB",
                     (unsigned long long)usedMB,
                     (unsigned long long)totalMB);
        }
        col = (OvColuna){ "RAM", RGB(110, 220, 140), vBuf, sBuf,
                          ultimoRamPercent, RGB(110, 220, 140) };
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        SelectObject(hdc, penSep);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, cx, rc.bottom - OV_PAD);

        /* ---- NET ---- */
        FormatarBytes(ultimoNetDown, dBuf, sizeof(dBuf));
        FormatarBytes(ultimoNetUp,   uBuf, sizeof(uBuf));
        snprintf(vBuf, sizeof(vBuf), "\x19%s/s", dBuf);
        snprintf(sBuf, sizeof(sBuf), "\x18%s/s", uBuf);
        col = (OvColuna){ "NET", RGB(255, 195, 80), vBuf, sBuf, -1.0, 0 };
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        /* ---- TEMP (opcional) ---- */
        if (ovMostrarTemp) {
            SelectObject(hdc, penSep);
            MoveToEx(hdc, cx, cy, NULL);
            LineTo(hdc, cx, rc.bottom - OV_PAD);

            if (numZonasTemp > 0) {
                double tMax = 0.0, tMedia = 0.0;
                int i;
                for (i = 0; i < numZonasTemp; i++) {
                    tMedia += tempAtual[i];
                    if (tempAtual[i] > tMax) tMax = tempAtual[i];
                }
                tMedia /= numZonasTemp;
                snprintf(vBuf, sizeof(vBuf), "%.0f\xB0" "C", tMedia);
                snprintf(sBuf, sizeof(sBuf), "max %.0f\xB0" "C", tMax);
                double barPct = (tMax > 0.0) ? (tMedia / 110.0) * 100.0 : 0.0;
                COLORREF cTemp = (tMedia > 85.0) ? RGB(255,80,80)
                               : (tMedia > 65.0) ? RGB(255,180,50)
                               :                   RGB(80, 200,160);
                col = (OvColuna){ "TEMP", RGB(255, 140, 140), vBuf, sBuf,
                                  barPct, cTemp };
            } else {
                col = (OvColuna){ "TEMP", RGB(255, 140, 140), "N/A", "WMI off", -1.0, 0 };
            }
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        /* ---- DISCO (opcional) ---- */
        if (ovMostrarDisco) {
            SelectObject(hdc, penSep);
            MoveToEx(hdc, cx, cy, NULL);
            LineTo(hdc, cx, rc.bottom - OV_PAD);

            FormatarBytes(ultimoDiskRead,  dBuf, sizeof(dBuf));
            FormatarBytes(ultimoDiskWrite, uBuf, sizeof(uBuf));
            snprintf(vBuf, sizeof(vBuf), "R %s/s", dBuf);
            snprintf(sBuf, sizeof(sBuf), "W %s/s", uBuf);
            col = (OvColuna){ "DISCO", RGB(180, 140, 255), vBuf, sBuf, -1.0, 0 };
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        }

        SelectObject(hdc, penVelho);
        DeleteObject(penSep);
        SelectObject(hdc, fOrig);
        DeleteObject(fLabel);
        DeleteObject(fValor);
        DeleteObject(fSub);

        EndPaint(hwnd, &ps);
        return 0;
    }

    /* Arrastar com botão esquerdo */
    case WM_LBUTTONDOWN:
        ovArrastar = TRUE;
        ovPtArrastar.x = LOWORD(lParam);
        ovPtArrastar.y = HIWORD(lParam);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (ovArrastar) {
            POINT pt;
            RECT  wr;
            GetCursorPos(&pt);
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, NULL,
                wr.left + (pt.x - wr.left - ovPtArrastar.x),
                wr.top  + (pt.y - wr.top  - ovPtArrastar.y),
                0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_LBUTTONUP:
        ovArrastar = FALSE;
        ReleaseCapture();
        return 0;

    /* Botão direito → diálogo de configuração */
    case WM_RBUTTONUP:
        MostrarDialogoConfigOverlay(hwnd);
        return 0;

    /* Duplo-clique → fechar */
    case WM_LBUTTONDBLCLK:
        ToggleOverlay();
        return 0;

    /* Scroll do rato → ajustar tamanho de fonte */
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        ovFontSize += (delta > 0) ? 1 : -1;
        if (ovFontSize < OV_FONT_MIN) ovFontSize = OV_FONT_MIN;
        if (ovFontSize > OV_FONT_MAX) ovFontSize = OV_FONT_MAX;
        RecalcOverlaySize();
        InvalidateRect(hwnd, NULL, FALSE);
        GravarConfigOverlay();
        return 0;
    }

    case WM_DESTROY:
        hOverlay     = NULL;
        overlayAtivo = 0;
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

/* ---- Diálogo de configuração do overlay --------------------------------- */

#define IDC_OV_FONTSIZE  3001
#define IDC_OV_FONTNAME  3002
#define IDC_OV_OPACITY   3003
#define IDC_OV_TEMP      3004
#define IDC_OV_DISCO     3005

static INT_PTR CALLBACK DialogoConfigOverlayProc(HWND hDlg, UINT uMsg,
                                                  WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (uMsg) {
    case WM_INITDIALOG: {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", ovFontSize);
        SetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf);
        SetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovFontName);
        snprintf(buf, sizeof(buf), "%d", (int)ovOpacity);
        SetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf);
        CheckDlgButton(hDlg, IDC_OV_TEMP,  ovMostrarTemp  ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_DISCO, ovMostrarDisco ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            char buf[64];
            /* Tamanho de fonte */
            GetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf, sizeof(buf));
            int fs = atoi(buf);
            if (fs >= OV_FONT_MIN && fs <= OV_FONT_MAX) ovFontSize = fs;
            /* Nome da fonte */
            GetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovFontName, sizeof(ovFontName));
            if (ovFontName[0] == '\0')
                strncpy_s(ovFontName, sizeof(ovFontName), "Consolas", _TRUNCATE);
            /* Opacidade */
            GetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf, sizeof(buf));
            int op = atoi(buf);
            if (op < 30)  op = 30;
            if (op > 255) op = 255;
            ovOpacity = (BYTE)op;
            /* Colunas */
            ovMostrarTemp  = (IsDlgButtonChecked(hDlg, IDC_OV_TEMP)  == BST_CHECKED) ? 1 : 0;
            ovMostrarDisco = (IsDlgButtonChecked(hDlg, IDC_OV_DISCO) == BST_CHECKED) ? 1 : 0;

            /* Aplicar imediatamente */
            if (hOverlay && overlayAtivo) {
                SetLayeredWindowAttributes(hOverlay, 0, ovOpacity, LWA_ALPHA);
                RecalcOverlaySize();
                InvalidateRect(hOverlay, NULL, FALSE);
            }
            GravarConfigOverlay();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void MostrarDialogoConfigOverlay(HWND hwndPai) {
    static WORD dlgBuf[640];
    WORD *p = dlgBuf;

    #define WRITE_STR_W(s) \
        do { const wchar_t *_ws=(s); while(*_ws) *p++=(WORD)*_ws++; *p++=0; } while(0)
    #define ALIGN_DWORD() if(((ULONG_PTR)p)&2) p++
    #define ADD_ITEM(sty,ex,xx,yy,ww,hh,iid,cls,txt) \
        do { ALIGN_DWORD(); \
             { DLGITEMTEMPLATE *_it=(DLGITEMTEMPLATE*)p; \
               _it->style=(sty); _it->dwExtendedStyle=(ex); \
               _it->x=(xx); _it->y=(yy); _it->cx=(ww); _it->cy=(hh); \
               _it->id=(iid); \
               p+=sizeof(DLGITEMTEMPLATE)/sizeof(WORD); } \
             *p++=0xFFFF; *p++=(cls); \
             WRITE_STR_W(txt); \
             *p++=0; } while(0)

    /* DLGTEMPLATE */
    DLGTEMPLATE *dt = (DLGTEMPLATE*)p;
    dt->style = WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER|DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit  = 12;   /* labels + edits + checks + botoes */
    dt->x=0; dt->y=0; dt->cx=220; dt->cy=155;
    p += sizeof(DLGTEMPLATE)/sizeof(WORD);
    *p++=0; *p++=0;
    WRITE_STR_W(L"Configurar Overlay");
    *p++=9; WRITE_STR_W(L"Segoe UI");

    /* Linha 1: Tamanho de fonte */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT,0,  7, 12, 90,10, -1,         0x0082,L"Tamanho fonte (pt):");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,0, 100,10, 35,12, IDC_OV_FONTSIZE, 0x0081,L"");

    /* Linha 2: Nome da fonte */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT,0,  7, 30, 90,10, -1,         0x0082,L"Fonte:");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER,  0, 100,28,110,12, IDC_OV_FONTNAME, 0x0081,L"");

    /* Linha 3: Opacidade */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT,0,  7, 48, 90,10, -1,         0x0082,L"Opacidade (30-255):");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,0,100,46, 35,12, IDC_OV_OPACITY,  0x0081,L"");

    /* Checkboxes */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,0, 7,66,100,12, IDC_OV_TEMP,  0x0080,L"Mostrar Temperatura");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,0, 7,82,100,12, IDC_OV_DISCO, 0x0080,L"Mostrar Disco I/O");

    /* Dica scroll */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|SS_LEFT,0, 7,100,206,10, -1, 0x0082,
             L"Dica: scroll do rato sobre o overlay ajusta a fonte.");

    /* Botoes */
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,0,  40,128, 60,14, IDOK,     0x0080,L"OK");
    ADD_ITEM(WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,   0, 114,128, 60,14, IDCANCEL, 0x0080,L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL),
                       (LPDLGTEMPLATE)dlgBuf, hwndPai,
                       DialogoConfigOverlayProc);

    #undef WRITE_STR_W
    #undef ALIGN_DWORD
    #undef ADD_ITEM
}

/* ---- Ciclo de vida do overlay ------------------------------------------- */

static void CriarOverlay(void) {
    HINSTANCE hInst = GetModuleHandle(NULL);

    CarregarConfigOverlay();
    RecalcOverlaySize();   /* calcula ovTotalW / ovTotalH */

    /* Registar classe (ignora erro se já registada) */
    {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc   = OverlayProc;
        wc.hInstance     = hInst;
        wc.lpszClassName = OVERLAY_CLASS_NAME;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.style         = CS_DBLCLKS;
        RegisterClassA(&wc);
    }

    /* Posição inicial: canto superior-direito da área de trabalho */
    RECT wa = {0};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right  - ovTotalW - OV_MARGIN_SCR;
    int y = wa.top    + OV_MARGIN_SCR;

    hOverlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME, "WinMon Overlay",
        WS_POPUP,
        x, y, ovTotalW, ovTotalH,
        NULL, NULL, hInst, NULL);

    if (!hOverlay) return;

    SetLayeredWindowAttributes(hOverlay, 0, ovOpacity, LWA_ALPHA);
    ShowWindow(hOverlay, SW_SHOWNOACTIVATE);
    UpdateWindow(hOverlay);
    overlayAtivo = 1;
}

static void FecharOverlay(void) {
    if (hOverlay) {
        DestroyWindow(hOverlay);
        hOverlay     = NULL;
    }
    overlayAtivo = 0;
}

static void ToggleOverlay(void) {
    if (overlayAtivo) FecharOverlay();
    else              CriarOverlay();
}

static void AtualizarOverlay(void) {
    if (hOverlay && overlayAtivo)
        InvalidateRect(hOverlay, NULL, FALSE);
}

/* ========================================================================= */

void AtualizarMonitor() {
    static char buffer[BUFFER_SIZE];
    size_t offset = 0;

    InicializarBuffer(buffer, sizeof(buffer), &offset);

    totalIntervalosAlerta = 0;
    alertaGlobalAtivo = 0;

    MonitorarSistema(buffer, BUFFER_SIZE, &offset);
    MonitorarCPU(buffer, BUFFER_SIZE, &offset);
    LerTemperaturaCPU(buffer, BUFFER_SIZE, &offset);
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
    VerificarAlertasBalloon();
    EscreverLinhaLog();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);

    AtualizarOverlay();
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

            /* Temperatura via WMI (independente do PDH). */
            InicializarTemperaturaCPU();

            /* Carregar configuracao de alertas do .ini (se existir). */
            CarregarConfigIni();

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
            if (LOWORD(wParam) == ID_TOGGLE_LOG) {
                if (logAtivo) {
                    FecharLogCSV();
                    MessageBoxA(hwnd, "Log CSV interrompido.", "WinMon", MB_OK | MB_ICONINFORMATION);
                } else {
                    IniciarLogCSV();
                    if (logAtivo) {
                        char msg[MAX_PATH + 64];
                        snprintf(msg, sizeof(msg), "Log CSV iniciado:\n%s", logNomeFicheiro);
                        MessageBoxA(hwnd, msg, "WinMon", MB_OK | MB_ICONINFORMATION);
                    }
                }
                return 0;
            }
            if (LOWORD(wParam) == ID_CONFIG_ALERTAS) {
                MostrarDialogoAlertas(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == ID_TOGGLE_OVERLAY) {
                ToggleOverlay();
                return 0;
            }
            if (LOWORD(wParam) == ID_CONFIG_OVERLAY) {
                MostrarDialogoConfigOverlay(hwnd);
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TIMER:
            if (wParam == TIMER_ID)
                AtualizarMonitor();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);

            FecharOverlay();

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

            FecharLogCSV();
            TerminarWMI();

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
        { FVIRTKEY | FCONTROL, 'S', ID_EXPORT_SNAPSHOT },
        { FVIRTKEY | FCONTROL, 'L', ID_TOGGLE_LOG      },
        { FVIRTKEY | FCONTROL, 'A', ID_CONFIG_ALERTAS  },
        { FVIRTKEY | FCONTROL, 'O', ID_TOGGLE_OVERLAY  }
    };
    HACCEL hAccel;
    MSG msg;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    HANDLE hInstanceMutex = NULL;

    (void)hPrevInstance;

    /* Flag de linha de comandos: winmon.exe --overlay  (ou -overlay) */
    int flagOverlay = (lpCmdLine &&
        (strstr(lpCmdLine, "--overlay") != NULL ||
         strstr(lpCmdLine, "-overlay")  != NULL));

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

    hAccel = CreateAcceleratorTableA(accels, 4);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    /* Activar overlay se pedido via linha de comandos */
    if (flagOverlay)
        CriarOverlay();

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