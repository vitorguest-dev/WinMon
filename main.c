/*
 * WinMon — variante endurecida, ainda monolítica.
 *
 * Objetivos:
 *   - eliminar o padrão perigoso "offset += snprintf(...)";
 *   - reduzir privilégios pedidos a processos monitorizados;
 *   - carregar Msftedit.dll apenas a partir de System32;
 *   - tratar falhas de alocação/API e overflow de contadores;
 *   - corrigir ciclo de vida de alguns objetos GDI;
 *   - permitir personalização de fontes/cores da janela principal e overlay;
 *   - manter a arquitetura num único .c.
 *   - destacar zonas de alerta nos graficos CPU/RAM;
 *   - fazer o overlay piscar a borda em caso de alerta;
 *   - centralizar configuracoes numa aba "Definicoes" com secoes recolhiveis;
 *   - abrir historico CPU/RAM por PID com duplo clique no ListView.
 *
 * Recomendações de compilação (MSVC x64):
 *   /O2 /W4 /WX /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT
 *   Linker: /GUARD:CF /CETCOMPAT /DYNAMICBASE /NXCOMPAT
 */

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
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
#include <ctype.h>

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
#pragma comment(lib, "comdlg32.lib")

#define _WIN32_DCOM
#include <wbemidl.h>

static const CLSID LOCAL_CLSID_WbemLocator =
    {0x4590f811, 0x1d3a, 0x11d0, {0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};
static const IID LOCAL_IID_IWbemLocator =
    {0xdc12a687, 0x737f, 0x11cf, {0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24}};

#define CLSID_WbemLocator LOCAL_CLSID_WbemLocator
#define IID_IWbemLocator LOCAL_IID_IWbemLocator

#define TIMER_ID 1
#define BUFFER_SIZE 16384
#define MAX_PROCESSES 2048
#define MAX_ALERT_RANGES 32
#define ID_EXPORT_SNAPSHOT 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define MAX_CORES 64

#define LIMITE_RAM_PERCENT_DEFAULT 90.0
#define LIMITE_DISCO_PERCENT_DEFAULT 95.0
#define LIMITE_CPU_PERCENT_DEFAULT 90.0

#define LOG_INTERVALO_SEGUNDOS 10
#define ID_TOGGLE_LOG 1002
#define ID_CONFIG_ALERTAS 1003
#define ID_TOGGLE_OVERLAY 1004
#define ID_CONFIG_OVERLAY 1005
#define ID_CYCLE_INTERVAL 1006
#define ID_TRAY_EXIT 1007
#define ID_TRAY_RESTORE 1008
#define ID_CONFIG_ESTILO_MAIN 1009
#define ID_CONFIG_TRAY 1010
#define ID_CONFIG_SETTINGS 1011

#define OVERLAY_CLASS_NAME "WinMonOverlayClass"

/* Hotkey global: Ctrl+Shift+O para toggle do overlay */
#define HOTKEY_ID_OVERLAY  1
#define HOTKEY_MOD_OVERLAY (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT)
#define HOTKEY_VK_OVERLAY  'O'

#define OV_FONT_MIN 8
#define OV_FONT_MAX 32
#define OV_SNAP_DIST 40
#define OV_MAX_PERFIS 16
#define OV_NOME_MAX 32

#define TAB_RESUMO 2001
#define TAB_CPU 2002
#define TAB_MEMORIA 2003
#define TAB_DISCO 2004
#define TAB_REDE 2005
#define TAB_PROCESSOS 2006
#define TAB_DEFINICOES 2007
#define NAV_LARGURA 148

#define MAX_TEMP_ZONAS 16
#define BALLOON_COOLDOWN_SEGUNDOS 30
#define HISTORICO_PONTOS 120
#define MAX_HISTORICO 2048

typedef struct
{
    DWORD pid;
    SIZE_T memUsageMB;
    double cpuPercent;
    char exeFile[MAX_PATH];
} ProcessoInfo;

typedef struct
{
    DWORD pid;
    ULONGLONG lastKernelTime;
    ULONGLONG lastUserTime;
    int valido;
} ProcessoCpuHistorico;

#define MAX_PROCESS_SERIES 2048

typedef struct
{
    DWORD pid;
    char exeFile[MAX_PATH];
    double cpu[HISTORICO_PONTOS];
    double ram[HISTORICO_PONTOS];
    ULONGLONG ultimoVisto;
    int emUso;
} ProcessoSerieHistorico;

typedef struct
{
    long inicio;
    long fim;
} IntervaloAlerta;

typedef struct
{
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

typedef struct
{
    char nome[OV_NOME_MAX];
    int fontePt;
    char fonteNome[64];
    BYTE opacidade;
    int mostrarTemp;
    int mostrarDisco;
    int mostrarNet;
    int clickThrough;

    COLORREF corBg;
    COLORREF corBorda;
    COLORREF corTextoLabel;
    COLORREF corTextoValor;
    COLORREF corBarraCpu;
    COLORREF corBarraRam;
    COLORREF corBarraTemp;
    COLORREF corBarraDisco;
    COLORREF corBarraNet;
    COLORREF corSeparador;
    int mostrarBarras;
    int mostrarSeparadores;
    int espessuraBorda;
} OvPerfil;

typedef struct
{
    LOGFONTA fonteEdit;
    COLORREF corFundoEdit;
    COLORREF corTextoEdit;
    COLORREF corFundoGrafico;
    COLORREF corGrelhaGrafico;
    COLORREF corEixoGrafico;
    COLORREF corTextoGrafico;
    COLORREF corGraficoCpu;
    COLORREF corGraficoRam;
    COLORREF corGraficoDiscoRead;
    COLORREF corGraficoDiscoWrite;
    COLORREF corGraficoNetDown;
    COLORREF corGraficoNetUp;
    COLORREF corGraficoProcesso;
    COLORREF corGraficoCore;
    int espessuraLinhas;
    int mostrarGrelha;
    int mostrarEixos;
} MainUiConfig;

typedef struct
{
    int mostrarCpu;
    int mostrarRam;
    int mostrarTemperatura;
    int mostrarRede;
    int mostrarDisco;
    int mostrarUptime;
    int restaurarComClique;
    int restaurarComDuploClique;
} TrayConfig;

static MainUiConfig g_mainConfig = {
    .fonteEdit = {
        .lfHeight = -15,
        .lfWeight = FW_NORMAL,
        .lfCharSet = ANSI_CHARSET,
        .lfOutPrecision = OUT_DEFAULT_PRECIS,
        .lfClipPrecision = CLIP_DEFAULT_PRECIS,
        .lfQuality = DEFAULT_QUALITY,
        .lfPitchAndFamily = FIXED_PITCH | FF_MODERN,
        .lfFaceName = "Consolas"},
    .corFundoEdit = RGB(255, 255, 255),
    .corTextoEdit = RGB(0, 0, 0),
    .corFundoGrafico = RGB(250, 251, 252),
    .corGrelhaGrafico = RGB(225, 228, 232),
    .corEixoGrafico = RGB(180, 185, 190),
    .corTextoGrafico = RGB(40, 44, 48),
    .corGraficoCpu = RGB(35, 115, 210),
    .corGraficoRam = RGB(145, 75, 185),
    .corGraficoDiscoRead = RGB(40, 120, 200),
    .corGraficoDiscoWrite = RGB(215, 120, 45),
    .corGraficoNetDown = RGB(35, 145, 95),
    .corGraficoNetUp = RGB(205, 80, 80),
    .corGraficoProcesso = RGB(205, 70, 70),
    .corGraficoCore = RGB(85, 145, 95),
    .espessuraLinhas = 2,
    .mostrarGrelha = 1,
    .mostrarEixos = 1};

static TrayConfig g_trayConfig = {
    1, 1, 0, 0, 0, 0, 1, 0};

static double limiteCpuPercent = LIMITE_CPU_PERCENT_DEFAULT;
static double limiteRamPercent = LIMITE_RAM_PERCENT_DEFAULT;
static double limiteDiscoPercent = LIMITE_DISCO_PERCENT_DEFAULT;

static FILE *hLogCSV = NULL;
static int logAtivo = 0;
static int logTickContador = 0;
static char logNomeFicheiro[MAX_PATH] = {0};

static int numZonasTemp = 0;
static double tempAtual[MAX_TEMP_ZONAS];
static char tempNome[MAX_TEMP_ZONAS][64];

static ULONGLONG ultimoBalloonTick = 0;

static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;

HWND hMainWindow = NULL;
HWND hEdit = NULL;
HWND hTab = NULL;
HWND hDashboard = NULL;
HWND hNav[7] = {NULL};
HWND hBtnOverlay = NULL;
HWND hBtnInterval = NULL;
HWND hBtnEstiloMain = NULL;
HWND hGraphCPU = NULL;
HWND hGraphRAM = NULL;
HWND hGraphDisk = NULL;
HWND hGraphNet = NULL;
HWND hGraphProcesses = NULL;

/* --- Painel de processos full (aba Processos) --- */
HWND hPainelProcessos = NULL;  /* container panel */
HWND hListViewProc = NULL;     /* ListView com todos os processos */
HWND hEditPesquisaProc = NULL; /* caixa de pesquisa */
HWND hLabelProcCount = NULL;   /* "N processos" */

#define SETTINGS_SECTIONS 5
#define IDC_EDIT_PESQUISA_PROC 6001
#define IDC_LISTVIEW_PROC 6002
#define IDC_LABEL_PROC_COUNT 6003
#define IDC_SETTINGS_TITLE 8000
#define IDC_SETTINGS_HDR_ALERT 8001
#define IDC_SETTINGS_HDR_APAR 8002
#define IDC_SETTINGS_HDR_OVER 8003
#define IDC_SETTINGS_HDR_TRAY 8004
#define IDC_SETTINGS_HDR_DADOS 8005
#define IDC_SETTINGS_ALERT 8011
#define IDC_SETTINGS_APAR 8012
#define IDC_SETTINGS_OV_CFG 8013
#define IDC_SETTINGS_OV_TOGGLE 8014
#define IDC_SETTINGS_TRAY 8015
#define IDC_SETTINGS_INTERVAL 8016
#define IDC_SETTINGS_LOG 8017
#define IDC_SETTINGS_SNAPSHOT 8018

static HWND hSettingsTitle = NULL;
static HWND hSettingsHeaders[SETTINGS_SECTIONS] = {NULL};
static HWND hSettingsActions[SETTINGS_SECTIONS][3] = {{NULL}};
static int g_settingsOpen[SETTINGS_SECTIONS] = {1, 1, 1, 1, 1};

/* estado de ordenação do ListView */
static int g_lvSortCol = 1;  /* coluna activa (0=Nome,1=PID,2=CPU,3=RAM) */
static int g_lvSortDesc = 1; /* 1=descendente, 0=ascendente */

/* snapshot filtrado para o ListView */
static ProcessoInfo g_lvProcessos[MAX_PROCESSES];
static int g_lvTotal = 0;

/* total bruto antes de filtrar (para o label) */
static int g_totalProcessosBruto = 0;
static int totalListaProcessos = 0;

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

static HWND hOverlay = NULL;
static int overlayAtivo = 0;

static int ovPerfilActivo = 0;
static int ovNumPerfis = 0;
static OvPerfil ovPerfis[OV_MAX_PERFIS];

#define OV_FONTE_PT (ovPerfis[ovPerfilActivo].fontePt)
#define OV_FONTE_NOME (ovPerfis[ovPerfilActivo].fonteNome)
#define OV_OPACIDADE (ovPerfis[ovPerfilActivo].opacidade)
#define OV_TEMP (ovPerfis[ovPerfilActivo].mostrarTemp)
#define OV_DISCO (ovPerfis[ovPerfilActivo].mostrarDisco)
#define OV_NET (ovPerfis[ovPerfilActivo].mostrarNet)
#define OV_CLICKTHRU (ovPerfis[ovPerfilActivo].clickThrough)

static int ovColW = 120;
static int ovRowH = 0;
static int ovTotalW = 0;
static int ovTotalH = 0;

static ProcessoInfo listaProcessos[MAX_PROCESSES];
static HistoricoMonitor historico;

static int abaAtual = 0;

static const UINT intervalosAtualizacao[] = {500, 1000, 2000, 5000};
static UINT intervaloAtualizacaoMs = 1000;

static double processoCpuAtual = 0.0;
static double processoRamAtual = 0.0;
static double coresCpuAtuais[MAX_CORES];
static double processoCpuTop[HISTORICO_PONTOS];
static double processoRamTop[HISTORICO_PONTOS];

static ProcessoSerieHistorico g_processSeries[MAX_PROCESS_SERIES];
static int g_processSeriesCount = 0;
static DWORD g_processDetailPid = 0;
static char g_processDetailName[MAX_PATH] = {0};
static HWND hProcessDetail = NULL;

#define OVERLAY_ALERT_TIMER 9001
#define OVERLAY_ALERT_MS 5000
static ULONGLONG overlayAlertaAteTick = 0;
static int overlayAlertaVisivel = 0;
static int alertaGlobalAnterior = 0;

/* ------------------------------------------------------------------------- */
/* Seleção de Cores e Fontes                                                 */
/* ------------------------------------------------------------------------- */

static BOOL SelecionarCor(HWND hwndPai, COLORREF *corAtual)
{
    CHOOSECOLORA cc;
    static COLORREF custColors[16] = {0};

    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwndPai;
    cc.lpCustColors = custColors;
    cc.rgbResult = *corAtual;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorA(&cc))
    {
        *corAtual = cc.rgbResult;
        return TRUE;
    }
    return FALSE;
}

static BOOL SelecionarFonte(HWND hwndPai, LOGFONTA *lf)
{
    CHOOSEFONTA cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = hwndPai;
    cf.lpLogFont = lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;

    return ChooseFontA(&cf);
}

/* ------------------------------------------------------------------------- */
/* Utilitários                                                               */
/* ------------------------------------------------------------------------- */

static int AppendFormat(char *buffer, size_t capacity, size_t *offset,
                        const char *format, ...)
{
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

    if (written < 0)
    {
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

static void InicializarBuffer(char *buffer, size_t capacity, size_t *offset)
{
    if (buffer && capacity > 0)
    {
        buffer[0] = '\0';
        if (offset)
            *offset = 0;
    }
}

void FormatarBytes(double bytes, char *buffer, size_t size)
{
    if (bytes >= 1073741824.0)
        snprintf(buffer, size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576.0)
        snprintf(buffer, size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024.0)
        snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    else
        snprintf(buffer, size, "%.0f B", bytes);
}

static ULONGLONG FileTimeToU64(FILETIME ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

static ProcessoCpuHistorico *EncontrarHistorico(DWORD pid)
{
    int i;
    for (i = 0; i < totalHistorico; i++)
    {
        if (historicoCpu[i].pid == pid)
            return &historicoCpu[i];
    }
    return NULL;
}

static void RegistarAlerta(long inicio, long fim)
{
    if (totalIntervalosAlerta < MAX_ALERT_RANGES)
    {
        intervalosAlerta[totalIntervalosAlerta].inicio = inicio;
        intervalosAlerta[totalIntervalosAlerta].fim = fim;
        totalIntervalosAlerta++;
    }
    alertaGlobalAtivo = 1;
}

int CompararProcessosPorRAM(const void *a, const void *b)
{
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->memUsageMB < p2->memUsageMB)
        return 1;
    if (p1->memUsageMB > p2->memUsageMB)
        return -1;
    return 0;
}

int CompararProcessosPorCPU(const void *a, const void *b)
{
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->cpuPercent < p2->cpuPercent)
        return 1;
    if (p1->cpuPercent > p2->cpuPercent)
        return -1;
    return 0;
}

static int HistoricoIndex(int offset)
{
    int idx;
    if (historico.count == 0)
        return 0;
    idx = historico.pos - (historico.count - 1) + offset;
    while (idx < 0)
        idx += HISTORICO_PONTOS;
    while (idx >= HISTORICO_PONTOS)
        idx -= HISTORICO_PONTOS;
    return idx;
}

static void AdicionarHistorico(double cpu, double ram,
                               double diskRead, double diskWrite,
                               double netDown, double netUp)
{
    int i;

    historico.pos = (historico.pos + 1) % HISTORICO_PONTOS;
    historico.cpu[historico.pos] = cpu;
    historico.ram[historico.pos] = ram;
    historico.diskRead[historico.pos] = diskRead;
    historico.diskWrite[historico.pos] = diskWrite;
    historico.netDown[historico.pos] = netDown;
    historico.netUp[historico.pos] = netUp;

    for (i = 0; i < numNucleosMonitorizados; i++)
    {
        historico.core[i][historico.pos] = 0.0;
    }

    processoCpuTop[historico.pos] = 0.0;
    processoRamTop[historico.pos] = 0.0;

    if (historico.count < HISTORICO_PONTOS)
        historico.count++;
}

/* ------------------------------------------------------------------------- */
/* Recolha de Dados                                                          */
/* ------------------------------------------------------------------------- */

void MonitorarSistema(char *buffer, size_t size, size_t *offset)
{
    ULONGLONG uptimeMs = GetTickCount64();
    int dias = (int)(uptimeMs / (1000ULL * 60 * 60 * 24));
    int horas = (int)((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int minutos = (int)((uptimeMs / (1000ULL * 60)) % 60);

    AppendFormat(buffer, size, offset, "=== [ SISTEMA & UPTIME ] ===\r\n"
                                       "Tempo de Atividade: %d dias, %d horas, %d minutos\r\n",
                 dias, horas, minutos);

    SYSTEM_POWER_STATUS statusEnergia;
    if (GetSystemPowerStatus(&statusEnergia))
    {
        if (statusEnergia.BatteryLifePercent != 255)
        {
            const char *fonte = (statusEnergia.ACLineStatus == 1)
                                    ? "Carregador Conectado"
                                    : "Em Bateria";
            AppendFormat(buffer, size, offset, "Energia: %d%% (%s)\r\n",
                         statusEnergia.BatteryLifePercent, fonte);
        }
        else
        {
            AppendFormat(buffer, size, offset, "Energia: Desktop (Sem Bateria)\r\n");
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarCPU(char *buffer, size_t size, size_t *offset)
{
    PDH_FMT_COUNTERVALUE counterVal;
    double cpuAtual = 0.0;
    int i;

    PdhCollectQueryData(hQuery);

    if (hCounterCPU &&
        PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS &&
        counterVal.CStatus == ERROR_SUCCESS)
    {
        cpuAtual = counterVal.doubleValue;
    }

    ultimoCpuPercent = cpuAtual;

    {
        long inicioLinha = (long)*offset;
        AppendFormat(buffer, size, offset, "=== [ PROCESSADOR ] ===\r\n"
                                           "Uso Atual da CPU (Total): %.1f%%\r\n",
                     cpuAtual);
        if (cpuAtual > limiteCpuPercent)
            RegistarAlerta(inicioLinha, (long)*offset);
    }

    if (numNucleosMonitorizados > 0)
    {
        AppendFormat(buffer, size, offset, "Por Nucleo: ");

        for (i = 0; i < numNucleosMonitorizados; i++)
        {
            PDH_FMT_COUNTERVALUE coreVal;
            double corePercent = 0.0;

            if (hCounterCoresCPU[i] &&
                PdhGetFormattedCounterValue(hCounterCoresCPU[i], PDH_FMT_DOUBLE,
                                            NULL, &coreVal) == ERROR_SUCCESS &&
                coreVal.CStatus == ERROR_SUCCESS)
            {
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

void MonitorarDiscoIO(char *buffer, size_t size, size_t *offset)
{
    PDH_FMT_COUNTERVALUE readVal, writeVal;
    double bytesLeitura = 0.0, bytesEscrita = 0.0;

    if (hCounterDiskRead &&
        PdhGetFormattedCounterValue(hCounterDiskRead, PDH_FMT_DOUBLE, NULL, &readVal) == ERROR_SUCCESS &&
        readVal.CStatus == ERROR_SUCCESS)
    {
        bytesLeitura = readVal.doubleValue;
    }

    if (hCounterDiskWrite &&
        PdhGetFormattedCounterValue(hCounterDiskWrite, PDH_FMT_DOUBLE, NULL, &writeVal) == ERROR_SUCCESS &&
        writeVal.CStatus == ERROR_SUCCESS)
    {
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

void MonitorarRAM(char *buffer, size_t size, size_t *offset)
{
    MEMORYSTATUSEX memInfo;
    ZeroMemory(&memInfo, sizeof(memInfo));
    memInfo.dwLength = sizeof(memInfo);

    if (GlobalMemoryStatusEx(&memInfo))
    {
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

void MonitorarDiscos(char *buffer, size_t size, size_t *offset)
{
    DWORD drives = GetLogicalDrives();
    char driveLetter[] = "A:\\";
    int i;

    AppendFormat(buffer, size, offset, "=== [ DISCOS DE ARMAZENAMENTO ] ===\r\n");

    for (i = 0; i < 26; i++)
    {
        if (drives & (1UL << i))
        {
            driveLetter[0] = (char)('A' + i);

            if (GetDriveTypeA(driveLetter) == DRIVE_FIXED)
            {
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;

                if (GetDiskFreeSpaceExA(driveLetter, &freeBytesAvailable,
                                        &totalBytes, &totalFreeBytes))
                {
                    double totalGB = (double)totalBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double livreGB = (double)totalFreeBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double usadaGB = totalGB - livreGB;
                    double percentUsado = totalGB > 0.0
                                              ? (usadaGB / totalGB) * 100.0
                                              : 0.0;

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

void MonitorarRede(char *buffer, size_t size, size_t *offset)
{
    ULONG outBufLen = 0;
    PMIB_IFTABLE pIfTable;
    DWORDLONG currentIn = 0, currentOut = 0;
    DWORD i;

    if (GetIfTable(NULL, &outBufLen, FALSE) != ERROR_INSUFFICIENT_BUFFER ||
        outBufLen == 0 ||
        outBufLen > (1024UL * 1024UL * 8UL))
    {
        return;
    }

    pIfTable = (PMIB_IFTABLE)malloc(outBufLen);
    if (!pIfTable)
        return;

    if (GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR)
    {
        for (i = 0; i < pIfTable->dwNumEntries; i++)
        {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }

        if (!firstNetworkRead)
        {
            double down = (double)(currentIn >= lastIn ? currentIn - lastIn : 0);
            double up = (double)(currentOut >= lastOut ? currentOut - lastOut : 0);

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
        }
        else
        {
            ultimoNetDown = 0.0;
            ultimoNetUp = 0.0;
        }

        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
    }

    free(pIfTable);
}

void MonitorarProcessos(char *buffer, size_t size, size_t *offset)
{
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
                          ? (currentSystemTime - lastSystemTime)
                          : 0;

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE)
    {
        AppendFormat(buffer, size, offset, "Nao foi possivel enumerar processos.\r\n\r\n");
        return;
    }

    ZeroMemory(&pe32, sizeof(pe32));
    pe32.dwSize = sizeof(pe32);

    if (Process32First(hProcessSnap, &pe32))
    {
        do
        {
            if (pe32.th32ProcessID != 0 && totalProcessos < MAX_PROCESSES)
            {
                HANDLE hProcess = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, pe32.th32ProcessID);

                SIZE_T memUsageMB = 0;
                double cpuPercent = 0.0;

                if (hProcess)
                {
                    PROCESS_MEMORY_COUNTERS pmc;

                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
                        memUsageMB = pmc.WorkingSetSize / (1024 * 1024);

                    {
                        FILETIME ftCreate, ftExit, ftKernel, ftUser;

                        if (GetProcessTimes(hProcess, &ftCreate, &ftExit,
                                            &ftKernel, &ftUser))
                        {
                            ULONGLONG kernelU64 = FileTimeToU64(ftKernel);
                            ULONGLONG userU64 = FileTimeToU64(ftUser);
                            ULONGLONG totalProcTime = kernelU64 + userU64;
                            ProcessoCpuHistorico *hist =
                                EncontrarHistorico(pe32.th32ProcessID);

                            if (hist && deltaSystemTime > 0)
                            {
                                ULONGLONG anterior =
                                    hist->lastKernelTime + hist->lastUserTime;
                                ULONGLONG deltaProc =
                                    totalProcTime >= anterior
                                        ? totalProcTime - anterior
                                        : 0;

                                cpuPercent =
                                    ((double)deltaProc /
                                     (double)deltaSystemTime) *
                                    100.0 * numProcessadores;

                                if (cpuPercent < 0.0)
                                    cpuPercent = 0.0;
                                if (cpuPercent > 100.0 * numProcessadores)
                                    cpuPercent = 100.0 * numProcessadores;
                            }

                            if (totalNovoHistorico < MAX_HISTORICO)
                            {
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
    totalListaProcessos = totalProcessos;

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

        for (i = 0; i < limite; i++)
        {
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

        for (i = 0; i < limiteRAM; i++)
        {
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
static void MostrarDialogoEstiloPrincipal(HWND hwndPai);
static void MostrarDialogoConfigTray(HWND hwndPai);
static void CarregarConfigVisual(void);
static void GravarConfigVisual(void);
static void CarregarConfigTray(void);
static void GravarConfigTray(void);
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
static void AplicarClickThrough(void);
static void SnapOverlayAoCanto(void);
static void InicializarPerfisOverlay(void);
static void GravarPerfisOverlay(void);
static void CarregarPerfisOverlay(void);
static void AtivarPerfil(int idx);
static void MostrarDialogoConfigOverlay(HWND hwndPai);
static void CriarPainelDefinicoes(HWND hwndPai);
static void AtualizarDefinicoesVisibilidade(void);
static void RedimensionarDefinicoes(const RECT *content);
static void AbrirDetalheProcesso(DWORD pid, const char *nome);
static LRESULT CALLBACK ProcessDetailProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void AtualizarHistoricoProcessos(void);
static ProcessoSerieHistorico *EncontrarSerieProcesso(DWORD pid, int criar);
static void AtivarAlertaOverlay(void);
static void AplicarEstiloJanelaPrincipal(void);
static void CriarPainelProcessos(HWND hwndPai);
static void AtualizarListViewProcessos(void);
static void RedimensionarPainelProcessos(HWND hwndPai);
static void FiltrarListaProcessos(void);
static LRESULT CALLBACK DashboardProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam);
static void DesenharBotaoNavegacao(const DRAWITEMSTRUCT *dis);

/* ------------------------------------------------------------------------- */
/* Graficos GDI                                                              */
/* ------------------------------------------------------------------------- */

typedef enum
{
    GRAPH_CPU,
    GRAPH_RAM,
    GRAPH_DISK,
    GRAPH_NET,
    GRAPH_PROCESS
} GraphType;

static void DrawGraphGrid(HDC hdc, RECT rc, double maxY)
{
    HPEN penGrid = CreatePen(PS_SOLID, 1, g_mainConfig.corGrelhaGrafico);
    HPEN penAxis = CreatePen(PS_SOLID, 1, g_mainConfig.corEixoGrafico);
    HPEN oldPen;
    HFONT oldFont;
    RECT labelRect;
    int i;

    oldPen = (HPEN)SelectObject(hdc, penGrid);

    if (g_mainConfig.mostrarGrelha)
        for (i = 0; i <= 4; i++)
        {
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

                oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, RGB(105, 110, 116));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, label, -1, &labelRect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }
        }

    if (g_mainConfig.mostrarEixos)
    {
        SelectObject(hdc, penAxis);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.left, rc.bottom);
        LineTo(hdc, rc.right, rc.bottom);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);
    DeleteObject(penAxis);
}

static void DrawSeries(HDC hdc, RECT rc, const double *data,
                       int count, double maxY, COLORREF color,
                       int thickness)
{
    HPEN pen;
    HPEN oldPen;
    int i;
    BOOL havePoint = FALSE;

    if (count <= 0 || maxY <= 0.0)
        return;

    pen = CreatePen(PS_SOLID, thickness, color);
    if (!pen)
        return;

    oldPen = (HPEN)SelectObject(hdc, pen);

    for (i = 0; i < count; i++)
    {
        int idx = HistoricoIndex(i);
        double value = data[idx];

        if (isnan(value))
        {
            havePoint = FALSE;
            continue;
        }

        if (value < 0.0)
            value = 0.0;
        if (value > maxY)
            value = maxY;

        {
            int x = rc.left +
                    (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));

            int y = rc.bottom -
                    (int)((value / maxY) * (rc.bottom - rc.top));

            if (!havePoint)
                MoveToEx(hdc, x, y, NULL);
            else
                LineTo(hdc, x, y);
            havePoint = TRUE;
        }
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawGraphLegend(HDC hdc, RECT *rc, const char *title,
                            const char **names, COLORREF *colors, int n,
                            const char *currentText)
{
    HFONT oldFont;
    RECT titleRect;
    int x;
    int i;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_mainConfig.corTextoGrafico);

    oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));

    titleRect = *rc;
    titleRect.left += 6;
    titleRect.top += 4;
    titleRect.bottom = titleRect.top + 24;
    DrawTextA(hdc, title, -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    x = rc->left + 10;

    for (i = 0; i < n; i++)
    {
        HBRUSH brush = CreateSolidBrush(colors[i]);
        RECT box = {x, rc->top + 34, x + 10, rc->top + 44};
        RECT text = {x + 15, rc->top + 29, x + 130, rc->top + 49};

        FillRect(hdc, &box, brush);
        DeleteObject(brush);

        DrawTextA(hdc, names[i], -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        x += 125;
    }

    if (currentText)
    {
        RECT current = *rc;
        current.left = rc->right - 260;
        current.top += 5;
        current.bottom = current.top + 22;
        DrawTextA(hdc, currentText, -1, &current,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

static void DrawTimeLabels(HDC hdc, RECT rc, int count)
{
    char text[64];
    int i;

    if (count <= 0)
        return;

    for (i = 0; i < 5; i++)
    {
        int x = rc.left + ((rc.right - rc.left) * i) / 4;
        int secondsAgo = (count - 1) * (4 - i) / 4;

        if (i == 4)
            strcpy_s(text, sizeof(text), "agora");
        else
            snprintf(text, sizeof(text), "-%ds", secondsAgo);

        {
            RECT tr = {x - 35, rc.bottom + 6, x + 35, rc.bottom + 24};
            SetTextColor(hdc, RGB(115, 120, 126));
            DrawTextA(hdc, text, -1, &tr, DT_CENTER | DT_SINGLELINE);
        }
    }
}

static void DrawDashboardCard(HDC hdc, RECT rc, const char *label,
                              const char *value, COLORREF accent)
{
    HBRUSH panel = CreateSolidBrush(RGB(255, 255, 255));
    HBRUSH marker = CreateSolidBrush(accent);
    RECT markerRect = {rc.left, rc.top, rc.left + 5, rc.bottom};
    RECT labelRect = {rc.left + 16, rc.top + 12, rc.right - 10, rc.top + 31};
    RECT valueRect = {rc.left + 16, rc.top + 31, rc.right - 10, rc.bottom - 10};
    HFONT oldFont;
    HFONT valueFont;

    FillRect(hdc, &rc, panel);
    FillRect(hdc, &markerRect, marker);
    DeleteObject(panel);
    DeleteObject(marker);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(102, 108, 116));
    oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    DrawTextA(hdc, label, -1, &labelRect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    valueFont = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (valueFont)
        SelectObject(hdc, valueFont);
    SetTextColor(hdc, RGB(32, 38, 45));
    DrawTextA(hdc, value, -1, &valueRect,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    SelectObject(hdc, oldFont);
    if (valueFont)
        DeleteObject(valueFont);
}

static LRESULT CALLBACK DashboardProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC paintDc = BeginPaint(hwnd, &ps);
        RECT client;
        HDC bufferDc;
        HBITMAP bufferBitmap;
        HBITMAP oldBitmap;
        HBRUSH background;
        RECT card;
        char value[64];
        int gap = 10;
        int cardWidth;
        int i;

        GetClientRect(hwnd, &client);
        bufferDc = CreateCompatibleDC(paintDc);
        bufferBitmap = CreateCompatibleBitmap(paintDc,
                                              client.right, client.bottom);

        if (!bufferDc || !bufferBitmap)
        {
            if (bufferDc)
                DeleteDC(bufferDc);
            if (bufferBitmap)
                DeleteObject(bufferBitmap);
            EndPaint(hwnd, &ps);
            return 0;
        }

        oldBitmap = (HBITMAP)SelectObject(bufferDc, bufferBitmap);
        background = CreateSolidBrush(RGB(241, 244, 247));
        FillRect(bufferDc, &client, background);
        DeleteObject(background);

        SetBkMode(bufferDc, TRANSPARENT);
        SetTextColor(bufferDc, RGB(33, 39, 46));
        {
            RECT title = {16, 10, client.right - 16, 34};
            DrawTextA(bufferDc, "Visao geral do sistema", -1, &title,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        cardWidth = (client.right - 32 - gap * 3) / 4;
        if (cardWidth < 100)
            cardWidth = 100;

        for (i = 0; i < 4; i++)
        {
            card.left = 16 + i * (cardWidth + gap);
            card.top = 42;
            card.right = card.left + cardWidth;
            card.bottom = 112;

            if (i == 0)
            {
                snprintf(value, sizeof(value), "%.1f%%", ultimoCpuPercent);
                DrawDashboardCard(bufferDc, card, "CPU", value,
                                  g_mainConfig.corGraficoCpu);
            }
            else if (i == 1)
            {
                snprintf(value, sizeof(value), "%.0f%%", ultimoRamPercent);
                DrawDashboardCard(bufferDc, card, "Memoria", value,
                                  g_mainConfig.corGraficoRam);
            }
            else if (i == 2)
            {
                snprintf(value, sizeof(value), "%.1f MB/s",
                         (ultimoDiskRead + ultimoDiskWrite) / 1048576.0);
                DrawDashboardCard(bufferDc, card, "Disco I/O", value,
                                  g_mainConfig.corGraficoDiscoRead);
            }
            else
            {
                snprintf(value, sizeof(value), "%.1f MB/s",
                         (ultimoNetDown + ultimoNetUp) / 1048576.0);
                DrawDashboardCard(bufferDc, card, "Rede", value,
                                  g_mainConfig.corGraficoNetDown);
            }
        }

        {
            RECT status = {16, 124, client.right - 16, 158};
            char statusText[160];
            snprintf(statusText, sizeof(statusText),
                     "%s   |   Processo lider: %.1f%% CPU   |   Historico: %d pontos",
                     alertaGlobalAtivo ? "Estado: ALERTA" : "Estado: normal",
                     processoCpuAtual, historico.count);
            SetTextColor(bufferDc, alertaGlobalAtivo
                                       ? RGB(190, 55, 45)
                                       : RGB(82, 91, 101));
            DrawTextA(bufferDc, statusText, -1, &status,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }

        BitBlt(paintDc, 0, 0, client.right, client.bottom,
               bufferDc, 0, 0, SRCCOPY);
        SelectObject(bufferDc, oldBitmap);
        DeleteObject(bufferBitmap);
        DeleteDC(bufferDc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void DrawAlertRegions(HDC hdc, RECT rc, const double *data,
                             int count, double maxY, double threshold)
{
    HBRUSH brush;
    int i;
    int yLimite;

    if (count <= 0 || maxY <= 0.0 || threshold <= 0.0 || threshold >= maxY)
        return;

    brush = CreateSolidBrush(RGB(255, 232, 232));
    if (!brush)
        return;

    yLimite = rc.bottom - (int)((threshold / maxY) * (rc.bottom - rc.top));
    if (yLimite < rc.top)
        yLimite = rc.top;
    if (yLimite > rc.bottom)
        yLimite = rc.bottom;

    for (i = 0; i < count; i++)
    {
        int idx = HistoricoIndex(i);
        double value = data[idx];
        int x1, x2;
        RECT zone;

        if (isnan(value) || value <= threshold)
            continue;

        x1 = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));
        x2 = (i == count - 1)
                 ? rc.right
                 : rc.left + (int)(((double)(i + 1) / (double)(count - 1)) *
                                   (rc.right - rc.left));

        if (x2 <= x1)
            x2 = x1 + 1;

        zone.left = x1;
        zone.right = x2;
        zone.top = rc.top;
        zone.bottom = yLimite;
        FillRect(hdc, &zone, brush);
    }

    DeleteObject(brush);
}

static void PaintGraph(HWND hwnd, HDC hdc, GraphType type)
{
    RECT client;
    RECT graph;
    HBRUSH bg = CreateSolidBrush(g_mainConfig.corFundoGrafico);
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

    switch (type)
    {
    case GRAPH_CPU:
    {
        names[0] = "CPU total";
        colors[0] = g_mainConfig.corGraficoCpu;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Atual: %.1f%%", ultimoCpuPercent);

        DrawGraphLegend(hdc, &client, "CPU nos ultimos 120 segundos",
                        names, colors, n, currentText);
        DrawAlertRegions(hdc, graph, historico.cpu, historico.count,
                         100.0, limiteCpuPercent);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeries(hdc, graph, historico.cpu, historico.count,
                   100.0, colors[0], g_mainConfig.espessuraLinhas);

        if (numNucleosMonitorizados > 0 && client.bottom > 430)
        {
            int smallTop = graph.bottom + 42;
            int smallBottom = client.bottom - 12;
            int smallHeight = smallBottom - smallTop;

            if (smallHeight > 45)
            {
                int cols = 4;
                int rows = (numNucleosMonitorizados + cols - 1) / cols;
                int cw = (client.right - 72) / cols;
                int rh = smallHeight / rows;

                for (i = 0; i < numNucleosMonitorizados; i++)
                {
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
                               g_mainConfig.corGraficoCore, 1);

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

    case GRAPH_RAM:
    {
        names[0] = "RAM usada";
        colors[0] = g_mainConfig.corGraficoRam;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Atual: %.0f%%", ultimoRamPercent);

        DrawGraphLegend(hdc, &client, "Memoria RAM em uso percentual",
                        names, colors, n, currentText);
        DrawAlertRegions(hdc, graph, historico.ram, historico.count,
                         100.0, limiteRamPercent);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeries(hdc, graph, historico.ram, historico.count,
                   100.0, colors[0], g_mainConfig.espessuraLinhas);
        DrawTimeLabels(hdc, graph, historico.count);
        break;
    }

    case GRAPH_DISK:
    {
        double maxVal = 1024.0 * 1024.0;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (historico.diskRead[i] > maxVal)
                maxVal = historico.diskRead[i];
            if (historico.diskWrite[i] > maxVal)
                maxVal = historico.diskWrite[i];
        }

        names[0] = "Leitura";
        names[1] = "Escrita";
        colors[0] = g_mainConfig.corGraficoDiscoRead;
        colors[1] = g_mainConfig.corGraficoDiscoWrite;
        n = 2;

        snprintf(currentText, sizeof(currentText),
                 "R %.2f MB/s   W %.2f MB/s",
                 ultimoDiskRead / 1048576.0,
                 ultimoDiskWrite / 1048576.0);

        DrawGraphLegend(hdc, &client, "I/O",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxVal);
        DrawSeries(hdc, graph, historico.diskRead, historico.count,
                   maxVal, colors[0], g_mainConfig.espessuraLinhas);
        DrawSeries(hdc, graph, historico.diskWrite, historico.count,
                   maxVal, colors[1], g_mainConfig.espessuraLinhas);
        DrawTimeLabels(hdc, graph, historico.count);
        break;
    }

    case GRAPH_NET:
    {
        double maxVal = 1024.0 * 1024.0;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (historico.netDown[i] > maxVal)
                maxVal = historico.netDown[i];
            if (historico.netUp[i] > maxVal)
                maxVal = historico.netUp[i];
        }

        names[0] = "Download";
        names[1] = "Upload";
        colors[0] = g_mainConfig.corGraficoNetDown;
        colors[1] = g_mainConfig.corGraficoNetUp;
        n = 2;

        snprintf(currentText, sizeof(currentText),
                 "D %.2f MB/s   U %.2f MB/s",
                 ultimoNetDown / 1048576.0,
                 ultimoNetUp / 1048576.0);

        DrawGraphLegend(hdc, &client, "Trafego de rede",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxVal);
        DrawSeries(hdc, graph, historico.netDown, historico.count,
                   maxVal, colors[0], g_mainConfig.espessuraLinhas);
        DrawSeries(hdc, graph, historico.netUp, historico.count,
                   maxVal, colors[1], g_mainConfig.espessuraLinhas);
        DrawTimeLabels(hdc, graph, historico.count);
        break;
    }

    case GRAPH_PROCESS:
    {
        double maxCpu = 100.0;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (processoCpuTop[i] > maxCpu)
                maxCpu = processoCpuTop[i];
        }

        names[0] = "Processo #1 CPU";
        colors[0] = g_mainConfig.corGraficoProcesso;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Pico atual: %.1f%% CPU",
                 processoCpuTop[historico.pos]);

        DrawGraphLegend(hdc, &client, "Processo com maior uso de CPU",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxCpu);
        DrawSeries(hdc, graph, processoCpuTop, historico.count,
                   maxCpu, colors[0], g_mainConfig.espessuraLinhas);
        DrawTimeLabels(hdc, graph, historico.count);

        if (client.bottom > 360)
        {
            RECT info = {
                70, graph.bottom + 34,
                client.right - 30, client.bottom - 8};
            char line[256];

            snprintf(line, sizeof(line),
                     "RAM do processo lider neste instante: %.0f MB",
                     processoRamTop[historico.pos]);

            SetTextColor(hdc, RGB(75, 80, 86));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, line, -1, &info,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        break;
    }
    }
}

LRESULT CALLBACK GraphProc(HWND hwnd, UINT msg,
                           WPARAM wParam, LPARAM lParam)
{
    GraphType type = (GraphType)(GetWindowLongPtr(hwnd, GWLP_USERDATA) - 1);

    switch (msg)
    {
    case WM_PAINT:
    {
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

static HWND CriarGrafico(HWND parent, GraphType type)
{
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

static ProcessoSerieHistorico *EncontrarSerieProcesso(DWORD pid, int criar)
{
    int i;
    ProcessoSerieHistorico *slot = NULL;

    for (i = 0; i < g_processSeriesCount; i++)
    {
        if (g_processSeries[i].emUso && g_processSeries[i].pid == pid)
            return &g_processSeries[i];
    }

    if (!criar)
        return NULL;

    if (g_processSeriesCount < MAX_PROCESS_SERIES)
    {
        slot = &g_processSeries[g_processSeriesCount++];
        ZeroMemory(slot, sizeof(*slot));
        slot->pid = pid;
        slot->emUso = 1;
    }
    else
    {
        int oldest = 0;
        ULONGLONG tickOldest = ~0ULL;
        for (i = 0; i < MAX_PROCESS_SERIES; i++)
        {
            if (g_processSeries[i].ultimoVisto < tickOldest)
            {
                tickOldest = g_processSeries[i].ultimoVisto;
                oldest = i;
            }
        }
        slot = &g_processSeries[oldest];
        ZeroMemory(slot, sizeof(*slot));
        slot->pid = pid;
        slot->emUso = 1;
    }

    for (i = 0; i < HISTORICO_PONTOS; i++)
    {
        slot->cpu[i] = NAN;
        slot->ram[i] = NAN;
    }
    return slot;
}

static void AtualizarHistoricoProcessos(void)
{
    int i;
    ULONGLONG agora = GetTickCount64();

    if (historico.count <= 0)
        return;

    for (i = 0; i < g_processSeriesCount; i++)
    {
        if (g_processSeries[i].emUso)
        {
            g_processSeries[i].cpu[historico.pos] = NAN;
            g_processSeries[i].ram[historico.pos] = NAN;
        }
    }

    for (i = 0; i < totalListaProcessos; i++)
    {
        ProcessoSerieHistorico *serie = EncontrarSerieProcesso(listaProcessos[i].pid, 1);
        if (serie)
        {
            serie->cpu[historico.pos] = listaProcessos[i].cpuPercent;
            serie->ram[historico.pos] = (double)listaProcessos[i].memUsageMB;
            strncpy_s(serie->exeFile, sizeof(serie->exeFile),
                      listaProcessos[i].exeFile, _TRUNCATE);
            serie->ultimoVisto = agora;
        }
    }
}

static void DesenharGraficoProcessoDetalhe(HDC hdc, RECT rc,
                                           const char *titulo,
                                           const double *data, int count,
                                           double maxY, COLORREF cor)
{
    const char *names[1] = {"Processo"};
    COLORREF colors[1] = {cor};
    DrawGraphLegend(hdc, &rc, titulo, names, colors, 1, NULL);
    DrawGraphGrid(hdc, rc, maxY);
    DrawSeries(hdc, rc, data, count, maxY, cor, 2);
    DrawTimeLabels(hdc, rc, count);
}

static LRESULT CALLBACK ProcessDetailProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hwnd, &ps);
        RECT client;
        HDC dc;
        HBITMAP bmp, oldBmp;
        HBRUSH bg;
        ProcessoSerieHistorico *serie;
        char titulo[256];
        int mid;
        double maxCpu = 100.0;
        double maxRam = 256.0;
        int i;

        GetClientRect(hwnd, &client);
        dc = CreateCompatibleDC(paint);
        bmp = CreateCompatibleBitmap(paint, client.right, client.bottom);
        if (!dc || !bmp)
        {
            if (dc)
                DeleteDC(dc);
            if (bmp)
                DeleteObject(bmp);
            EndPaint(hwnd, &ps);
            return 0;
        }

        oldBmp = (HBITMAP)SelectObject(dc, bmp);
        bg = CreateSolidBrush(g_mainConfig.corFundoGrafico);
        FillRect(dc, &client, bg);
        DeleteObject(bg);

        serie = EncontrarSerieProcesso(g_processDetailPid, 0);
        if (serie)
        {
            for (i = 0; i < HISTORICO_PONTOS; i++)
            {
                if (!isnan(serie->cpu[i]) && serie->cpu[i] > maxCpu)
                    maxCpu = serie->cpu[i];
                if (!isnan(serie->ram[i]) && serie->ram[i] > maxRam)
                    maxRam = serie->ram[i];
            }
        }

        snprintf(titulo, sizeof(titulo), "Detalhe do processo  %s  (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "desconhecido",
                 g_processDetailPid);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_mainConfig.corTextoGrafico);
        {
            RECT tr = {16, 8, client.right - 16, 34};
            HFONT oldFont = (HFONT)SelectObject(dc,
                                                GetStockObject(DEFAULT_GUI_FONT));
            DrawTextA(dc, titulo, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
        }

        mid = (client.bottom - 70) / 2 + 42;
        if (mid < 180)
            mid = 180;
        {
            RECT rCpu = {58, 45, client.right - 20, mid - 12};
            RECT rRam = {58, mid + 12, client.right - 20, client.bottom - 42};
            char cpuCur[64], ramCur[64];

            if (serie)
            {
                double c = serie->cpu[historico.pos];
                double r = serie->ram[historico.pos];
                snprintf(cpuCur, sizeof(cpuCur), "Atual: %s",
                         isnan(c) ? "N/A" : "-");
                if (!isnan(c))
                    snprintf(cpuCur, sizeof(cpuCur), "Atual: %.1f%%", c);
                snprintf(ramCur, sizeof(ramCur), "Atual: %s",
                         isnan(r) ? "N/A" : "-");
                if (!isnan(r))
                    snprintf(ramCur, sizeof(ramCur), "Atual: %.0f MB", r);
            }
            else
            {
                strcpy_s(cpuCur, sizeof(cpuCur), "Sem historico");
                strcpy_s(ramCur, sizeof(ramCur), "Sem historico");
            }

            if (serie)
            {
                DesenharGraficoProcessoDetalhe(dc, rCpu, "CPU do processo",
                                               serie->cpu, historico.count,
                                               maxCpu, g_mainConfig.corGraficoProcesso);
                DesenharGraficoProcessoDetalhe(dc, rRam, "RAM do processo",
                                               serie->ram, historico.count,
                                               maxRam, g_mainConfig.corGraficoRam);
            }
            else
            {
                SetTextColor(dc, RGB(100, 105, 110));
                DrawTextA(dc, "Ainda nao existe historico para este PID.", -1,
                          &rCpu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            {
                RECT inf = {16, client.bottom - 28, client.right - 16, client.bottom - 8};
                char info[160];
                snprintf(info, sizeof(info), "%s    |    %s    |    Historico: %d pontos",
                         cpuCur, ramCur, historico.count);
                SetTextColor(dc, g_mainConfig.corTextoGrafico);
                DrawTextA(dc, info, -1, &inf, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        BitBlt(paint, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 620;
        mmi->ptMinTrackSize.y = 420;
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        hProcessDetail = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void AbrirDetalheProcesso(DWORD pid, const char *nome)
{
    WNDCLASSA wc;

    g_processDetailPid = pid;
    strncpy_s(g_processDetailName, sizeof(g_processDetailName),
              nome ? nome : "", _TRUNCATE);

    if (hProcessDetail && IsWindow(hProcessDetail))
    {
        char titulo[256];
        snprintf(titulo, sizeof(titulo), "WinMon — %s (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "Processo", pid);
        SetWindowTextA(hProcessDetail, titulo);
        ShowWindow(hProcessDetail, SW_SHOWNOACTIVATE);
        SetForegroundWindow(hProcessDetail);
        InvalidateRect(hProcessDetail, NULL, FALSE);
        return;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ProcessDetailProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinMonProcessDetailClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    {
        char titulo[256];
        snprintf(titulo, sizeof(titulo), "WinMon — %s (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "Processo", pid);
        hProcessDetail = CreateWindowExA(
            WS_EX_TOOLWINDOW,
            wc.lpszClassName, titulo,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, 760, 540,
            hMainWindow, NULL, GetModuleHandle(NULL), NULL);
    }

    if (hProcessDetail)
    {
        ShowWindow(hProcessDetail, SW_SHOW);
        UpdateWindow(hProcessDetail);
    }
}

/* ------------------------------------------------------------------------- */
/* RichEdit e Interface                                                      */
/* ------------------------------------------------------------------------- */

void AplicarCoresDeAlerta()
{
    CHARFORMAT2A cf;
    int i;

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;

    SendMessage(hEdit, EM_SETSEL, 0, -1);
    cf.crTextColor = g_mainConfig.corTextoEdit;
    cf.dwEffects = 0;
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    cf.crTextColor = RGB(200, 0, 0);

    for (i = 0; i < totalIntervalosAlerta; i++)
    {
        SendMessage(hEdit, EM_SETSEL,
                    intervalosAlerta[i].inicio,
                    intervalosAlerta[i].fim);
        SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }

    SendMessage(hEdit, EM_SETSEL, 0, 0);
}

static void AplicarEstiloJanelaPrincipal(void)
{
    if (!hEdit)
        return;

    HFONT hNovaFonte = CreateFontIndirectA(&g_mainConfig.fonteEdit);
    if (hNovaFonte)
    {
        if (hFontMonitor)
            DeleteObject(hFontMonitor);
        hFontMonitor = hNovaFonte;
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMonitor, TRUE);
    }

    SendMessage(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)g_mainConfig.corFundoEdit);

    CHARFORMAT2A cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = g_mainConfig.corTextoEdit;

    SendMessage(hEdit, EM_SETSEL, 0, -1);
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(hEdit, EM_SETSEL, 0, 0);

    AplicarCoresDeAlerta();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);
    InvalidateRect(hDashboard, NULL, FALSE);
}

void AtualizarTituloJanela()
{
    if (alertaGlobalAtivo)
    {
        SetWindowTextA(hMainWindow,
                       "Monitor de Hardware & Sistema (Win32) v6  —  [!] ALERTA");
    }
    else
    {
        SetWindowTextA(hMainWindow,
                       "Monitor de Hardware & Sistema (Win32) v6");
    }
}

void AtualizarTooltipTray()
{
    /* Atualizar sempre o szTip (mesmo sem tray visível) para que fique
       pronto no momento em que o ícone for adicionado. */

    char linha[128];
    linha[0] = '\0';
    if (g_trayConfig.mostrarCpu)
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "CPU: %.1f%%", ultimoCpuPercent);
    if (g_trayConfig.mostrarRam)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "RAM: %.0f%%", ultimoRamPercent);
    }
    if (g_trayConfig.mostrarTemperatura && numZonasTemp > 0)
    {
        double t = 0;
        int i;
        for (i = 0; i < numZonasTemp; i++)
            t += tempAtual[i];
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "TEMP: %.0fC", t / numZonasTemp);
    }
    if (g_trayConfig.mostrarRede)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "NET: D %.1f/U %.1f MB/s", ultimoNetDown / 1048576.0, ultimoNetUp / 1048576.0);
    }
    if (g_trayConfig.mostrarDisco)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "DISCO: R %.1f/W %.1f MB/s", ultimoDiskRead / 1048576.0, ultimoDiskWrite / 1048576.0);
    }
    if (g_trayConfig.mostrarUptime)
    {
        ULONGLONG u = GetTickCount64() / 1000ULL;
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "UP: %lluh", (unsigned long long)(u / 3600ULL));
    }
    if (!linha[0])
        strcpy_s(linha, sizeof(linha), "Sem metricas selecionadas");
    snprintf(nid.szTip, sizeof(nid.szTip), "WinMon\r\n%s", linha);

    /* Só notifica o Shell se o ícone estiver visível */
    if (trayIconAtivo)
        Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void ExportarSnapshot()
{
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

    if (err == 0 && f != NULL)
    {
        fwrite(ultimoSnapshot, 1, strlen(ultimoSnapshot), f);
        fclose(f);

        snprintf(msg, sizeof(msg),
                 "Snapshot gravado como:\r\n%s", nomeFicheiro);

        MessageBoxA(hMainWindow, msg,
                    "Export concluido", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxA(hMainWindow,
                    "Nao foi possivel gravar o ficheiro de snapshot.",
                    "Erro", MB_OK | MB_ICONERROR);
    }
}

/* ------------------------------------------------------------------------- */
/* Abas                                                                      */
/* ------------------------------------------------------------------------- */

static void MostrarAba(int indice)
{
    int i;
    /* índices 1-4 mapeiam para gráficos; índice 5 é o painel de processos */
    HWND graficos[] = {
        hGraphCPU, hGraphRAM, hGraphDisk, hGraphNet};

    abaAtual = indice;

    for (i = 0; i < 7; i++)
    {
        if (hNav[i])
            SendMessage(hNav[i], BM_SETSTATE, i == indice, 0);
    }

    if (hEdit)
        ShowWindow(hEdit, indice == 0 ? SW_SHOW : SW_HIDE);

    if (hDashboard)
        ShowWindow(hDashboard, indice == 0 ? SW_SHOW : SW_HIDE);

    for (i = 0; i < 4; i++)
    {
        if (graficos[i])
            ShowWindow(graficos[i], indice == i + 1 ? SW_SHOW : SW_HIDE);
    }

    /* hGraphProcesses não tem aba própria — mantém-se sempre oculto */
    if (hGraphProcesses)
        ShowWindow(hGraphProcesses, SW_HIDE);

    /* painel de lista de processos: mostrar/esconder cada controlo */
    {
        int visProc = (indice == 5) ? SW_SHOW : SW_HIDE;
        if (hPainelProcessos)
            ShowWindow(hPainelProcessos, visProc);
        if (hEditPesquisaProc)
            ShowWindow(hEditPesquisaProc, visProc);
        if (hLabelProcCount)
            ShowWindow(hLabelProcCount, visProc);
        if (hListViewProc)
            ShowWindow(hListViewProc, visProc);
    }

    AtualizarDefinicoesVisibilidade();
    InvalidateRect(hMainWindow, NULL, TRUE);
}

static void DesenharBotaoNavegacao(const DRAWITEMSTRUCT *dis)
{
    RECT rc;
    HBRUSH fundo;
    COLORREF corTexto;
    COLORREF corFaixa;
    HFONT fonte;
    HFONT fonteAnterior;
    char texto[64];
    int ativo;

    if (!dis || !dis->hwndItem)
        return;

    rc = dis->rcItem;
    ativo = GetDlgCtrlID(dis->hwndItem) == TAB_RESUMO + abaAtual;
    corTexto = (ativo || (dis->itemState & ODS_SELECTED))
                   ? RGB(255, 255, 255)
                   : RGB(205, 211, 218);
    corFaixa = (ativo || (dis->itemState & ODS_SELECTED))
                   ? RGB(55, 145, 215)
                   : RGB(80, 88, 98);

    fundo = CreateSolidBrush((ativo || (dis->itemState & ODS_SELECTED))
                                 ? RGB(42, 53, 66)
                                 : RGB(31, 39, 49));
    FillRect(dis->hDC, &rc, fundo);
    DeleteObject(fundo);

    if (dis->itemState & ODS_FOCUS)
    {
        HPEN foco = CreatePen(PS_SOLID, 1, RGB(130, 190, 235));
        HPEN anterior = (HPEN)SelectObject(dis->hDC, foco);
        Rectangle(dis->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(dis->hDC, anterior);
        DeleteObject(foco);
    }

    {
        RECT faixa = {rc.left, rc.top, rc.left + 4, rc.bottom};
        HBRUSH pincelFaixa = CreateSolidBrush(corFaixa);
        FillRect(dis->hDC, &faixa, pincelFaixa);
        DeleteObject(pincelFaixa);
    }

    GetWindowTextA(dis->hwndItem, texto, sizeof(texto));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, corTexto);
    fonte = hFontUI ? hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    fonteAnterior = (HFONT)SelectObject(dis->hDC, fonte);
    rc.left += 16;
    DrawTextA(dis->hDC, texto, -1, &rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dis->hDC, fonteAnterior);
}

static void RedimensionarConteudo(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    /*
     * Posições calculadas da direita para a esquerda (com margem de 4px):
     * 1. Overlay [Ctrl+O]     : Largura 118 -> X = rc.right - 122
     * 2. Engrenagem (CFG)     : Largura 26  -> X = rc.right - 152
     * 3. Atualizacao: X s     : Largura 116 -> X = rc.right - 272
     * 4. Definicoes [Ctrl+D]  : Largura 116 -> X = rc.right - 392
     */

    if (hBtnOverlay)
        MoveWindow(hBtnOverlay, rc.right - 122, 3, 118, 22, TRUE);

    {
        HWND hBtnCfg = GetDlgItem(hwnd, ID_CONFIG_OVERLAY);
        if (hBtnCfg)
            MoveWindow(hBtnCfg, rc.right - 152, 3, 26, 22, TRUE);
    }

    if (hBtnInterval)
        MoveWindow(hBtnInterval, rc.right - 272, 3, 116, 22, TRUE);

    if (hBtnEstiloMain)
        MoveWindow(hBtnEstiloMain, rc.right - 392, 3, 116, 22, TRUE);

    if (hTab)
    {
        MoveWindow(hTab, NAV_LARGURA, 0,
                   rc.right - NAV_LARGURA, rc.bottom, TRUE);

        TabCtrl_AdjustRect(hTab, FALSE, &rc);
        rc.left += NAV_LARGURA;

        for (int i = 0; i < 7; i++)
        {
            if (hNav[i])
                MoveWindow(hNav[i], 12, 42 + i * 38,
                           NAV_LARGURA - 24, 30, TRUE);
        }

        if (hEdit)
            MoveWindow(hEdit, rc.left, rc.top + 172,
                       rc.right - rc.left,
                       rc.bottom - rc.top - 172, TRUE);

        if (hDashboard)
            MoveWindow(hDashboard, rc.left, rc.top,
                       rc.right - rc.left, 172, TRUE);

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

        /* Barra de pesquisa + ListView de processos */
        RedimensionarDefinicoes(&rc);

        {
            int cx = rc.left;
            int cy = rc.top;
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;
            int barH = 28;   /* altura da barra de pesquisa */
            int lblW = 74;   /* "Pesquisar:" */
            int editW = 240; /* caixa de texto */
            if (editW > cw - lblW - 8)
                editW = cw - lblW - 8;
            if (editW < 60)
                editW = 60;

            if (hPainelProcessos) /* label "Pesquisar:" */
                MoveWindow(hPainelProcessos,
                           cx + 4, cy + 4, lblW, 22, TRUE);

            if (hEditPesquisaProc)
                MoveWindow(hEditPesquisaProc,
                           cx + 4 + lblW + 4, cy + 3, editW, 22, TRUE);

            if (hLabelProcCount)
                MoveWindow(hLabelProcCount,
                           cx + 4 + lblW + 4 + editW + 8, cy + 4,
                           cw - (4 + lblW + 4 + editW + 8) - 4, 22, TRUE);

            if (hListViewProc && ch > barH)
                MoveWindow(hListViewProc,
                           cx, cy + barH,
                           cw, ch - barH, TRUE);
        }
    }
}

/* =========================================================================
 * Painel de processos full-list  (aba "Processos")
 * ========================================================================= */

/* Comparadores para qsort — usam g_lvSortCol / g_lvSortDesc */
static int CompararLvProc(const void *a, const void *b)
{
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    int r = 0;

    switch (g_lvSortCol)
    {
    case 0: /* Nome */
        r = _stricmp(p1->exeFile, p2->exeFile);
        break;
    case 1: /* PID */
        r = (p1->pid > p2->pid) ? 1 : (p1->pid < p2->pid) ? -1
                                                          : 0;
        break;
    case 2: /* CPU */
        r = (p1->cpuPercent > p2->cpuPercent) ? 1 : (p1->cpuPercent < p2->cpuPercent) ? -1
                                                                                      : 0;
        break;
    case 3: /* RAM */
        r = (p1->memUsageMB > p2->memUsageMB) ? 1 : (p1->memUsageMB < p2->memUsageMB) ? -1
                                                                                      : 0;
        break;
    }
    return g_lvSortDesc ? -r : r;
}

/* Preenche g_lvProcessos com base no filtro de texto actual */
static void FiltrarListaProcessos(void)
{
    char filtro[MAX_PATH];
    char filtroLow[MAX_PATH];
    int i, n;

    filtro[0] = '\0';
    if (hEditPesquisaProc)
        GetWindowTextA(hEditPesquisaProc, filtro, (int)sizeof(filtro));

    /* converte filtro para minúsculas para comparação case-insensitive */
    strncpy_s(filtroLow, sizeof(filtroLow), filtro, _TRUNCATE);
    {
        char *p;
        for (p = filtroLow; *p; p++)
            *p = (char)tolower((unsigned char)*p);
    }

    n = 0;
    for (i = 0; i < g_totalProcessosBruto && n < MAX_PROCESSES; i++)
    {
        if (filtroLow[0] == '\0')
        {
            /* sem filtro — copia tudo */
            g_lvProcessos[n++] = listaProcessos[i];
        }
        else
        {
            /* testa match no nome */
            char nomeLow[MAX_PATH];
            strncpy_s(nomeLow, sizeof(nomeLow),
                      listaProcessos[i].exeFile, _TRUNCATE);
            {
                char *p;
                for (p = nomeLow; *p; p++)
                    *p = (char)tolower((unsigned char)*p);
            }

            if (strstr(nomeLow, filtroLow) != NULL)
            {
                g_lvProcessos[n++] = listaProcessos[i];
                continue;
            }

            /* testa match no PID */
            {
                char pidStr[16];
                snprintf(pidStr, sizeof(pidStr), "%u",
                         listaProcessos[i].pid);
                if (strstr(pidStr, filtroLow) != NULL)
                    g_lvProcessos[n++] = listaProcessos[i];
            }
        }
    }
    g_lvTotal = n;

    /* ordena conforme coluna activa */
    if (g_lvTotal > 1)
        qsort(g_lvProcessos, (size_t)g_lvTotal,
              sizeof(ProcessoInfo), CompararLvProc);
}

/* Actualiza o conteúdo do ListView com os dados mais recentes */
static void AtualizarListViewProcessos(void)
{
    char buf[64];
    int i;

    if (!hListViewProc)
        return;

    /* guarda o total bruto antes de filtrar */
    /* (MonitorarProcessos() já preencheu listaProcessos[],
       e usamos totalListaProcessos, preenchido pelo mesmo ciclo de recolha. */
    g_totalProcessosBruto = totalListaProcessos; /* mesmo ciclo */

    FiltrarListaProcessos();

    /* Suspende redesenho para evitar flicker */
    SendMessage(hListViewProc, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemCountEx(hListViewProc, g_lvTotal,
                            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    SendMessage(hListViewProc, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hListViewProc, NULL, FALSE);

    /* Actualiza label de contagem */
    if (hLabelProcCount)
    {
        snprintf(buf, sizeof(buf), "%d processos  (%d visíveis)",
                 g_totalProcessosBruto, g_lvTotal);
        SetWindowTextA(hLabelProcCount, buf);
    }

    (void)i;
}

/* Callback de notificação do ListView (LVN_GETDISPINFO para virtual list) */
static void LvProcGetDispInfo(NMLVDISPINFOA *pdi)
{
    int idx;

    if (!(pdi->item.mask & LVIF_TEXT))
        return;

    idx = pdi->item.iItem;
    if (idx < 0 || idx >= g_lvTotal)
        return;

    switch (pdi->item.iSubItem)
    {
    case 0: /* Nome */
        strncpy_s(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                  g_lvProcessos[idx].exeFile, _TRUNCATE);
        break;
    case 1: /* PID */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                 "%u", g_lvProcessos[idx].pid);
        break;
    case 2: /* CPU */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                 "%.1f%%", g_lvProcessos[idx].cpuPercent);
        break;
    case 3: /* RAM */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                 "%lu MB",
                 (unsigned long)g_lvProcessos[idx].memUsageMB);
        break;
    }
}

/* Trata cliques nos cabeçalhos do ListView para ordenação */
static void LvProcColumnClick(int col)
{
    if (g_lvSortCol == col)
        g_lvSortDesc = !g_lvSortDesc;
    else
    {
        g_lvSortCol = col;
        g_lvSortDesc = (col == 2 || col == 3); /* CPU/RAM desc por omissão */
    }
    AtualizarListViewProcessos();
}

/* Cria o painel de processos.
 * Todos os controlos são filhos DIRECTOS de hwndPai (a janela principal),
 * para que o WM_NOTIFY com LVN_GETDISPINFO chegue ao WindowProc sem
 * ser intercepado por um container intermédio. O "painel" é apenas
 * o próprio hListViewProc + controlos de barra de pesquisa; usamos
 * hPainelProcessos como sentinela NULL/não-NULL para saber se foi criado.
 */
static void CriarPainelProcessos(HWND hwndPai)
{
    LVCOLUMNA lvc;
    HINSTANCE hInst = GetModuleHandle(NULL);

    /* Label "Pesquisar:" — filho directo da janela principal */
    hPainelProcessos = CreateWindowExA(/* reutilizamos como label */
                                       0, "STATIC", "Pesquisar:",
                                       WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                                       0, 0, 74, 22,
                                       hwndPai, NULL, hInst, NULL);

    /* Edit de pesquisa */
    hEditPesquisaProc = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 220, 22,
        hwndPai,
        (HMENU)(UINT_PTR)IDC_EDIT_PESQUISA_PROC,
        hInst, NULL);

    /* Label de contagem */
    hLabelProcCount = CreateWindowExA(
        0, "STATIC", "0 processos",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 260, 22,
        hwndPai,
        (HMENU)(UINT_PTR)IDC_LABEL_PROC_COUNT,
        hInst, NULL);

    /* ListView virtual — filho directo da janela principal */
    {
        INITCOMMONCONTROLSEX icc2;
        icc2.dwSize = sizeof(icc2);
        icc2.dwICC = ICC_LISTVIEW_CLASSES;
        InitCommonControlsEx(&icc2);
    }

    hListViewProc = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA, "",
        WS_CHILD | WS_VSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA |
            LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
        0, 0, 100, 100,
        hwndPai,
        (HMENU)(UINT_PTR)IDC_LISTVIEW_PROC,
        hInst, NULL);

    if (!hListViewProc)
        return;

    ListView_SetExtendedListViewStyle(hListViewProc,
                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    /* Colunas */
    ZeroMemory(&lvc, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    lvc.pszText = "Nome";
    lvc.cx = 220;
    lvc.fmt = LVCFMT_LEFT;
    ListView_InsertColumn(hListViewProc, 0, &lvc);

    lvc.pszText = "PID";
    lvc.cx = 70;
    lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 1, &lvc);

    lvc.pszText = "CPU (%)";
    lvc.cx = 90;
    lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 2, &lvc);

    lvc.pszText = "RAM (MB)";
    lvc.cx = 90;
    lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 3, &lvc);
}

/* Stub mantido para compatibilidade — posicionamento é feito em RedimensionarConteudo */
static void RedimensionarPainelProcessos(HWND hwndPai)
{
    (void)hwndPai;
}

static void AtualizarDefinicoesVisibilidade(void)
{
    int i, j;

    if (hSettingsTitle)
        ShowWindow(hSettingsTitle, abaAtual == 6 ? SW_SHOW : SW_HIDE);

    for (i = 0; i < SETTINGS_SECTIONS; i++)
    {
        if (hSettingsHeaders[i])
        {
            char texto[96];
            const char *nome;
            switch (i)
            {
            case 0:
                nome = "Alertas e limites";
                break;
            case 1:
                nome = "Aparencia";
                break;
            case 2:
                nome = "Overlay";
                break;
            case 3:
                nome = "Tray";
                break;
            default:
                nome = "Atualizacao e dados";
                break;
            }
            snprintf(texto, sizeof(texto), "%s %s",
                     g_settingsOpen[i] ? "[-]" : "[+]", nome);
            SetWindowTextA(hSettingsHeaders[i], texto);
            ShowWindow(hSettingsHeaders[i], abaAtual == 6 ? SW_SHOW : SW_HIDE);
        }

        for (j = 0; j < 3; j++)
        {
            if (hSettingsActions[i][j])
                ShowWindow(hSettingsActions[i][j],
                           (abaAtual == 6 && g_settingsOpen[i] && j < (i == 2 ? 2 : (i == 4 ? 3 : 1)))
                               ? SW_SHOW
                               : SW_HIDE);
        }
    }

    if (hSettingsActions[2][1])
        SetWindowTextA(hSettingsActions[2][1],
                       overlayAtivo ? "Desativar overlay" : "Ativar overlay");
    if (hSettingsActions[4][1])
        SetWindowTextA(hSettingsActions[4][1],
                       logAtivo ? "Parar log CSV" : "Iniciar log CSV");
}

static void RedimensionarDefinicoes(const RECT *content)
{
    int i, j;
    int y;
    int x = content->left + 20;
    int w = content->right - content->left - 40;

    if (w < 220)
        w = 220;

    if (hSettingsTitle)
        MoveWindow(hSettingsTitle, x, content->top + 10, w, 28, TRUE);

    y = content->top + 44;
    for (i = 0; i < SETTINGS_SECTIONS; i++)
    {
        if (hSettingsHeaders[i])
            MoveWindow(hSettingsHeaders[i], x, y, w, 28, TRUE);
        y += 34;

        for (j = 0; j < 3; j++)
        {
            if (hSettingsActions[i][j])
                MoveWindow(hSettingsActions[i][j], x + 24, y,
                           (w > 270 ? 250 : w - 24), 24, TRUE);
            if (g_settingsOpen[i] && (j < (i == 2 ? 2 : (i == 4 ? 3 : 1))))
                y += 30;
        }
        y += 8;
    }
    AtualizarDefinicoesVisibilidade();
}

static void CriarPainelDefinicoes(HWND hwndPai)
{
    HINSTANCE hInst = GetModuleHandle(NULL);
    const char *headers[SETTINGS_SECTIONS] = {
        "[-] Alertas e limites", "[-] Aparencia", "[-] Overlay",
        "[-] Tray", "[-] Atualizacao e dados"};
    const char *actions[SETTINGS_SECTIONS][3] = {
        {"Configurar limites de alerta", NULL, NULL},
        {"Estilo e cores da janela principal", NULL, NULL},
        {"Configurar overlay", "Ativar overlay", NULL},
        {"Personalizar Tray", NULL, NULL},
        {"Mudar intervalo de atualizacao", "Iniciar/parar log CSV", "Exportar snapshot"}};
    const int headerIds[SETTINGS_SECTIONS] = {
        IDC_SETTINGS_HDR_ALERT, IDC_SETTINGS_HDR_APAR, IDC_SETTINGS_HDR_OVER,
        IDC_SETTINGS_HDR_TRAY, IDC_SETTINGS_HDR_DADOS};
    const int actionIds[SETTINGS_SECTIONS][3] = {
        {IDC_SETTINGS_ALERT, 0, 0},
        {IDC_SETTINGS_APAR, 0, 0},
        {IDC_SETTINGS_OV_CFG, IDC_SETTINGS_OV_TOGGLE, 0},
        {IDC_SETTINGS_TRAY, 0, 0},
        {IDC_SETTINGS_INTERVAL, IDC_SETTINGS_LOG, IDC_SETTINGS_SNAPSHOT}};
    int i, j;

    hSettingsTitle = CreateWindowExA(
        0, "STATIC", "Definicoes centralizadas do WinMon",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 300, 28, hwndPai,
        (HMENU)(UINT_PTR)IDC_SETTINGS_TITLE, hInst, NULL);

    for (i = 0; i < SETTINGS_SECTIONS; i++)
    {
        hSettingsHeaders[i] = CreateWindowExA(
            0, "BUTTON", headers[i],
            WS_CHILD | BS_PUSHBUTTON,
            0, 0, 100, 28, hwndPai,
            (HMENU)(UINT_PTR)headerIds[i], hInst, NULL);

        for (j = 0; j < 3; j++)
        {
            if (!actions[i][j])
                continue;
            hSettingsActions[i][j] = CreateWindowExA(
                0, "BUTTON", actions[i][j],
                WS_CHILD | BS_PUSHBUTTON,
                0, 0, 100, 24, hwndPai,
                (HMENU)(UINT_PTR)actionIds[i][j], hInst, NULL);
        }
    }
}

static void CriarAbas(HWND hwnd)
{
    TCITEMA item;
    const char *nomes[] = {
        "Resumo", "CPU", "Memoria", "Disco", "Rede", "Processos", "Definicoes"};
    int i;

    hTab = CreateWindowExA(
        0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT;

    for (i = 0; i < 7; i++)
    {
        item.pszText = (LPSTR)nomes[i];
        TabCtrl_InsertItem(hTab, i, &item);
    }

    {
        const char *navNomes[] = {
            "Visao geral", "CPU", "Memoria", "Disco", "Rede", "Processos", "Definicoes"};

        for (i = 0; i < 7; i++)
        {
            hNav[i] = CreateWindowExA(
                0, "BUTTON", navNomes[i],
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                12, 42 + i * 38, NAV_LARGURA - 24, 30,
                hwnd, (HMENU)(UINT_PTR)(TAB_RESUMO + i),
                GetModuleHandle(NULL), NULL);

            if (hNav[i] && hFontUI)
                SendMessage(hNav[i], WM_SETFONT, (WPARAM)hFontUI, TRUE);
        }
    }

    ShowWindow(hTab, SW_HIDE);

    hEdit = CreateWindowExA(
        0, "RICHEDIT50W", "A recolher dados do sistema...",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY,
        0, 0, 100, 100,
        hwnd, NULL, NULL, NULL);

    hDashboard = CreateWindowExA(
        0, "HardwareMonitorDashboard", "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 172,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    AplicarEstiloJanelaPrincipal();

    SendMessage(hEdit, EM_EXLIMITTEXT, 0, (LPARAM)(BUFFER_SIZE * 2));

    hGraphCPU = CriarGrafico(hwnd, GRAPH_CPU);
    hGraphRAM = CriarGrafico(hwnd, GRAPH_RAM);
    hGraphDisk = CriarGrafico(hwnd, GRAPH_DISK);
    hGraphNet = CriarGrafico(hwnd, GRAPH_NET);
    hGraphProcesses = CriarGrafico(hwnd, GRAPH_PROCESS);

    CriarPainelProcessos(hwnd);
    CriarPainelDefinicoes(hwnd);

    hBtnOverlay = CreateWindowExA(
        0, "BUTTON", "Overlay [Ctrl+O]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 118, 22,
        hwnd, (HMENU)(UINT_PTR)ID_TOGGLE_OVERLAY,
        GetModuleHandle(NULL), NULL);

    hBtnInterval = CreateWindowExA(
        0, "BUTTON", "Atualizacao: 1 s",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 116, 22,
        hwnd, (HMENU)(UINT_PTR)ID_CYCLE_INTERVAL,
        GetModuleHandle(NULL), NULL);

    hBtnEstiloMain = CreateWindowExA(
        0, "BUTTON", "Definicoes [Ctrl+D]",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 116, 22,
        hwnd, (HMENU)(UINT_PTR)ID_CONFIG_SETTINGS,
        GetModuleHandle(NULL), NULL);

    CreateWindowExW(
        0, L"BUTTON", L"Config",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 26, 22,
        hwnd, (HMENU)(UINT_PTR)ID_CONFIG_OVERLAY,
        GetModuleHandle(NULL), NULL);

    if (hFontUI)
    {
        SendMessage(hBtnOverlay, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hBtnInterval, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessage(hBtnEstiloMain, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    }

    MostrarAba(0);
}

/* ------------------------------------------------------------------------- */
/* Logging CSV Contínuo                                                      */
/* ------------------------------------------------------------------------- */

static void IniciarLogCSV(void)
{
    SYSTEMTIME st;
    errno_t err;

    if (hLogCSV)
        return;

    GetLocalTime(&st);
    snprintf(logNomeFicheiro, sizeof(logNomeFicheiro),
             "winmon_log_%04d%02d%02d_%02d%02d%02d.csv",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    err = fopen_s(&hLogCSV, logNomeFicheiro, "a");
    if (err != 0 || !hLogCSV)
    {
        MessageBoxA(hMainWindow,
                    "Nao foi possivel criar o ficheiro de log.",
                    "Erro de Log", MB_OK | MB_ICONERROR);
        hLogCSV = NULL;
        logAtivo = 0;
        return;
    }

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

    logAtivo = 1;
    logTickContador = 0;
}

static void FecharLogCSV(void)
{
    if (hLogCSV)
    {
        fclose(hLogCSV);
        hLogCSV = NULL;
    }
    logAtivo = 0;
}

static void EscreverLinhaLog(void)
{
    SYSTEMTIME st;
    int i;

    if (!logAtivo || !hLogCSV)
        return;

    logTickContador++;
    if (logTickContador < LOG_INTERVALO_SEGUNDOS)
        return;
    logTickContador = 0;

    GetLocalTime(&st);

    fprintf(hLogCSV,
            "%04d-%02d-%02d %02d:%02d:%02d,"
            "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            ultimoCpuPercent, ultimoRamPercent,
            ultimoDiskRead, ultimoDiskWrite,
            ultimoNetDown, ultimoNetUp);

    for (i = 0; i < numNucleosMonitorizados; i++)
        fprintf(hLogCSV, ",%.2f", coresCpuAtuais[i]);

    for (i = 0; i < numZonasTemp; i++)
        fprintf(hLogCSV, ",%.1f", tempAtual[i]);

    fprintf(hLogCSV, "\n");
    fflush(hLogCSV);
}

/* ------------------------------------------------------------------------- */
/* Alertas e Configuração INI                                                */
/* ------------------------------------------------------------------------- */

#define INI_FICHEIRO "winmon.ini"
#define INI_SECAO "Alertas"
#define INI_SECAO_GERAL "Geral"

static void AtualizarTextoIntervalo(void)
{
    char texto[32];

    if (!hBtnInterval)
        return;

    if (intervaloAtualizacaoMs < 1000)
        snprintf(texto, sizeof(texto), "Atualizacao: %u ms", intervaloAtualizacaoMs);
    else
        snprintf(texto, sizeof(texto), "Atualizacao: %u s", intervaloAtualizacaoMs / 1000);

    SetWindowTextA(hBtnInterval, texto);
}

static void CiclarIntervaloAtualizacao(void)
{
    size_t i;

    for (i = 0; i < sizeof(intervalosAtualizacao) / sizeof(intervalosAtualizacao[0]); i++)
    {
        if (intervaloAtualizacaoMs == intervalosAtualizacao[i])
        {
            i = (i + 1) % (sizeof(intervalosAtualizacao) / sizeof(intervalosAtualizacao[0]));
            intervaloAtualizacaoMs = intervalosAtualizacao[i];
            SetTimer(hMainWindow, TIMER_ID, intervaloAtualizacaoMs, NULL);
            {
                char valor[16];
                snprintf(valor, sizeof(valor), "%u", intervaloAtualizacaoMs);
                WritePrivateProfileStringA(INI_SECAO_GERAL, "IntervaloMs",
                                           valor, INI_FICHEIRO);
            }
            AtualizarTextoIntervalo();
            return;
        }
    }
}

static void CarregarConfigVisual(void)
{
    char val[32];
    GetPrivateProfileStringA("Aparencia", "FundoGrafico", "16579578", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corFundoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Grelha", "15262945", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGrelhaGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Eixo", "12499380", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corEixoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "TextoGrafico", "3157032", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corTextoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Cpu", "13792035", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoCpu = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Ram", "12143505", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoRam = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "DiscoRead", "13137960", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoDiscoRead = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "DiscoWrite", "2980055", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoDiscoWrite = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "NetDown", "6262307", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoNetDown = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "NetUp", "5263565", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoNetUp = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Processo", "4605645", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoProcesso = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Core", "6263125", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoCore = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Espessura", "2", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.espessuraLinhas = atoi(val);
    GetPrivateProfileStringA("Aparencia", "MostrarGrelha", "1", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.mostrarGrelha = atoi(val) != 0;
    GetPrivateProfileStringA("Aparencia", "MostrarEixos", "1", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.mostrarEixos = atoi(val) != 0;
    if (g_mainConfig.espessuraLinhas < 1 || g_mainConfig.espessuraLinhas > 5)
        g_mainConfig.espessuraLinhas = 2;
}

static void GravarConfigVisual(void)
{
    char val[32];
#define WV(k, v)                                                       \
    do                                                                 \
    {                                                                  \
        snprintf(val, sizeof(val), "%lu", (unsigned long)(v));         \
        WritePrivateProfileStringA("Aparencia", k, val, INI_FICHEIRO); \
    } while (0)
    WV("FundoGrafico", g_mainConfig.corFundoGrafico);
    WV("Grelha", g_mainConfig.corGrelhaGrafico);
    WV("Eixo", g_mainConfig.corEixoGrafico);
    WV("TextoGrafico", g_mainConfig.corTextoGrafico);
    WV("Cpu", g_mainConfig.corGraficoCpu);
    WV("Ram", g_mainConfig.corGraficoRam);
    WV("DiscoRead", g_mainConfig.corGraficoDiscoRead);
    WV("DiscoWrite", g_mainConfig.corGraficoDiscoWrite);
    WV("NetDown", g_mainConfig.corGraficoNetDown);
    WV("NetUp", g_mainConfig.corGraficoNetUp);
    WV("Processo", g_mainConfig.corGraficoProcesso);
    WV("Core", g_mainConfig.corGraficoCore);
    WV("Espessura", g_mainConfig.espessuraLinhas);
    WV("MostrarGrelha", g_mainConfig.mostrarGrelha);
    WV("MostrarEixos", g_mainConfig.mostrarEixos);
#undef WV
}

static void CarregarConfigTray(void)
{
    char val[16];
#define RV(k, d) GetPrivateProfileStringA("Tray", k, d, val, sizeof(val), INI_FICHEIRO)
    RV("MostrarCPU", "1");
    g_trayConfig.mostrarCpu = atoi(val);
    RV("MostrarRAM", "1");
    g_trayConfig.mostrarRam = atoi(val);
    RV("MostrarTemp", "0");
    g_trayConfig.mostrarTemperatura = atoi(val);
    RV("MostrarRede", "0");
    g_trayConfig.mostrarRede = atoi(val);
    RV("MostrarDisco", "0");
    g_trayConfig.mostrarDisco = atoi(val);
    RV("MostrarUptime", "0");
    g_trayConfig.mostrarUptime = atoi(val);
    RV("Clique", "1");
    g_trayConfig.restaurarComClique = atoi(val);
    RV("DuploClique", "0");
    g_trayConfig.restaurarComDuploClique = atoi(val);
#undef RV
}

static void GravarConfigTray(void)
{
    char val[16];
#define WVTR(k, v)                                                \
    do                                                            \
    {                                                             \
        snprintf(val, sizeof(val), "%d", (v));                    \
        WritePrivateProfileStringA("Tray", k, val, INI_FICHEIRO); \
    } while (0)
    WVTR("MostrarCPU", g_trayConfig.mostrarCpu);
    WVTR("MostrarRAM", g_trayConfig.mostrarRam);
    WVTR("MostrarTemp", g_trayConfig.mostrarTemperatura);
    WVTR("MostrarRede", g_trayConfig.mostrarRede);
    WVTR("MostrarDisco", g_trayConfig.mostrarDisco);
    WVTR("MostrarUptime", g_trayConfig.mostrarUptime);
    WVTR("Clique", g_trayConfig.restaurarComClique);
    WVTR("DuploClique", g_trayConfig.restaurarComDuploClique);
#undef WVTR
}

static void CarregarConfigIni(void)
{
    char val[32];

    GetPrivateProfileStringA(INI_SECAO, "LimiteCPU", "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteCpuPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteRAM", "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteRamPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteDisco", "95.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteDiscoPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO_GERAL, "IntervaloMs", "1000",
                             val, sizeof(val), INI_FICHEIRO);
    intervaloAtualizacaoMs = (UINT)atoi(val);

    if (limiteCpuPercent < 1.0 || limiteCpuPercent > 100.0)
        limiteCpuPercent = LIMITE_CPU_PERCENT_DEFAULT;
    if (limiteRamPercent < 1.0 || limiteRamPercent > 100.0)
        limiteRamPercent = LIMITE_RAM_PERCENT_DEFAULT;
    if (limiteDiscoPercent < 1.0 || limiteDiscoPercent > 100.0)
        limiteDiscoPercent = LIMITE_DISCO_PERCENT_DEFAULT;
    if (intervaloAtualizacaoMs != 500 && intervaloAtualizacaoMs != 1000 &&
        intervaloAtualizacaoMs != 2000 && intervaloAtualizacaoMs != 5000)
        intervaloAtualizacaoMs = 1000;
}

static void GravarConfigIni(void)
{
    char val[32];

    snprintf(val, sizeof(val), "%.1f", limiteCpuPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteCPU", val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteRamPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteRAM", val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteDiscoPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteDisco", val, INI_FICHEIRO);
}

#define IDC_EDIT_CPU 3001
#define IDC_EDIT_RAM 3002
#define IDC_EDIT_DISCO 3003

static INT_PTR CALLBACK DialogoAlertasProc(HWND hDlg, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
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
        if (LOWORD(wParam) == IDOK)
        {
            char buf[16];
            double v;

            GetDlgItemTextA(hDlg, IDC_EDIT_CPU, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteCpuPercent = v;

            GetDlgItemTextA(hDlg, IDC_EDIT_RAM, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteRamPercent = v;

            GetDlgItemTextA(hDlg, IDC_EDIT_DISCO, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteDiscoPercent = v;

            GravarConfigIni();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
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

static void MostrarDialogoAlertas(HWND hwndPai)
{
    static WORD dlgBuf[512];
    WORD *p = dlgBuf;

#define WRITE_STR_W(s)             \
    do                             \
    {                              \
        const wchar_t *_ws = (s);  \
        while (*_ws)               \
            *p++ = (WORD) * _ws++; \
        *p++ = 0;                  \
    } while (0)

#define ALIGN_DWORD()       \
    if (((ULONG_PTR)p) & 2) \
    p++

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = 8;
    dt->x = 0;
    dt->y = 0;
    dt->cx = 200;
    dt->cy = 130;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);

    *p++ = 0;
    *p++ = 0;
    WRITE_STR_W(L"Configurar Limites de Alerta (%)");
    *p++ = 9;
    WRITE_STR_W(L"Segoe UI");

#define ADD_ITEM(sty, ex, xx, yy, ww, hh, iid, cls, txt) \
    do                                                   \
    {                                                    \
        ALIGN_DWORD();                                   \
        {                                                \
            DLGITEMTEMPLATE *_it = (DLGITEMTEMPLATE *)p; \
            _it->style = (sty);                          \
            _it->dwExtendedStyle = (ex);                 \
            _it->x = (xx);                               \
            _it->y = (yy);                               \
            _it->cx = (ww);                              \
            _it->cy = (hh);                              \
            _it->id = (iid);                             \
            p += sizeof(DLGITEMTEMPLATE) / sizeof(WORD); \
        }                                                \
        *p++ = 0xFFFF;                                   \
        *p++ = (cls);                                    \
        WRITE_STR_W(txt);                                \
        *p++ = 0;                                        \
    } while (0)

    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 14, 70, 10, -1, 0x0082, L"Limite CPU (%):");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 34, 70, 10, -1, 0x0082, L"Limite RAM (%):");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 54, 70, 10, -1, 0x0082, L"Limite Disco (%):");

    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 12, 40, 12, IDC_EDIT_CPU, 0x0081, L"");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 32, 40, 12, IDC_EDIT_RAM, 0x0081, L"");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 52, 40, 12, IDC_EDIT_DISCO, 0x0081, L"");

    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 34, 108, 60, 14, IDOK, 0x0080, L"OK");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 106, 108, 60, 14, IDCANCEL, 0x0080, L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL),
                       (LPDLGTEMPLATE)dlgBuf,
                       hwndPai,
                       DialogoAlertasProc);

#undef WRITE_STR_W
#undef ALIGN_DWORD
#undef ADD_ITEM
}

/* ------------------------------------------------------------------------- */
/* Diálogo de Estilo da Janela Principal                                     */
/* ------------------------------------------------------------------------- */

#define IDC_MAIN_BTN_FONTE 5001
#define IDC_MAIN_BTN_BG 5002
#define IDC_MAIN_BTN_TEXT 5003
#define IDC_MAIN_BTN_GCPU 5004
#define IDC_MAIN_BTN_GRAM 5005
#define IDC_MAIN_BTN_GDISKR 5006
#define IDC_MAIN_BTN_GDISKW 5007
#define IDC_MAIN_BTN_GNETD 5008
#define IDC_MAIN_BTN_GNETU 5009
#define IDC_MAIN_BTN_GPROC 5010
#define IDC_MAIN_BTN_GCORE 5011
#define IDC_MAIN_BTN_GBG 5012
#define IDC_MAIN_BTN_GGRID 5013
#define IDC_MAIN_BTN_GAXIS 5014
#define IDC_MAIN_BTN_GTEXT 5015
#define IDC_MAIN_CHK_GRID 5016
#define IDC_MAIN_CHK_AXIS 5017
#define IDC_MAIN_SPIN_THICK 5018

static INT_PTR CALLBACK DialogoEstiloMainProc(HWND hDlg, UINT msg,
                                              WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        char tb[16];
        snprintf(tb, sizeof(tb), "%d", g_mainConfig.espessuraLinhas);
        SetDlgItemTextA(hDlg, IDC_MAIN_SPIN_THICK, tb);
        CheckDlgButton(hDlg, IDC_MAIN_CHK_GRID, g_mainConfig.mostrarGrelha ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_MAIN_CHK_AXIS, g_mainConfig.mostrarEixos ? BST_CHECKED : BST_UNCHECKED);
    }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_MAIN_BTN_FONTE:
            if (SelecionarFonte(hDlg, &g_mainConfig.fonteEdit))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;

        case IDC_MAIN_BTN_BG:
            if (SelecionarCor(hDlg, &g_mainConfig.corFundoEdit))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;

        case IDC_MAIN_BTN_TEXT:
            if (SelecionarCor(hDlg, &g_mainConfig.corTextoEdit))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;

        case IDC_MAIN_BTN_GCPU:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoCpu))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;

        case IDC_MAIN_BTN_GRAM:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoRam))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;

        case IDC_MAIN_BTN_GDISKR:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoDiscoRead))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GDISKW:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoDiscoWrite))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GNETD:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoNetDown))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GNETU:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoNetUp))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GPROC:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoProcesso))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GCORE:
            if (SelecionarCor(hDlg, &g_mainConfig.corGraficoCore))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GBG:
            if (SelecionarCor(hDlg, &g_mainConfig.corFundoGrafico))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GGRID:
            if (SelecionarCor(hDlg, &g_mainConfig.corGrelhaGrafico))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GAXIS:
            if (SelecionarCor(hDlg, &g_mainConfig.corEixoGrafico))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_BTN_GTEXT:
            if (SelecionarCor(hDlg, &g_mainConfig.corTextoGrafico))
            {
                AplicarEstiloJanelaPrincipal();
                GravarConfigVisual();
            }
            return TRUE;
        case IDC_MAIN_CHK_GRID:
            g_mainConfig.mostrarGrelha = (IsDlgButtonChecked(hDlg, IDC_MAIN_CHK_GRID) == BST_CHECKED);
            AplicarEstiloJanelaPrincipal();
            GravarConfigVisual();
            return TRUE;
        case IDC_MAIN_CHK_AXIS:
            g_mainConfig.mostrarEixos = (IsDlgButtonChecked(hDlg, IDC_MAIN_CHK_AXIS) == BST_CHECKED);
            AplicarEstiloJanelaPrincipal();
            GravarConfigVisual();
            return TRUE;
        case IDOK:
        {
            char tb[16];
            GetDlgItemTextA(hDlg, IDC_MAIN_SPIN_THICK, tb, sizeof(tb));
            {
                int t = atoi(tb);
                if (t >= 1 && t <= 5)
                    g_mainConfig.espessuraLinhas = t;
            }
            GravarConfigVisual();
            AplicarEstiloJanelaPrincipal();
            EndDialog(hDlg, IDOK);
        }
            return TRUE;
        case IDCANCEL:
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

static void MostrarDialogoEstiloPrincipal(HWND hwndPai)
{
    static WORD dlgBuf[512];
    WORD *p = dlgBuf;

#define WRITE_STR_W(s)             \
    do                             \
    {                              \
        const wchar_t *_ws = (s);  \
        while (*_ws)               \
            *p++ = (WORD) * _ws++; \
        *p++ = 0;                  \
    } while (0)

#define ALIGN_DWORD()       \
    if (((ULONG_PTR)p) & 2) \
    p++

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = 19;
    dt->x = 0;
    dt->y = 0;
    dt->cx = 285;
    dt->cy = 270;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);

    *p++ = 0;
    *p++ = 0;
    WRITE_STR_W(L"Estilo/cores da janela principal");
    *p++ = 9;
    WRITE_STR_W(L"Segoe UI");

#define ADD_ITEM(sty, ex, xx, yy, ww, hh, iid, cls, txt) \
    do                                                   \
    {                                                    \
        ALIGN_DWORD();                                   \
        {                                                \
            DLGITEMTEMPLATE *_it = (DLGITEMTEMPLATE *)p; \
            _it->style = (sty);                          \
            _it->dwExtendedStyle = (ex);                 \
            _it->x = (xx);                               \
            _it->y = (yy);                               \
            _it->cx = (ww);                              \
            _it->cy = (hh);                              \
            _it->id = (iid);                             \
            p += sizeof(DLGITEMTEMPLATE) / sizeof(WORD); \
        }                                                \
        *p++ = 0xFFFF;                                   \
        *p++ = (cls);                                    \
        WRITE_STR_W(txt);                                \
        *p++ = 0;                                        \
    } while (0)

    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 12, 180, 14, IDC_MAIN_BTN_FONTE, 0x0080, L"Escolher fonte do log...");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 32, 180, 14, IDC_MAIN_BTN_BG, 0x0080, L"Cor de fundo do log...");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 52, 180, 14, IDC_MAIN_BTN_TEXT, 0x0080, L"Cor do texto do Log...");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 72, 180, 14, IDC_MAIN_BTN_GCPU, 0x0080, L"Cor do grafico de CPU...");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 92, 125, 14, IDC_MAIN_BTN_GRAM, 0x0080, L"Cor RAM");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 92, 125, 14, IDC_MAIN_BTN_GDISKR, 0x0080, L"Disco leitura");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 110, 125, 14, IDC_MAIN_BTN_GDISKW, 0x0080, L"Disco escrita");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 110, 125, 14, IDC_MAIN_BTN_GNETD, 0x0080, L"Rede download");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 128, 125, 14, IDC_MAIN_BTN_GNETU, 0x0080, L"Rede upload");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 128, 125, 14, IDC_MAIN_BTN_GPROC, 0x0080, L"Processo");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 146, 125, 14, IDC_MAIN_BTN_GCORE, 0x0080, L"Nucleos CPU");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 146, 125, 14, IDC_MAIN_BTN_GBG, 0x0080, L"Fundo grafico");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 164, 125, 14, IDC_MAIN_BTN_GGRID, 0x0080, L"Grelha");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 164, 125, 14, IDC_MAIN_BTN_GAXIS, 0x0080, L"Eixos");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 15, 182, 125, 14, IDC_MAIN_BTN_GTEXT, 0x0080, L"Texto grafico");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 145, 182, 125, 14, IDC_MAIN_CHK_GRID, 0x0080, L"Mostrar grelha");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 145, 198, 125, 14, IDC_MAIN_CHK_AXIS, 0x0080, L"Mostrar eixos");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 15, 205, 90, 12, -1, 0x0082, L"Espessura linhas (1-5):");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 110, 203, 35, 12, IDC_MAIN_SPIN_THICK, 0x0081, L"2");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 150, 232, 65, 14, IDOK, 0x0080, L"Fechar");

    DialogBoxIndirectA(GetModuleHandle(NULL),
                       (LPDLGTEMPLATE)dlgBuf,
                       hwndPai,
                       DialogoEstiloMainProc);

#undef WRITE_STR_W
#undef ALIGN_DWORD
#undef ADD_ITEM
}

#define IDC_TRAY_CPU 7001
#define IDC_TRAY_RAM 7002
#define IDC_TRAY_TEMP 7003
#define IDC_TRAY_NET 7004
#define IDC_TRAY_DISK 7005
#define IDC_TRAY_UPTIME 7006
#define IDC_TRAY_CLICK 7007
#define IDC_TRAY_DBLCLICK 7008

static INT_PTR CALLBACK DialogoConfigTrayProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    if (msg == WM_INITDIALOG)
    {
        CheckDlgButton(hDlg, IDC_TRAY_CPU, g_trayConfig.mostrarCpu ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_RAM, g_trayConfig.mostrarRam ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_TEMP, g_trayConfig.mostrarTemperatura ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_NET, g_trayConfig.mostrarRede ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_DISK, g_trayConfig.mostrarDisco ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_UPTIME, g_trayConfig.mostrarUptime ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_CLICK, g_trayConfig.restaurarComClique ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_TRAY_DBLCLICK, g_trayConfig.restaurarComDuploClique ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        g_trayConfig.mostrarCpu = IsDlgButtonChecked(hDlg, IDC_TRAY_CPU) == BST_CHECKED;
        g_trayConfig.mostrarRam = IsDlgButtonChecked(hDlg, IDC_TRAY_RAM) == BST_CHECKED;
        g_trayConfig.mostrarTemperatura = IsDlgButtonChecked(hDlg, IDC_TRAY_TEMP) == BST_CHECKED;
        g_trayConfig.mostrarRede = IsDlgButtonChecked(hDlg, IDC_TRAY_NET) == BST_CHECKED;
        g_trayConfig.mostrarDisco = IsDlgButtonChecked(hDlg, IDC_TRAY_DISK) == BST_CHECKED;
        g_trayConfig.mostrarUptime = IsDlgButtonChecked(hDlg, IDC_TRAY_UPTIME) == BST_CHECKED;
        g_trayConfig.restaurarComClique = IsDlgButtonChecked(hDlg, IDC_TRAY_CLICK) == BST_CHECKED;
        g_trayConfig.restaurarComDuploClique = IsDlgButtonChecked(hDlg, IDC_TRAY_DBLCLICK) == BST_CHECKED;
        GravarConfigTray();
        AtualizarTooltipTray();
        EndDialog(hDlg, IDOK);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDCANCEL)
    {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    if (msg == WM_CLOSE)
    {
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void MostrarDialogoConfigTray(HWND hwndPai)
{
    static WORD b[768];
    WORD *p = b;
#define WSTR(s)                  \
    do                           \
    {                            \
        const wchar_t *q = (s);  \
        while (*q)               \
            *p++ = (WORD) * q++; \
        *p++ = 0;                \
    } while (0)
#define AL()                    \
    do                          \
    {                           \
        if (((ULONG_PTR)p) & 2) \
            p++;                \
    } while (0)
#define CTRL(st, ex, xx, yy, ww, hh, iid, cls, txt)  \
    do                                               \
    {                                                \
        AL();                                        \
        DLGITEMTEMPLATE *it = (DLGITEMTEMPLATE *)p;  \
        it->style = (st);                            \
        it->dwExtendedStyle = (ex);                  \
        it->x = (xx);                                \
        it->y = (yy);                                \
        it->cx = (ww);                               \
        it->cy = (hh);                               \
        it->id = (iid);                              \
        p += sizeof(DLGITEMTEMPLATE) / sizeof(WORD); \
        *p++ = 0xFFFF;                               \
        *p++ = (cls);                                \
        WSTR(txt);                                   \
        *p++ = 0;                                    \
    } while (0)
    DLGTEMPLATE *d = (DLGTEMPLATE *)p;
    d->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    d->dwExtendedStyle = 0;
    d->cdit = 10;
    d->x = 0;
    d->y = 0;
    d->cx = 230;
    d->cy = 185;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);
    *p++ = 0;
    *p++ = 0;
    WSTR(L"Personalizar Tray Icon");
    *p++ = 9;
    WSTR(L"Segoe UI");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 10, 210, 12, IDC_TRAY_CPU, 0x0080, L"Mostrar CPU no tooltip");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 27, 210, 12, IDC_TRAY_RAM, 0x0080, L"Mostrar RAM no tooltip");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 44, 210, 12, IDC_TRAY_TEMP, 0x0080, L"Mostrar temperatura");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 61, 210, 12, IDC_TRAY_NET, 0x0080, L"Mostrar rede");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 78, 210, 12, IDC_TRAY_DISK, 0x0080, L"Mostrar disco I/O");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 95, 210, 12, IDC_TRAY_UPTIME, 0x0080, L"Mostrar uptime (tooltip)");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 112, 210, 12, IDC_TRAY_CLICK, 0x0080, L"Clique esquerdo restaura janela");
    CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 8, 129, 210, 12, IDC_TRAY_DBLCLICK, 0x0080, L"Duplo clique restaura janela");
    CTRL(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 55, 151, 55, 14, IDOK, 0x0080, L"OK");
    CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 120, 151, 55, 14, IDCANCEL, 0x0080, L"Cancelar");
    DialogBoxIndirectA(GetModuleHandle(NULL), (LPDLGTEMPLATE)b, hwndPai, DialogoConfigTrayProc);
#undef WSTR
#undef AL
#undef CTRL
}

/* ------------------------------------------------------------------------- */
/* Balloon & WMI                                                             */
/* ------------------------------------------------------------------------- */

static void EnviarBalloon(const char *titulo, const char *msg)
{
    ULONGLONG agora = GetTickCount64();

    if (agora - ultimoBalloonTick <
        (ULONGLONG)BALLOON_COOLDOWN_SEGUNDOS * 1000ULL)
        return;

    if (!trayIconAtivo)
    {
        Shell_NotifyIconA(NIM_ADD, &nid);
        trayIconAtivo = 1;
    }

    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    strncpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), titulo, _TRUNCATE);
    strncpy_s(nid.szInfo, sizeof(nid.szInfo), msg, _TRUNCATE);
    nid.uTimeout = 5000;

    Shell_NotifyIconA(NIM_MODIFY, &nid);

    nid.uFlags &= ~NIF_INFO;
    ultimoBalloonTick = agora;
}

static void VerificarAlertasBalloon(void)
{
    if (!alertaGlobalAtivo)
        return;

    {
        char msgBalloon[256];
        char partes[3][64];
        int n = 0;

        partes[0][0] = partes[1][0] = partes[2][0] = '\0';

        if (ultimoCpuPercent > limiteCpuPercent)
            snprintf(partes[n++], 64, "CPU %.0f%%", ultimoCpuPercent);
        if (ultimoRamPercent > limiteRamPercent)
            snprintf(partes[n++], 64, "RAM %.0f%%", ultimoRamPercent);

        if (n == 0)
            return;

        msgBalloon[0] = '\0';
        {
            int i;
            for (i = 0; i < n; i++)
            {
                if (i > 0)
                    strncat_s(msgBalloon, sizeof(msgBalloon), " | ", _TRUNCATE);
                strncat_s(msgBalloon, sizeof(msgBalloon), partes[i], _TRUNCATE);
            }
        }

        EnviarBalloon("WinMon — Alerta de Recursos", msgBalloon);
    }
}

static IWbemLocator *g_pWbemLoc = NULL;
static IWbemServices *g_pWbemSvc = NULL;
static int g_wmiPronto = 0;

static void InicializarTemperaturaCPU(void)
{
    HRESULT hr;

    numZonasTemp = 0;
    g_wmiPronto = 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return;

    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL, EOAC_NONE, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE)
        return;

    hr = CoCreateInstance(
        &CLSID_WbemLocator, NULL,
        CLSCTX_INPROC_SERVER,
        &IID_IWbemLocator,
        (LPVOID *)&g_pWbemLoc);
    if (FAILED(hr) || !g_pWbemLoc)
        return;

    {
        BSTR bstrNs = SysAllocString(L"ROOT\\WMI");
        if (!bstrNs)
            return;

        hr = g_pWbemLoc->lpVtbl->ConnectServer(
            g_pWbemLoc, bstrNs,
            NULL, NULL, NULL, 0, NULL, NULL,
            &g_pWbemSvc);

        SysFreeString(bstrNs);
    }

    if (FAILED(hr) || !g_pWbemSvc)
        return;

    hr = CoSetProxyBlanket(
        (IUnknown *)g_pWbemSvc,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        NULL,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        NULL,
        EOAC_NONE);

    if (FAILED(hr))
        return;

    g_wmiPronto = 1;
}

static void LerTemperaturaCPU(char *buffer, size_t size, size_t *offset)
{
    HRESULT hr;
    BSTR bstrQuery = NULL;
    BSTR bstrWql = NULL;
    IEnumWbemClassObject *pEnum = NULL;
    IWbemClassObject *pObj = NULL;
    ULONG retorno;
    int algumValido = 0;

    if (!g_wmiPronto || !g_pWbemSvc)
        return;

    numZonasTemp = 0;

    bstrWql = SysAllocString(L"WQL");
    bstrQuery = SysAllocString(
        L"SELECT InstanceName, CurrentTemperature "
        L"FROM MSAcpi_ThermalZoneTemperature");

    if (!bstrWql || !bstrQuery)
        goto cleanup;

    hr = g_pWbemSvc->lpVtbl->ExecQuery(
        g_pWbemSvc, bstrWql, bstrQuery,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, &pEnum);

    if (FAILED(hr) || !pEnum)
        goto cleanup;

    while (numZonasTemp < MAX_TEMP_ZONAS)
    {
        VARIANT vtTemp, vtName;
        double celsius;

        hr = pEnum->lpVtbl->Next(pEnum,
                                 WBEM_INFINITE, 1, &pObj, &retorno);
        if (FAILED(hr) || retorno == 0)
            break;

        VariantInit(&vtTemp);
        VariantInit(&vtName);

        hr = pObj->lpVtbl->Get(pObj, L"CurrentTemperature", 0, &vtTemp, NULL, NULL);
        if (SUCCEEDED(hr) && vtTemp.vt == VT_I4)
            celsius = (vtTemp.lVal / 10.0) - 273.15;
        else if (SUCCEEDED(hr) && vtTemp.vt == VT_R8)
            celsius = (vtTemp.dblVal / 10.0) - 273.15;
        else
        {
            VariantClear(&vtTemp);
            VariantClear(&vtName);
            pObj->lpVtbl->Release(pObj);
            continue;
        }

        if (celsius < -10.0 || celsius > 150.0)
        {
            VariantClear(&vtTemp);
            VariantClear(&vtName);
            pObj->lpVtbl->Release(pObj);
            continue;
        }

        tempAtual[numZonasTemp] = celsius;
        algumValido = 1;

        hr = pObj->lpVtbl->Get(pObj, L"InstanceName", 0, &vtName, NULL, NULL);
        if (SUCCEEDED(hr) && vtName.vt == VT_BSTR && vtName.bstrVal)
        {
            WideCharToMultiByte(CP_ACP, 0, vtName.bstrVal, -1,
                                tempNome[numZonasTemp],
                                sizeof(tempNome[numZonasTemp]), NULL, NULL);
        }
        else
        {
            snprintf(tempNome[numZonasTemp], sizeof(tempNome[numZonasTemp]),
                     "Zona %d", numZonasTemp);
        }

        VariantClear(&vtTemp);
        VariantClear(&vtName);
        pObj->lpVtbl->Release(pObj);
        pObj = NULL;

        numZonasTemp++;
    }

cleanup:
    if (pObj)
        pObj->lpVtbl->Release(pObj);
    if (pEnum)
        pEnum->lpVtbl->Release(pEnum);
    SysFreeString(bstrQuery);
    SysFreeString(bstrWql);

    if (!algumValido || numZonasTemp == 0)
        return;

    if (buffer && size > 0 && offset)
    {
        int i;
        AppendFormat(buffer, size, offset,
                     "=== [ TEMPERATURA CPU (WMI) ] ===\r\n");

        for (i = 0; i < numZonasTemp; i++)
        {
            const char *label = tempNome[i];
            const char *barra = strrchr(label, '\\');
            if (barra)
                label = barra + 1;

            AppendFormat(buffer, size, offset,
                         "%-14s %.1f \xB0\x43\r\n", label, tempAtual[i]);
        }

        AppendFormat(buffer, size, offset, "\r\n");
    }
}

static void TerminarWMI(void)
{
    if (g_pWbemSvc)
    {
        g_pWbemSvc->lpVtbl->Release(g_pWbemSvc);
        g_pWbemSvc = NULL;
    }
    if (g_pWbemLoc)
    {
        g_pWbemLoc->lpVtbl->Release(g_pWbemLoc);
        g_pWbemLoc = NULL;
    }
    CoUninitialize();
    g_wmiPronto = 0;
}

/* ------------------------------------------------------------------------- */
/* Overlay Alargado                                                          */
/* ------------------------------------------------------------------------- */

#define OV_PAD 6
#define OV_BAR_H 5
#define OV_MARGIN_SCR 12
#define OV_SEP RGB(45, 47, 52)

static BOOL ovArrastar = FALSE;
static POINT ovPtArrastar = {0, 0};

static void InicializarPerfisOverlay(void)
{
    strncpy_s(ovPerfis[0].nome, OV_NOME_MAX, "Gaming", _TRUNCATE);
    ovPerfis[0].fontePt = 10;
    strncpy_s(ovPerfis[0].fonteNome, sizeof(ovPerfis[0].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[0].opacidade = 200;
    ovPerfis[0].mostrarTemp = 0;
    ovPerfis[0].mostrarDisco = 0;
    ovPerfis[0].mostrarNet = 1;
    ovPerfis[0].clickThrough = 1;
    ovPerfis[0].corBg = RGB(14, 14, 16);
    ovPerfis[0].corBorda = RGB(60, 63, 70);
    ovPerfis[0].corTextoLabel = RGB(100, 190, 255);
    ovPerfis[0].corTextoValor = RGB(230, 232, 235);
    ovPerfis[0].corBarraCpu = RGB(100, 190, 255);
    ovPerfis[0].corBarraRam = RGB(110, 220, 140);
    ovPerfis[0].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[0].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[0].corBarraNet = RGB(35, 145, 95);
    ovPerfis[0].corSeparador = RGB(45, 47, 52);
    ovPerfis[0].mostrarBarras = 1;
    ovPerfis[0].mostrarSeparadores = 1;
    ovPerfis[0].espessuraBorda = 1;

    strncpy_s(ovPerfis[1].nome, OV_NOME_MAX, "Trabalho", _TRUNCATE);
    ovPerfis[1].fontePt = 13;
    strncpy_s(ovPerfis[1].fonteNome, sizeof(ovPerfis[1].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[1].opacidade = 220;
    ovPerfis[1].mostrarTemp = 0;
    ovPerfis[1].mostrarDisco = 1;
    ovPerfis[1].mostrarNet = 1;
    ovPerfis[1].clickThrough = 0;
    ovPerfis[1].corBg = RGB(20, 22, 26);
    ovPerfis[1].corBorda = RGB(70, 75, 85);
    ovPerfis[1].corTextoLabel = RGB(120, 200, 255);
    ovPerfis[1].corTextoValor = RGB(240, 242, 245);
    ovPerfis[1].corBarraCpu = RGB(120, 200, 255);
    ovPerfis[1].corBarraRam = RGB(130, 230, 150);
    ovPerfis[1].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[1].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[1].corBarraNet = RGB(35, 145, 95);
    ovPerfis[1].corSeparador = RGB(45, 47, 52);
    ovPerfis[1].mostrarBarras = 1;
    ovPerfis[1].mostrarSeparadores = 1;
    ovPerfis[1].espessuraBorda = 1;

    strncpy_s(ovPerfis[2].nome, OV_NOME_MAX, "Completo", _TRUNCATE);
    ovPerfis[2].fontePt = 13;
    strncpy_s(ovPerfis[2].fonteNome, sizeof(ovPerfis[2].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[2].opacidade = 210;
    ovPerfis[2].mostrarTemp = 1;
    ovPerfis[2].mostrarDisco = 1;
    ovPerfis[2].mostrarNet = 1;
    ovPerfis[2].clickThrough = 0;
    ovPerfis[2].corBg = RGB(14, 14, 16);
    ovPerfis[2].corBorda = RGB(60, 63, 70);
    ovPerfis[2].corTextoLabel = RGB(100, 190, 255);
    ovPerfis[2].corTextoValor = RGB(230, 232, 235);
    ovPerfis[2].corBarraCpu = RGB(100, 190, 255);
    ovPerfis[2].corBarraRam = RGB(110, 220, 140);
    ovPerfis[2].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[2].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[2].corBarraNet = RGB(35, 145, 95);
    ovPerfis[2].corSeparador = RGB(45, 47, 52);
    ovPerfis[2].mostrarBarras = 1;
    ovPerfis[2].mostrarSeparadores = 1;
    ovPerfis[2].espessuraBorda = 1;

    ovNumPerfis = 3;
    ovPerfilActivo = 2;
}

static void GravarPerfisOverlay(void)
{
    HKEY hkBase;
    int i;

    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\WinMon\\Overlay", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hkBase, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)ovPerfilActivo;
        RegSetValueExA(hkBase, "PerfilActivo", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovNumPerfis;
        RegSetValueExA(hkBase, "NumPerfis", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegCloseKey(hkBase);
    }

    for (i = 0; i < ovNumPerfis; i++)
    {
        char subchave[64];
        HKEY hk;
        snprintf(subchave, sizeof(subchave),
                 "Software\\WinMon\\Overlay\\Perfis\\%d", i);

        if (RegCreateKeyExA(HKEY_CURRENT_USER, subchave, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
            continue;

        DWORD v;
        RegSetValueExA(hk, "Nome", 0, REG_SZ,
                       (BYTE *)ovPerfis[i].nome, (DWORD)(strlen(ovPerfis[i].nome) + 1));
        v = (DWORD)ovPerfis[i].fontePt;
        RegSetValueExA(hk, "FontePt", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegSetValueExA(hk, "FonteNome", 0, REG_SZ,
                       (BYTE *)ovPerfis[i].fonteNome, (DWORD)(strlen(ovPerfis[i].fonteNome) + 1));
        v = (DWORD)ovPerfis[i].opacidade;
        RegSetValueExA(hk, "Opacidade", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarTemp;
        RegSetValueExA(hk, "MostrarTemp", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarDisco;
        RegSetValueExA(hk, "MostrarDisco", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarNet;
        RegSetValueExA(hk, "MostrarNet", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].clickThrough;
        RegSetValueExA(hk, "ClickThrough", 0, REG_DWORD, (BYTE *)&v, sizeof(v));

        v = (DWORD)ovPerfis[i].corBg;
        RegSetValueExA(hk, "CorBg", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBorda;
        RegSetValueExA(hk, "CorBorda", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corTextoLabel;
        RegSetValueExA(hk, "CorTextoLabel", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corTextoValor;
        RegSetValueExA(hk, "CorTextoValor", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraCpu;
        RegSetValueExA(hk, "CorBarraCpu", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraRam;
        RegSetValueExA(hk, "CorBarraRam", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraTemp;
        RegSetValueExA(hk, "CorBarraTemp", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraDisco;
        RegSetValueExA(hk, "CorBarraDisco", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraNet;
        RegSetValueExA(hk, "CorBarraNet", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corSeparador;
        RegSetValueExA(hk, "CorSeparador", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarBarras;
        RegSetValueExA(hk, "MostrarBarras", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarSeparadores;
        RegSetValueExA(hk, "MostrarSeparadores", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].espessuraBorda;
        RegSetValueExA(hk, "EspessuraBorda", 0, REG_DWORD, (BYTE *)&v, sizeof(v));

        RegCloseKey(hk);
    }
}

static void CarregarPerfisOverlay(void)
{
    HKEY hkBase;
    DWORD v, sz;
    int i, numGuardados;

    InicializarPerfisOverlay();

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\WinMon\\Overlay", 0, KEY_QUERY_VALUE, &hkBase) != ERROR_SUCCESS)
        return;

    sz = sizeof(DWORD);
    numGuardados = 3;
    if (RegQueryValueExA(hkBase, "NumPerfis", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
        numGuardados = (int)v;
    if (numGuardados < 3)
        numGuardados = 3;
    if (numGuardados > OV_MAX_PERFIS)
        numGuardados = OV_MAX_PERFIS;

    sz = sizeof(DWORD);
    if (RegQueryValueExA(hkBase, "PerfilActivo", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
        ovPerfilActivo = (int)v;
    RegCloseKey(hkBase);

    for (i = 0; i < numGuardados; i++)
    {
        char subchave[64];
        HKEY hk;
        snprintf(subchave, sizeof(subchave),
                 "Software\\WinMon\\Overlay\\Perfis\\%d", i);

        if (RegOpenKeyExA(HKEY_CURRENT_USER, subchave, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
            continue;

        sz = sizeof(ovPerfis[i].nome);
        RegQueryValueExA(hk, "Nome", NULL, NULL, (BYTE *)ovPerfis[i].nome, &sz);
        sz = sizeof(ovPerfis[i].fonteNome);
        RegQueryValueExA(hk, "FonteNome", NULL, NULL, (BYTE *)ovPerfis[i].fonteNome, &sz);
        sz = sizeof(DWORD);
        if (RegQueryValueExA(hk, "FontePt", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].fontePt = (int)v;
        if (RegQueryValueExA(hk, "Opacidade", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].opacidade = (BYTE)v;
        if (RegQueryValueExA(hk, "MostrarTemp", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarTemp = (int)v;
        if (RegQueryValueExA(hk, "MostrarDisco", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarDisco = (int)v;
        if (RegQueryValueExA(hk, "MostrarNet", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarNet = (int)v;
        if (RegQueryValueExA(hk, "ClickThrough", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].clickThrough = (int)v;

        if (RegQueryValueExA(hk, "CorBg", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBg = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBorda", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBorda = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorTextoLabel", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corTextoLabel = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorTextoValor", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corTextoValor = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraCpu", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraCpu = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraRam", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraRam = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraTemp", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraTemp = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraDisco", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraDisco = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraNet", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraNet = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorSeparador", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corSeparador = (COLORREF)v;
        if (RegQueryValueExA(hk, "MostrarBarras", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarBarras = (int)v;
        if (RegQueryValueExA(hk, "MostrarSeparadores", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarSeparadores = (int)v;
        if (RegQueryValueExA(hk, "EspessuraBorda", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].espessuraBorda = (int)v;

        if (ovPerfis[i].fontePt < OV_FONT_MIN)
            ovPerfis[i].fontePt = OV_FONT_MIN;
        if (ovPerfis[i].fontePt > OV_FONT_MAX)
            ovPerfis[i].fontePt = OV_FONT_MAX;
        if (ovPerfis[i].opacidade < 30)
            ovPerfis[i].opacidade = 30;
        if (ovPerfis[i].fonteNome[0] == '\0')
            strncpy_s(ovPerfis[i].fonteNome, sizeof(ovPerfis[i].fonteNome), "Consolas", _TRUNCATE);

        RegCloseKey(hk);
    }

    ovNumPerfis = numGuardados;
    if (ovPerfilActivo < 0 || ovPerfilActivo >= ovNumPerfis)
        ovPerfilActivo = 0;
}

static void AtivarPerfil(int idx)
{
    if (idx < 0 || idx >= ovNumPerfis)
        return;
    ovPerfilActivo = idx;

    if (hOverlay && overlayAtivo)
    {
        SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
        AplicarClickThrough();
        RecalcOverlaySize();
        InvalidateRect(hOverlay, NULL, FALSE);
    }
}

static void AplicarClickThrough(void)
{
    if (!hOverlay)
        return;

    LONG_PTR ex = GetWindowLongPtr(hOverlay, GWL_EXSTYLE);

    if (OV_CLICKTHRU)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;

    SetWindowLongPtr(hOverlay, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
}

static void SnapOverlayAoCanto(void)
{
    RECT wa = {0}, wr = {0};
    int wx, wy;
    int snapX = -1, snapY = -1;
    int distMin;

    if (!hOverlay)
        return;

    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    GetWindowRect(hOverlay, &wr);

    wx = wr.left;
    wy = wr.top;

    struct
    {
        int x;
        int y;
    } cantos[4] = {
        {wa.left, wa.top},
        {wa.right - ovTotalW, wa.top},
        {wa.left, wa.bottom - ovTotalH},
        {wa.right - ovTotalW, wa.bottom - ovTotalH}};

    distMin = OV_SNAP_DIST + 1;

    {
        int i;
        for (i = 0; i < 4; i++)
        {
            int dx = wx - cantos[i].x;
            int dy = wy - cantos[i].y;
            int dist = (int)sqrt((double)(dx * dx + dy * dy));
            if (dist < distMin)
            {
                distMin = dist;
                snapX = cantos[i].x;
                snapY = cantos[i].y;
            }
        }
    }

    if (snapX >= 0)
    {
        SetWindowPos(hOverlay, HWND_TOPMOST,
                     snapX, snapY, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void RecalcOverlaySize(void)
{
    int charW = (OV_FONTE_PT * 8) / 13;
    int lineH = (OV_FONTE_PT * 20) / 13;
    int ncols, nlinhas;

    if (charW < 5)
        charW = 5;
    if (lineH < 12)
        lineH = 12;

    ovRowH = lineH;
    ovColW = charW * 11 + OV_PAD * 2;
    if (ovColW < 80)
        ovColW = 80;

    ncols = 2;
    if (OV_NET)
        ncols++;
    if (OV_TEMP)
        ncols++;
    if (OV_DISCO)
        ncols++;

    nlinhas = 3;

    ovTotalW = OV_PAD + ncols * ovColW + OV_PAD;
    ovTotalH = OV_PAD + nlinhas * ovRowH + OV_BAR_H + OV_PAD * 2;

    if (hOverlay && overlayAtivo)
    {
        RECT wr;
        GetWindowRect(hOverlay, &wr);
        SetWindowPos(hOverlay, HWND_TOPMOST,
                     wr.left, wr.top, ovTotalW, ovTotalH,
                     SWP_NOACTIVATE);
    }
}

static void DrawMiniBar(HDC hdc, int x, int y, int w, double pct,
                        COLORREF corFill, COLORREF corBg)
{
    RECT bar = {x, y, x + w, y + OV_BAR_H};
    HBRUSH bg = CreateSolidBrush(corBg);
    FillRect(hdc, &bar, bg);
    DeleteObject(bg);

    if (pct > 0.0)
    {
        int fill = (int)(w * (pct / 100.0));
        if (fill > w)
            fill = w;
        if (fill > 0)
        {
            RECT filled = {x, y, x + fill, y + OV_BAR_H};
            HBRUSH fg = CreateSolidBrush(corFill);
            FillRect(hdc, &filled, fg);
            DeleteObject(fg);
        }
    }
}

typedef struct
{
    const char *label;
    COLORREF corLabel;
    const char *valorStr;
    const char *subStr;
    double barPct;
    COLORREF corBar;
} OvColuna;

static void DrawOvColuna(HDC hdc, int x, int y,
                         HFONT fLabel, HFONT fValor, HFONT fSub,
                         const OvColuna *c)
{
    int lh = ovRowH;
    int cw = ovColW;
    RECT tr;

    SelectObject(hdc, fLabel);
    SetTextColor(hdc, c->corLabel);
    tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
    DrawTextA(hdc, c->label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    SelectObject(hdc, fValor);
    SetTextColor(hdc, ovPerfis[ovPerfilActivo].corTextoValor);
    tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
    DrawTextA(hdc, c->valorStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    if (c->barPct >= 0.0 && ovPerfis[ovPerfilActivo].mostrarBarras)
        DrawMiniBar(hdc, x + OV_PAD, y, cw - OV_PAD * 2,
                    c->barPct, c->corBar, RGB(40, 42, 46));
    y += OV_BAR_H + 2;

    if (c->subStr && c->subStr[0])
    {
        SelectObject(hdc, fSub);
        SetTextColor(hdc, RGB(150, 153, 158));
        tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
        DrawTextA(hdc, c->subStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT uMsg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bgBrush = CreateSolidBrush(ovPerfis[ovPerfilActivo].corBg);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        COLORREF corBordaAtual = (overlayAlertaVisivel ? RGB(220, 45, 45) : ovPerfis[ovPerfilActivo].corBorda);
        int espBordaAtual = overlayAlertaVisivel
                                ? max(2, ovPerfis[ovPerfilActivo].espessuraBorda + 1)
                                : ovPerfis[ovPerfilActivo].espessuraBorda;
        HPEN penBorda = CreatePen(PS_SOLID, espBordaAtual, corBordaAtual);
        HPEN penVelho = (HPEN)SelectObject(hdc, penBorda);
        MoveToEx(hdc, 0, 0, NULL);
        LineTo(hdc, rc.right - 1, 0);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        LineTo(hdc, 0, rc.bottom - 1);
        LineTo(hdc, 0, 0);
        SelectObject(hdc, penVelho);
        DeleteObject(penBorda);

        if (OV_CLICKTHRU)
        {
            HPEN penCT = CreatePen(PS_SOLID, 2, RGB(80, 160, 80));
            HPEN pv = (HPEN)SelectObject(hdc, penCT);
            MoveToEx(hdc, 1, 1, NULL);
            LineTo(hdc, rc.right - 2, 1);
            LineTo(hdc, rc.right - 2, rc.bottom - 2);
            LineTo(hdc, 1, rc.bottom - 2);
            LineTo(hdc, 1, 1);
            SelectObject(hdc, pv);
            DeleteObject(penCT);
        }

        SetBkMode(hdc, TRANSPARENT);

        HFONT fLabel = CreateFontA(
            OV_FONTE_PT - 1, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);
        HFONT fValor = CreateFontA(
            OV_FONTE_PT + 1, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);
        HFONT fSub = CreateFontA(
            max(OV_FONTE_PT - 3, OV_FONT_MIN), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);

        HFONT fOrig = (HFONT)SelectObject(hdc, fLabel);

        int cx = OV_PAD;
        int cy = OV_PAD;
        OvColuna col;
        char vBuf[64], sBuf[64], dBuf[32], uBuf[32];
        HPEN penSep = CreatePen(PS_SOLID, 1, ovPerfis[ovPerfilActivo].corSeparador);

        snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoCpuPercent);
        if (numZonasTemp > 0)
        {
            double t = 0.0;
            int i;
            for (i = 0; i < numZonasTemp; i++)
                t += tempAtual[i];
            snprintf(sBuf, sizeof(sBuf), "%.0f\xB0"
                                         "C",
                     t / numZonasTemp);
        }
        else
        {
            snprintf(sBuf, sizeof(sBuf), "%d cores", numNucleosMonitorizados);
        }
        col = (OvColuna){"CPU", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                         ultimoCpuPercent, ovPerfis[ovPerfilActivo].corBarraCpu};
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        SelectObject(hdc, penSep);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, cx, rc.bottom - OV_PAD);

        {
            MEMORYSTATUSEX ms;
            ms.dwLength = sizeof(ms);
            DWORDLONG usedMB = 0, totalMB = 0;
            if (GlobalMemoryStatusEx(&ms))
            {
                totalMB = ms.ullTotalPhys / (1024 * 1024);
                usedMB = totalMB - ms.ullAvailPhys / (1024 * 1024);
            }
            snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoRamPercent);
            snprintf(sBuf, sizeof(sBuf), "%llu/%llu MB",
                     (unsigned long long)usedMB, (unsigned long long)totalMB);
        }
        col = (OvColuna){"RAM", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                         ultimoRamPercent, ovPerfis[ovPerfilActivo].corBarraRam};
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        if (OV_NET)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            FormatarBytes(ultimoNetDown, dBuf, sizeof(dBuf));
            FormatarBytes(ultimoNetUp, uBuf, sizeof(uBuf));
            snprintf(vBuf, sizeof(vBuf), "\x19%s/s", dBuf);
            snprintf(sBuf, sizeof(sBuf), "\x18%s/s", uBuf);
            col = (OvColuna){"NET", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, -1.0, ovPerfis[ovPerfilActivo].corBarraNet};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_TEMP)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            if (numZonasTemp > 0)
            {
                double tMax = 0.0, tMedia = 0.0;
                int i;
                for (i = 0; i < numZonasTemp; i++)
                {
                    tMedia += tempAtual[i];
                    if (tempAtual[i] > tMax)
                        tMax = tempAtual[i];
                }
                tMedia /= numZonasTemp;
                snprintf(vBuf, sizeof(vBuf), "%.0f\xB0"
                                             "C",
                         tMedia);
                snprintf(sBuf, sizeof(sBuf), "max %.0f\xB0"
                                             "C",
                         tMax);
                double bPct = (tMax > 0.0) ? (tMedia / 110.0) * 100.0 : 0.0;
                COLORREF cT = ovPerfis[ovPerfilActivo].corBarraTemp;
                col = (OvColuna){"TEMP", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, bPct, cT};
            }
            else
            {
                col = (OvColuna){"TEMP", ovPerfis[ovPerfilActivo].corTextoLabel, "N/A", "WMI off", -1.0, 0};
            }
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_DISCO)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            FormatarBytes(ultimoDiskRead, dBuf, sizeof(dBuf));
            FormatarBytes(ultimoDiskWrite, uBuf, sizeof(uBuf));
            snprintf(vBuf, sizeof(vBuf), "R %s/s", dBuf);
            snprintf(sBuf, sizeof(sBuf), "W %s/s", uBuf);
            col = (OvColuna){"DISCO", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, -1.0, ovPerfis[ovPerfilActivo].corBarraDisco};
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

    case WM_TIMER:
        if (wParam == OVERLAY_ALERT_TIMER)
        {
            ULONGLONG agora = GetTickCount64();
            if (agora >= overlayAlertaAteTick)
            {
                overlayAlertaVisivel = 0;
                KillTimer(hwnd, OVERLAY_ALERT_TIMER);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else
            {
                overlayAlertaVisivel = !overlayAlertaVisivel;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        if (!OV_CLICKTHRU)
        {
            ovArrastar = TRUE;
            ovPtArrastar.x = LOWORD(lParam);
            ovPtArrastar.y = HIWORD(lParam);
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (ovArrastar)
        {
            POINT pt;
            RECT wr;
            GetCursorPos(&pt);
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, NULL,
                         wr.left + (pt.x - wr.left - ovPtArrastar.x),
                         wr.top + (pt.y - wr.top - ovPtArrastar.y),
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (ovArrastar)
        {
            ovArrastar = FALSE;
            ReleaseCapture();
            SnapOverlayAoCanto();
        }
        return 0;

    case WM_RBUTTONUP:
        MostrarDialogoConfigOverlay(hwnd);
        return 0;

    case WM_LBUTTONDBLCLK:
        ToggleOverlay();
        return 0;

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        OV_FONTE_PT += (delta > 0) ? 1 : -1;
        if (OV_FONTE_PT < OV_FONT_MIN)
            OV_FONTE_PT = OV_FONT_MIN;
        if (OV_FONTE_PT > OV_FONT_MAX)
            OV_FONTE_PT = OV_FONT_MAX;
        RecalcOverlaySize();
        InvalidateRect(hwnd, NULL, FALSE);
        GravarPerfisOverlay();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, OVERLAY_ALERT_TIMER);
        overlayAlertaVisivel = 0;
        hOverlay = NULL;
        overlayAtivo = 0;
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define IDC_OV_PERFIL_CB 4001
#define IDC_OV_NOME 4002
#define IDC_OV_FONTSIZE 4003
#define IDC_OV_FONTNAME 4004
#define IDC_OV_OPACITY 4005
#define IDC_OV_TEMP 4006
#define IDC_OV_DISCO 4007
#define IDC_OV_NET 4008
#define IDC_OV_CLICKTHRU 4009
#define IDC_OV_BTN_NOVO 4010
#define IDC_OV_BTN_APAGAR 4011
#define IDC_OV_BTN_COR_BG 4012
#define IDC_OV_BTN_COR_BORDA 4013
#define IDC_OV_BTN_COR_LABEL 4014
#define IDC_OV_BTN_COR_VALOR 4015
#define IDC_OV_BTN_COR_CPU 4016
#define IDC_OV_BTN_COR_RAM 4017
#define IDC_OV_BTN_COR_TEMP 4018
#define IDC_OV_BTN_COR_DISCO 4019
#define IDC_OV_BTN_COR_NET 4020
#define IDC_OV_BTN_COR_SEP 4021
#define IDC_OV_BARRAS 4022
#define IDC_OV_SEPARADORES 4023
#define IDC_OV_BORDERW 4024

static INT_PTR CALLBACK DialogoConfigOverlayProc(HWND hDlg, UINT uMsg,
                                                 WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        char buf[32];
        int i;
        HWND hCb = GetDlgItem(hDlg, IDC_OV_PERFIL_CB);

        SendMessageA(hCb, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < ovNumPerfis; i++)
            SendMessageA(hCb, CB_ADDSTRING, 0, (LPARAM)ovPerfis[i].nome);
        SendMessageA(hCb, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);

        SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[ovPerfilActivo].nome);
        snprintf(buf, sizeof(buf), "%d", OV_FONTE_PT);
        SetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf);
        SetDlgItemTextA(hDlg, IDC_OV_FONTNAME, OV_FONTE_NOME);
        snprintf(buf, sizeof(buf), "%d", (int)OV_OPACIDADE);
        SetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf);
        CheckDlgButton(hDlg, IDC_OV_TEMP, OV_TEMP ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_DISCO, OV_DISCO ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_NET, OV_NET ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_CLICKTHRU, OV_CLICKTHRU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_BARRAS, ovPerfis[ovPerfilActivo].mostrarBarras ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_SEPARADORES, ovPerfis[ovPerfilActivo].mostrarSeparadores ? BST_CHECKED : BST_UNCHECKED);
        snprintf(buf, sizeof(buf), "%d", ovPerfis[ovPerfilActivo].espessuraBorda);
        SetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf);

        EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), ovPerfilActivo >= 3 ? TRUE : FALSE);
        return TRUE;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);

        if (id == IDC_OV_PERFIL_CB && notif == CBN_SELCHANGE)
        {
            int sel = (int)SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < ovNumPerfis)
            {
                char buf[32];
                ovPerfilActivo = sel;
                SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[sel].nome);
                snprintf(buf, sizeof(buf), "%d", ovPerfis[sel].fontePt);
                SetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf);
                SetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovPerfis[sel].fonteNome);
                snprintf(buf, sizeof(buf), "%d", (int)ovPerfis[sel].opacidade);
                SetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf);
                CheckDlgButton(hDlg, IDC_OV_TEMP, ovPerfis[sel].mostrarTemp ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_DISCO, ovPerfis[sel].mostrarDisco ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_NET, ovPerfis[sel].mostrarNet ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_CLICKTHRU, ovPerfis[sel].clickThrough ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_BARRAS, ovPerfis[sel].mostrarBarras ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_SEPARADORES, ovPerfis[sel].mostrarSeparadores ? BST_CHECKED : BST_UNCHECKED);
                snprintf(buf, sizeof(buf), "%d", ovPerfis[sel].espessuraBorda);
                SetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf);
                EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), sel >= 3 ? TRUE : FALSE);
            }
            return TRUE;
        }

        if (id == IDC_OV_BTN_COR_BG)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBg))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_BORDA)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBorda))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_LABEL)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corTextoLabel))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_VALOR)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corTextoValor))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_CPU)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraCpu))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_RAM)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraRam))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }

        if (id == IDC_OV_BTN_COR_TEMP)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraTemp))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_DISCO)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraDisco))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_NET)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraNet))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_SEP)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corSeparador))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }

        if (id == IDC_OV_BTN_NOVO)
        {
            if (ovNumPerfis >= OV_MAX_PERFIS)
            {
                MessageBoxA(hDlg, "Limite de perfis atingido.", "WinMon", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            ovPerfis[ovNumPerfis] = ovPerfis[ovPerfilActivo];
            snprintf(ovPerfis[ovNumPerfis].nome, OV_NOME_MAX, "Perfil %d", ovNumPerfis + 1);
            ovNumPerfis++;
            ovPerfilActivo = ovNumPerfis - 1;

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_ADDSTRING, 0, (LPARAM)ovPerfis[ovPerfilActivo].nome);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);
            SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[ovPerfilActivo].nome);
            EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), TRUE);
            return TRUE;
        }

        if (id == IDC_OV_BTN_APAGAR && ovPerfilActivo >= 3)
        {
            int i, idx = ovPerfilActivo;
            for (i = idx; i < ovNumPerfis - 1; i++)
                ovPerfis[i] = ovPerfis[i + 1];
            ovNumPerfis--;
            if (ovPerfilActivo >= ovNumPerfis)
                ovPerfilActivo = ovNumPerfis - 1;

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_RESETCONTENT, 0, 0);
            for (i = 0; i < ovNumPerfis; i++)
                SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_ADDSTRING, 0, (LPARAM)ovPerfis[i].nome);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), ovPerfilActivo >= 3 ? TRUE : FALSE);
            return TRUE;
        }

        if (id == IDOK)
        {
            char buf[64];
            int sel = ovPerfilActivo;

            GetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[sel].nome, OV_NOME_MAX);
            if (ovPerfis[sel].nome[0] == '\0')
                snprintf(ovPerfis[sel].nome, OV_NOME_MAX, "Perfil %d", sel + 1);

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_DELETESTRING, (WPARAM)sel, 0);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_INSERTSTRING, (WPARAM)sel, (LPARAM)ovPerfis[sel].nome);

            GetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf, sizeof(buf));
            {
                int fs = atoi(buf);
                if (fs >= OV_FONT_MIN && fs <= OV_FONT_MAX)
                    ovPerfis[sel].fontePt = fs;
            }

            GetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovPerfis[sel].fonteNome, sizeof(ovPerfis[sel].fonteNome));
            if (ovPerfis[sel].fonteNome[0] == '\0')
                strncpy_s(ovPerfis[sel].fonteNome, sizeof(ovPerfis[sel].fonteNome), "Consolas", _TRUNCATE);

            GetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf, sizeof(buf));
            {
                int op = atoi(buf);
                if (op < 30)
                    op = 30;
                if (op > 255)
                    op = 255;
                ovPerfis[sel].opacidade = (BYTE)op;
            }

            ovPerfis[sel].mostrarTemp = (IsDlgButtonChecked(hDlg, IDC_OV_TEMP) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarDisco = (IsDlgButtonChecked(hDlg, IDC_OV_DISCO) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarNet = (IsDlgButtonChecked(hDlg, IDC_OV_NET) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].clickThrough = (IsDlgButtonChecked(hDlg, IDC_OV_CLICKTHRU) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarBarras = (IsDlgButtonChecked(hDlg, IDC_OV_BARRAS) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarSeparadores = (IsDlgButtonChecked(hDlg, IDC_OV_SEPARADORES) == BST_CHECKED) ? 1 : 0;
            GetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf, sizeof(buf));
            {
                int bw = atoi(buf);
                if (bw < 1)
                    bw = 1;
                if (bw > 4)
                    bw = 4;
                ovPerfis[sel].espessuraBorda = bw;
            }

            AtivarPerfil(sel);
            GravarPerfisOverlay();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void MostrarDialogoConfigOverlay(HWND hwndPai)
{
    static WORD dlgBuf[1024];
    WORD *p = dlgBuf;

#define WRITE_STR_W(s)             \
    do                             \
    {                              \
        const wchar_t *_ws = (s);  \
        while (*_ws)               \
            *p++ = (WORD) * _ws++; \
        *p++ = 0;                  \
    } while (0)
#define ALIGN_DW()          \
    if (((ULONG_PTR)p) & 2) \
    p++
#define ADD_CTRL(sty, ex, xx, yy, ww, hh, iid, cls, txt) \
    do                                                   \
    {                                                    \
        ALIGN_DW();                                      \
        {                                                \
            DLGITEMTEMPLATE *_it = (DLGITEMTEMPLATE *)p; \
            _it->style = (sty);                          \
            _it->dwExtendedStyle = (ex);                 \
            _it->x = (xx);                               \
            _it->y = (yy);                               \
            _it->cx = (ww);                              \
            _it->cy = (hh);                              \
            _it->id = (iid);                             \
            p += sizeof(DLGITEMTEMPLATE) / sizeof(WORD); \
        }                                                \
        *p++ = 0xFFFF;                                   \
        *p++ = (cls);                                    \
        WRITE_STR_W(txt);                                \
        *p++ = 0;                                        \
    } while (0)

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = 33;
    dt->x = 0;
    dt->y = 0;
    dt->cx = 280;
    dt->cy = 225;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);
    *p++ = 0;
    *p++ = 0;
    WRITE_STR_W(L"Configurar Overlay");
    *p++ = 9;
    WRITE_STR_W(L"Segoe UI");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 8, 40, 10, -1, 0x0082, L"Perfil:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 48, 6, 120, 80, IDC_OV_PERFIL_CB, 0x0085, L"");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 172, 6, 30, 13, IDC_OV_BTN_NOVO, 0x0080, L"Novo");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 205, 6, 30, 13, IDC_OV_BTN_APAGAR, 0x0080, L"Apagar");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 25, 40, 10, -1, 0x0082, L"Nome:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 48, 23, 187, 12, IDC_OV_NOME, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 43, 40, 10, -1, 0x0082, L"Fonte pt:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 48, 41, 28, 12, IDC_OV_FONTSIZE, 0x0081, L"");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 82, 43, 40, 10, -1, 0x0082, L"Nome:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 120, 41, 115, 12, IDC_OV_FONTNAME, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 61, 40, 10, -1, 0x0082, L"Opac. (30-255):");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 90, 59, 35, 12, IDC_OV_OPACITY, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 7, 78, 55, 12, IDC_OV_NET, 0x0080, L"Rede");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 65, 78, 65, 12, IDC_OV_TEMP, 0x0080, L"Temperatura");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 133, 78, 55, 12, IDC_OV_DISCO, 0x0080, L"Disco I/O");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 7, 95, 226, 12, IDC_OV_CLICKTHRU, 0x0080, L"Click-through (cliques passam para janelas por baixo)");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 114, 70, 13, IDC_OV_BTN_COR_BG, 0x0080, L"Cor Fundo");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 82, 114, 70, 13, IDC_OV_BTN_COR_BORDA, 0x0080, L"Cor Borda");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 157, 114, 75, 13, IDC_OV_BTN_COR_LABEL, 0x0080, L"Cor Labels");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 130, 70, 13, IDC_OV_BTN_COR_VALOR, 0x0080, L"Cor Valores");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 82, 130, 70, 13, IDC_OV_BTN_COR_CPU, 0x0080, L"Cor Barra CPU");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 157, 130, 75, 13, IDC_OV_BTN_COR_RAM, 0x0080, L"Cor Barra RAM");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 146, 85, 13, IDC_OV_BTN_COR_TEMP, 0x0080, L"Cor Temp");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 97, 146, 85, 13, IDC_OV_BTN_COR_DISCO, 0x0080, L"Cor Disco");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 187, 146, 45, 13, IDC_OV_BTN_COR_NET, 0x0080, L"Cor Net");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 162, 85, 13, IDC_OV_BTN_COR_SEP, 0x0080, L"Cor Separador");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 97, 162, 75, 13, IDC_OV_BARRAS, 0x0080, L"Mostrar barras");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 174, 162, 80, 13, IDC_OV_SEPARADORES, 0x0080, L"Separadores");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 180, 95, 10, -1, 0x0082, L"Espessura borda:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 105, 178, 28, 12, IDC_OV_BORDERW, 0x0081, L"1");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 196, 260, 10, -1, 0x0082, L"Scroll no overlay altera o tamanho da fonte.");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 70, 208, 60, 14, IDOK, 0x0080, L"OK");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 208, 60, 14, IDCANCEL, 0x0080, L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL), (LPDLGTEMPLATE)dlgBuf, hwndPai, DialogoConfigOverlayProc);

#undef WRITE_STR_W
#undef ALIGN_DW
#undef ADD_CTRL
}

static void CriarOverlay(void)
{
    HINSTANCE hInst = GetModuleHandle(NULL);

    CarregarPerfisOverlay();
    RecalcOverlaySize();

    {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = hInst;
        wc.lpszClassName = OVERLAY_CLASS_NAME;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.style = CS_DBLCLKS;
        RegisterClassA(&wc);
    }

    RECT wa = {0};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - ovTotalW - OV_MARGIN_SCR;
    int y = wa.top + OV_MARGIN_SCR;

    hOverlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME, "WinMon Overlay",
        WS_POPUP,
        x, y, ovTotalW, ovTotalH,
        NULL, NULL, hInst, NULL);

    if (!hOverlay)
        return;

    SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
    AplicarClickThrough();
    ShowWindow(hOverlay, SW_SHOWNOACTIVATE);
    UpdateWindow(hOverlay);
    overlayAtivo = 1;
}

static void FecharOverlay(void)
{
    if (hOverlay)
    {
        DestroyWindow(hOverlay);
        hOverlay = NULL;
    }
    overlayAtivo = 0;
}

static void ToggleOverlay(void)
{
    if (overlayAtivo)
        FecharOverlay();
    else
        CriarOverlay();
}

static void AtivarAlertaOverlay(void)
{
    if (!hOverlay || !overlayAtivo)
        return;

    overlayAlertaAteTick = GetTickCount64() + OVERLAY_ALERT_MS;
    overlayAlertaVisivel = 1;
    SetTimer(hOverlay, OVERLAY_ALERT_TIMER, 250, NULL);
    InvalidateRect(hOverlay, NULL, FALSE);
}

static void AtualizarOverlay(void)
{
    if (hOverlay && overlayAtivo)
        InvalidateRect(hOverlay, NULL, FALSE);
}

void AtualizarMonitor()
{
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
    if (alertaGlobalAtivo && !alertaGlobalAnterior)
        AtivarAlertaOverlay();
    alertaGlobalAnterior = alertaGlobalAtivo;
    EscreverLinhaLog();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);
    InvalidateRect(hDashboard, NULL, FALSE);
    if (hProcessDetail)
        InvalidateRect(hProcessDetail, NULL, FALSE);

    /* Actualiza a lista completa de processos (aba Processos) */
    g_totalProcessosBruto = totalListaProcessos;
    AtualizarHistoricoProcessos();
    if (abaAtual == 5)
        AtualizarListViewProcessos();
    else
    {
        /* mesmo fora da aba, mantemos o filtro actualizado
           para que ao mudar de aba a lista já esteja pronta */
        FiltrarListaProcessos();
        if (hLabelProcCount)
        {
            char _cntbuf[64];
            snprintf(_cntbuf, sizeof(_cntbuf),
                     "%d processos  (%d visíveis)",
                     g_totalProcessosBruto, g_lvTotal);
            SetWindowTextA(hLabelProcCount, _cntbuf);
        }
    }

    AtualizarOverlay();
}

/* ------------------------------------------------------------------------- */
/* WindowProc                                                                */
/* ------------------------------------------------------------------------- */

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg,
                            WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        SYSTEM_INFO sysInfo;
        INITCOMMONCONTROLSEX icc;

        hMainWindow = hwnd;

        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&icc);

        GetSystemInfo(&sysInfo);
        numProcessadores = (int)sysInfo.dwNumberOfProcessors;
        if (numProcessadores < 1)
            numProcessadores = 1;

        ZeroMemory(&historico, sizeof(historico));

        hRichEditLib = LoadLibraryExW(L"Msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        CriarAbas(hwnd);

        if (PdhOpenQuery(NULL, 0, &hQuery) == ERROR_SUCCESS)
        {
            PdhAddEnglishCounterA(
                hQuery, "\\Processor(_Total)\\% Processor Time",
                0, &hCounterCPU);

            numNucleosMonitorizados =
                (numProcessadores < MAX_CORES)
                    ? numProcessadores
                    : MAX_CORES;

            {
                int i;
                for (i = 0; i < numNucleosMonitorizados; i++)
                {
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

        InicializarTemperaturaCPU();
        CarregarConfigIni();
        CarregarConfigVisual();
        CarregarConfigTray();
        AtualizarTextoIntervalo();

        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(NOTIFYICONDATAA);
        nid.hWnd = hwnd;
        nid.uID = ID_TRAY_ICON;
        nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        strncpy_s(nid.szTip, sizeof(nid.szTip),
                  "Monitor de Hardware", _TRUNCATE);

        SetTimer(hwnd, TIMER_ID, intervaloAtualizacaoMs, NULL);

        /* Registar hotkey global Ctrl+Shift+O para toggle overlay */
        if (!RegisterHotKey(hwnd, HOTKEY_ID_OVERLAY,
                            HOTKEY_MOD_OVERLAY, HOTKEY_VK_OVERLAY))
        {
            /* Se falhar (outra instância ou conflito), não é fatal */
        }

        return 0;
    }

    case WM_SIZE:
        RedimensionarConteudo(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 620;
        mmi->ptMinTrackSize.y = 420;
        return 0;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;

        if (dis && dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= TAB_RESUMO && dis->CtlID <= TAB_DEFINICOES)
        {
            DesenharBotaoNavegacao(dis);
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY:
    {
        NMHDR *hdr = (NMHDR *)lParam;

        if (!hdr)
            break;

        if (hdr->hwndFrom == hTab && hdr->code == TCN_SELCHANGE)
        {
            int indice = TabCtrl_GetCurSel(hTab);
            MostrarAba(indice);
            return 0;
        }

        /* Notificações do ListView de processos */
        if (hListViewProc && hdr->hwndFrom == hListViewProc)
        {
            if (hdr->code == LVN_GETDISPINFOA)
            {
                LvProcGetDispInfo((NMLVDISPINFOA *)lParam);
                return 0;
            }
            if (hdr->code == LVN_COLUMNCLICK)
            {
                NMLISTVIEW *pnm = (NMLISTVIEW *)lParam;
                LvProcColumnClick(pnm->iSubItem);
                return 0;
            }
            if (hdr->code == NM_DBLCLK)
            {
                NMITEMACTIVATE *pnm = (NMITEMACTIVATE *)lParam;
                if (pnm->iItem >= 0 && pnm->iItem < g_lvTotal)
                {
                    AbrirDetalheProcesso(g_lvProcessos[pnm->iItem].pid,
                                         g_lvProcessos[pnm->iItem].exeFile);
                }
                return 0;
            }
        }

        break;
    }

    case WM_CLOSE:
        /* Minimiza para tray em vez de terminar */
        ShowWindow(hwnd, SW_HIDE);
        if (!trayIconAtivo)
        {
            Shell_NotifyIconA(NIM_ADD, &nid);
            trayIconAtivo = 1;
            /* Tooltip inicial imediato */
            AtualizarTooltipTray();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE)
        {
            ShowWindow(hwnd, SW_HIDE);

            if (!trayIconAtivo)
            {
                Shell_NotifyIconA(NIM_ADD, &nid);
                trayIconAtivo = 1;
                AtualizarTooltipTray();
            }

            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    case WM_TRAYICON:
        if ((lParam == WM_LBUTTONUP && g_trayConfig.restaurarComClique) ||
            (lParam == WM_LBUTTONDBLCLK && g_trayConfig.restaurarComDuploClique))
        {
            if (trayIconAtivo)
            {
                Shell_NotifyIconA(NIM_DELETE, &nid);
                trayIconAtivo = 0;
            }
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();
            if (hMenu)
            {
                char cabecalho[64];
                snprintf(cabecalho, sizeof(cabecalho),
                         "CPU %.1f%% | RAM %.0f%%", ultimoCpuPercent, ultimoRamPercent);
                AppendMenuA(hMenu, MF_STRING | MF_GRAYED, 0, cabecalho);
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

                AppendMenuA(hMenu, MF_STRING, ID_TRAY_RESTORE, "Restaurar janela");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

                AppendMenuA(hMenu, MF_STRING | (overlayAtivo ? MF_CHECKED : MF_UNCHECKED),
                            ID_TOGGLE_OVERLAY, "Overlay [Ctrl+O]");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_ESTILO_MAIN, "Estilo e cores... [Ctrl+E]");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_TRAY, "Configurar tray...");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_SETTINGS, "Definicoes... [Ctrl+D]");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

                AppendMenuA(hMenu, MF_STRING, ID_TOGGLE_LOG,
                            logAtivo ? "Parar log CSV" : "Iniciar log CSV");
                AppendMenuA(hMenu, MF_STRING, ID_EXPORT_SNAPSHOT, "Exportar snapshot");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Sair");

                SetForegroundWindow(hwnd);

                POINT pt;
                GetCursorPos(&pt);
                TrackPopupMenu(hMenu,
                               TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                               pt.x, pt.y, 0, hwnd, NULL);

                PostMessage(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
            }
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) >= TAB_RESUMO &&
            LOWORD(wParam) <= TAB_DEFINICOES &&
            HIWORD(wParam) == BN_CLICKED)
        {
            int indice = LOWORD(wParam) - TAB_RESUMO;
            TabCtrl_SetCurSel(hTab, indice);
            MostrarAba(indice);
            return 0;
        }
        if (LOWORD(wParam) == IDC_EDIT_PESQUISA_PROC &&
            HIWORD(wParam) == EN_CHANGE)
        {
            AtualizarListViewProcessos();
            return 0;
        }
        if (LOWORD(wParam) >= IDC_SETTINGS_HDR_ALERT &&
            LOWORD(wParam) <= IDC_SETTINGS_HDR_DADOS &&
            HIWORD(wParam) == BN_CLICKED)
        {
            int sec = (int)LOWORD(wParam) - IDC_SETTINGS_HDR_ALERT;
            if (sec >= 0 && sec < SETTINGS_SECTIONS)
            {
                g_settingsOpen[sec] = !g_settingsOpen[sec];
                RedimensionarConteudo(hwnd);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_ALERT)
        {
            MostrarDialogoAlertas(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_APAR)
        {
            MostrarDialogoEstiloPrincipal(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_OV_CFG)
        {
            MostrarDialogoConfigOverlay(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_OV_TOGGLE)
        {
            ToggleOverlay();
            AtualizarDefinicoesVisibilidade();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_TRAY)
        {
            MostrarDialogoConfigTray(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_INTERVAL)
        {
            CiclarIntervaloAtualizacao();
            AtualizarDefinicoesVisibilidade();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_LOG)
        {
            if (logAtivo)
            {
                FecharLogCSV();
                MessageBoxA(hwnd, "Log CSV interrompido.", "WinMon", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                IniciarLogCSV();
                if (logAtivo)
                {
                    char msg[MAX_PATH + 64];
                    snprintf(msg, sizeof(msg), "Log CSV iniciado:\n%s", logNomeFicheiro);
                    MessageBoxA(hwnd, msg, "WinMon", MB_OK | MB_ICONINFORMATION);
                }
            }
            AtualizarDefinicoesVisibilidade();
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_SNAPSHOT)
        {
            ExportarSnapshot();
            return 0;
        }
        if (LOWORD(wParam) == ID_EXPORT_SNAPSHOT)
        {
            ExportarSnapshot();
            return 0;
        }
        if (LOWORD(wParam) == ID_TOGGLE_LOG)
        {
            if (logAtivo)
            {
                FecharLogCSV();
                MessageBoxA(hwnd, "Log CSV interrompido.", "WinMon", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                IniciarLogCSV();
                if (logAtivo)
                {
                    char msg[MAX_PATH + 64];
                    snprintf(msg, sizeof(msg), "Log CSV iniciado:\n%s", logNomeFicheiro);
                    MessageBoxA(hwnd, msg, "WinMon", MB_OK | MB_ICONINFORMATION);
                }
            }
            return 0;
        }
        if (LOWORD(wParam) == ID_CONFIG_SETTINGS)
        {
            TabCtrl_SetCurSel(hTab, 6);
            MostrarAba(6);
            return 0;
        }
        if (LOWORD(wParam) == ID_CONFIG_ALERTAS)
        {
            MostrarDialogoAlertas(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_CONFIG_ESTILO_MAIN)
        {
            MostrarDialogoEstiloPrincipal(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_CONFIG_TRAY)
        {
            MostrarDialogoConfigTray(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_CYCLE_INTERVAL)
        {
            CiclarIntervaloAtualizacao();
            return 0;
        }
        if (LOWORD(wParam) == ID_TOGGLE_OVERLAY)
        {
            ToggleOverlay();
            AtualizarDefinicoesVisibilidade();
            return 0;
        }
        if (LOWORD(wParam) == ID_CONFIG_OVERLAY)
        {
            MostrarDialogoConfigOverlay(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_RESTORE)
        {
            if (trayIconAtivo)
            {
                Shell_NotifyIconA(NIM_DELETE, &nid);
                trayIconAtivo = 0;
            }
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == ID_TRAY_EXIT)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

    case WM_HOTKEY:
        if ((int)wParam == HOTKEY_ID_OVERLAY)
        {
            ToggleOverlay();
            AtualizarDefinicoesVisibilidade();
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID)
            AtualizarMonitor();
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID_OVERLAY);
        KillTimer(hwnd, TIMER_ID);

        FecharOverlay();
        if (hProcessDetail && IsWindow(hProcessDetail))
            DestroyWindow(hProcessDetail);
        hProcessDetail = NULL;

        if (hQuery)
        {
            PdhCloseQuery(hQuery);
            hQuery = NULL;
        }

        if (hFontMonitor)
        {
            DeleteObject(hFontMonitor);
            hFontMonitor = NULL;
        }

        if (hFontUI)
        {
            DeleteObject(hFontUI);
            hFontUI = NULL;
        }

        if (trayIconAtivo)
        {
            Shell_NotifyIconA(NIM_DELETE, &nid);
            trayIconAtivo = 0;
        }

        FecharLogCSV();
        TerminarWMI();

        if (hRichEditLib)
        {
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
                   LPSTR lpCmdLine, int nCmdShow)
{
    const char CLASS_NAME[] = "HardwareMonitorClassV6";
    WNDCLASSA wc;
    HWND hwnd;
    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL, 'S', ID_EXPORT_SNAPSHOT},
        {FVIRTKEY | FCONTROL, 'L', ID_TOGGLE_LOG},
        {FVIRTKEY | FCONTROL, 'A', ID_CONFIG_ALERTAS},
        {FVIRTKEY | FCONTROL, 'E', ID_CONFIG_ESTILO_MAIN},
        {FVIRTKEY | FCONTROL, 'O', ID_TOGGLE_OVERLAY},
        {FVIRTKEY | FCONTROL, 'D', ID_CONFIG_SETTINGS}};
    HACCEL hAccel;
    MSG msg;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    HANDLE hInstanceMutex = NULL;

    (void)hPrevInstance;

    int flagOverlay = (lpCmdLine &&
                       (strstr(lpCmdLine, "--overlay") != NULL ||
                        strstr(lpCmdLine, "-overlay") != NULL));

    hInstanceMutex = CreateMutexW(
        NULL, TRUE, L"Local\\WinMon-HardwareMonitor-V6");

    if (!hInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (hInstanceMutex)
            CloseHandle(hInstanceMutex);
        return 0;
    }

    if (hUser32)
    {
        typedef BOOL(WINAPI * SetDpiCtxFunc)(DPI_AWARENESS_CONTEXT);
        SetDpiCtxFunc pSetDpiCtx =
            (SetDpiCtxFunc)GetProcAddress(
                hUser32, "SetProcessDpiAwarenessContext");

        if (pSetDpiCtx)
            pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else
            SetProcessDPIAware();
    }
    else
    {
        SetProcessDPIAware();
    }

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

    {
        WNDCLASSA dashboardClass;
        ZeroMemory(&dashboardClass, sizeof(dashboardClass));

        dashboardClass.lpfnWndProc = DashboardProc;
        dashboardClass.hInstance = hInstance;
        dashboardClass.lpszClassName = "HardwareMonitorDashboard";
        dashboardClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        dashboardClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        RegisterClassA(&dashboardClass);
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

    hAccel = CreateAcceleratorTableA(accels, 6);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (flagOverlay)
        CriarOverlay();

    ZeroMemory(&msg, sizeof(msg));

    while (GetMessageA(&msg, NULL, 0, 0))
    {
        if (!TranslateAcceleratorA(hwnd, hAccel, &msg))
        {
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