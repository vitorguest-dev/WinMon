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
 *   - permitir navegar/zoomar no historico dos graficos.
 *   - exportar graficos diretamente para PNG via WIC.
 *   - permitir escolher o monitor do overlay e manter a escolha.
 *   - disponibilizar um modo compacto sempre visivel.
 *   - aba Ligacoes: ligacoes TCP/UDP (IPv4/IPv6) com o processo dono;
 *   - frequencia do CPU e memoria da GPU (cartoes do Resumo e overlay).
 *   - aba Rede Local: scanner ARP (fabricante, nome, alerta de dispositivo novo),
 *     traceroute com latencia por salto e teste de DNS por servidor.
 *
 * Recomendações de compilação (MSVC x64):
 *   /O2 /W4 /WX /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT
 *   Linker: /GUARD:CF /CETCOMPAT /DYNAMICBASE /NXCOMPAT
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

/* MinGW-w64/ucrt64 compatibility: some PDH headers do not expose
 * PDH_MORE_DATA even though PdhExpandWildCardPathA may return it. */
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#endif
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <dxgi.h>
#include <shellapi.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <wincodec.h>
#include <icmpapi.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
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
#pragma comment(lib, "windowscodecs.lib")

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
#define MAX_GPU_COUNTERS 256

/* Tipos de engine GPU (mesmos que o Gestor de Tarefas monitoriza) */
#define GPU_ENG_3D 0
#define GPU_ENG_COPY 1
#define GPU_ENG_ENCODE 2
#define GPU_ENG_DECODE 3
#define GPU_ENG_COUNT 4

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
#define ID_EXPORT_GRAPH_PNG 1012
#define ID_TOGGLE_COMPACT 1013
#define ID_SESSION_EXPORT 1014
#define ID_SESSION_RESET 1015
#define ID_SETTINGS_HELP 8021

#define OVERLAY_CLASS_NAME "WinMonOverlayClass"

/* Hotkey global: Ctrl+Shift+O para toggle do overlay */
#define HOTKEY_ID_OVERLAY 1
#define HOTKEY_ID_COMPACT 2
#define HOTKEY_MOD_OVERLAY (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT)
#define HOTKEY_VK_OVERLAY 'O'
#define HOTKEY_MOD_COMPACT (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT)
#define HOTKEY_VK_COMPACT 'C'
#define HOTKEY_ID_MARKER 3
#define HOTKEY_MOD_MARKER (MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT)
#define HOTKEY_VK_MARKER 'M'
#define SESSION_MAX_SAMPLES 100000
#define SESSION_MAX_MARKERS 256

#define OV_FONT_MIN 8
#define OV_FONT_MAX 32
#define OV_SNAP_DIST 40
#define OV_MAX_PERFIS 16
#define OV_NOME_MAX 32

#define TAB_RESUMO 2001
#define TAB_CPU 2002
#define TAB_MEMORIA 2003
#define TAB_TEMPERATURA 2004
#define TAB_DISCO 2005
#define TAB_REDE 2006
#define TAB_GPU 2007
#define TAB_PROCESSOS 2008
#define TAB_LIGACOES 2009
#define TAB_DEFINICOES 2010
#define TAB_REDE_LOCAL 2011
#define NAV_LARGURA 188
#define NUM_ABAS 11
#define ALERT_HISTORY_MAX 100

#define MAX_TEMP_ZONAS 16
#define BALLOON_COOLDOWN_SEGUNDOS 30
#define HISTORICO_PONTOS 2048
#define PROCESS_HISTORICO_PONTOS 120
#define MAX_HISTORICO MAX_PROCESSES
#define GRAPH_DEFAULT_VISIBLE 120
#define GRAPH_MIN_VISIBLE 15

typedef struct
{
    DWORD pid;
    SIZE_T memUsageMB;
    double cpuPercent;
    char exeFile[MAX_PATH];
    int instancias; /* >1 apenas em linhas agrupadas (aba Processos) */
    double netDownBps; /* bytes/s recebidos pelo processo (TCP EStats) */
    double netUpBps;   /* bytes/s enviados pelo processo (TCP EStats) */
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
    double cpu[PROCESS_HISTORICO_PONTOS];
    double ram[PROCESS_HISTORICO_PONTOS];
    ULONGLONG ultimoVisto;
    int pos;
    int count;
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
    double temp[HISTORICO_PONTOS];
    double diskRead[HISTORICO_PONTOS];
    double diskWrite[HISTORICO_PONTOS];
    double netDown[HISTORICO_PONTOS];
    double netUp[HISTORICO_PONTOS];
    double gpu[HISTORICO_PONTOS]; /* 3D (usado no overlay) */
    double gpuCopy[HISTORICO_PONTOS];
    double gpuEncode[HISTORICO_PONTOS];
    double gpuDecode[HISTORICO_PONTOS];
    double ping[HISTORICO_PONTOS];      /* ms; -1 = perdido; NAN = sem dados */
    double wifiSinal[HISTORICO_PONTOS]; /* % ; NAN = sem Wi-Fi ligado */
    double core[MAX_CORES][HISTORICO_PONTOS];
    SYSTEMTIME timestamp[HISTORICO_PONTOS];
    ULONGLONG tickMs[HISTORICO_PONTOS];

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
    int mostrarGpu;
    int mostrarPing;
    int mostrarVram; /* coluna VRAM (memoria da GPU) */
    int mostrarFreq; /* coluna FREQ (frequencia do CPU) */
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
    COLORREF corGraficoGpu;
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
    .corFundoGrafico = RGB(248, 250, 252),
    .corGrelhaGrafico = RGB(226, 231, 236),
    .corEixoGrafico = RGB(170, 178, 187),
    .corTextoGrafico = RGB(40, 44, 48),
    .corGraficoCpu = RGB(35, 115, 210),
    .corGraficoRam = RGB(145, 75, 185),
    .corGraficoDiscoRead = RGB(40, 120, 200),
    .corGraficoDiscoWrite = RGB(215, 120, 45),
    .corGraficoNetDown = RGB(35, 145, 95),
    .corGraficoNetUp = RGB(205, 80, 80),
    .corGraficoProcesso = RGB(205, 70, 70),
    .corGraficoCore = RGB(85, 145, 95),
    .corGraficoGpu = RGB(120, 80, 190),
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
static ULONGLONG logUltimoTickMs = 0; /* instante da ultima linha gravada */
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
HWND hNav[NUM_ABAS] = {NULL};
HWND hAppTitle = NULL;
HWND hAppSubtitle = NULL;
HWND hAppStatus = NULL;
HWND hBtnOverlay = NULL;
HWND hBtnInterval = NULL;
HWND hBtnEstiloMain = NULL;
HWND hGraphCPU = NULL;
HWND hGraphRAM = NULL;
HWND hGraphTemp = NULL;
HWND hGraphDisk = NULL;
HWND hGraphNet = NULL;
HWND hGraphGPU = NULL;
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
#define IDC_SETTINGS_HELP 8021
#define IDC_SETTINGS_ALERT 8011
#define IDC_SETTINGS_APAR 8012
#define IDC_SETTINGS_OV_CFG 8013
#define IDC_SETTINGS_OV_TOGGLE 8014
#define IDC_SETTINGS_TRAY 8015
#define IDC_SETTINGS_INTERVAL 8016
#define IDC_SETTINGS_LOG 8017
#define IDC_SETTINGS_SNAPSHOT 8018
#define IDC_SETTINGS_COMPACT 8019
#define IDC_SETTINGS_ALERT_HISTORY 8020
#define IDC_ALERT_HISTORY_CLEAR 8030

static HWND hSettingsTitle = NULL;
static HWND hSettingsHeaders[SETTINGS_SECTIONS] = {NULL};
static HWND hSettingsActions[SETTINGS_SECTIONS][4] = {{NULL}};
static int g_settingsOpen[SETTINGS_SECTIONS] = {1, 1, 1, 1, 1};

/* estado de ordenação do ListView */
static int g_lvSortCol = 1;  /* 0=Nome,1=PID,2=CPU,3=RAM,4=Download,5=Upload */
static int g_lvSortDesc = 1; /* 1=descendente, 0=ascendente */

/* agrupamento de processos pelo nome (ex.: 30x chrome.exe -> 1 linha) */
#define IDC_CHK_AGRUPAR 6004
static int g_lvAgrupar = 0;    /* 1 = vista agrupada */
static int g_lvAgruparUI = -1; /* ultimo estado refletido na UI */
HWND hChkAgrupar = NULL;

/* aba Ligacoes */
HWND hPainelLig = NULL;     /* label "Filtrar" */
HWND hEditLig = NULL;       /* caixa de filtro */
HWND hChkLigAtivas = NULL;  /* "Apenas estabelecidas" */
HWND hLabelLigCount = NULL; /* "N de M ligacoes" */
HWND hListViewLig = NULL;   /* ListView virtual */

/* --- Painel Rede Local: dispositivos (ARP), traceroute e teste de DNS --- */
#define IDC_LAN_SCAN 6020
#define IDC_LAN_STATUS 6021
#define IDC_LAN_LIST 6022
#define IDC_LAN_ACEITAR 6023
#define IDC_REDE_MODO_LAN 6030
#define IDC_REDE_MODO_TRACE 6031
#define IDC_REDE_MODO_DNS 6032
#define IDC_TRACE_HOST 6033
#define IDC_TRACE_GO 6034
#define IDC_TRACE_STATUS 6035
#define IDC_TRACE_LIST 6036
#define IDC_TRACE_NOMES 6037
#define IDC_DNS_DOMINIO 6038
#define IDC_DNS_GO 6039
#define IDC_DNS_STATUS 6040
#define IDC_DNS_LIST 6041
#define WM_LAN_SCAN_DONE (WM_APP + 12)
#define WM_TRACE_HOP (WM_APP + 13)
#define WM_TRACE_DONE (WM_APP + 14)
#define WM_DNS_RESULT (WM_APP + 15)
#define LAN_MAX_DEVICES 512
#define LAN_MAX_SCAN_IPS 256
typedef struct
{
    DWORD ip;       /* endereço IPv4 em ordem de rede */
    BYTE mac[6];
    char ipText[16];
    char macText[24];
    char vendor[40]; /* fabricante (OUI) */
    char host[64];   /* nome (DNS inverso) */
    char estado[12]; /* "Conhecido", "NOVO" ou "Este PC" */
    int self;
} LanDevice;
static HWND hRedeModo[3] = {NULL, NULL, NULL};
static HWND hLanScan = NULL;
static HWND hLanAceitar = NULL;
static HWND hLanStatus = NULL;
static HWND hLanList = NULL;
static HWND hTraceLbl = NULL;
static HWND hTraceHost = NULL;
static HWND hTraceGo = NULL;
static HWND hTraceNomes = NULL;
static HWND hTraceStatus = NULL;
static HWND hTraceList = NULL;
static HWND hDnsLbl = NULL;
static HWND hDnsDominio = NULL;
static HWND hDnsGo = NULL;
static HWND hDnsStatus = NULL;
static HWND hDnsList = NULL;
static HANDLE g_lanThread = NULL;
static volatile LONG g_lanScanRunning = 0;
static LanDevice g_lanDevices[LAN_MAX_DEVICES];
static int g_lanDeviceCount = 0;


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
/* Contadores PDH separados por tipo de engine GPU */
static PDH_HCOUNTER hCounterGpu[GPU_ENG_COUNT][MAX_GPU_COUNTERS];
static int numGpuCounters[GPU_ENG_COUNT] = {0, 0, 0, 0};

/* Frequencia do CPU (PDH) e memoria da GPU (PDH + DXGI) */
#define MAX_VRAM_COUNTERS 8
static PDH_HCOUNTER hCounterCpuFreq = NULL; /* \Processor Information(_Total)\Processor Frequency */
static PDH_HCOUNTER hCounterCpuPerf = NULL; /* \Processor Information(_Total)\% Processor Performance */
static PDH_HCOUNTER hCounterVram[MAX_VRAM_COUNTERS];
static int numVramCounters = 0;
static double g_cpuBaseMhzReg = 0.0;     /* ~MHz do registry (fallback da frequencia base) */
static double ultimoCpuFreqMHz = 0.0;    /* 0 = indisponivel */
static double ultimoCpuFreqBaseMHz = 0.0;
static double ultimoCpuPerfPct = 0.0;    /* % Processor Performance (100 = frequencia base) */
static double ultimaVramUsadaMB = -1.0;  /* < 0 = indisponivel */
static double g_vramTotalMB = 0.0;       /* 0 = desconhecido */
static int g_vramIntegrada = 0;          /* 1 = GPU integrada (memoria dedicada + partilhada) */

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
static double ultimoDiscoUsoPercent = 0.0;
static double ultimoNetDown = 0.0;
static double ultimoNetUp = 0.0;
static double ultimoGpuPercent = 0.0; /* 3D — usado no overlay */
static double ultimoGpuCopy = 0.0;
static double ultimoGpuEncode = 0.0;
static double ultimoGpuDecode = 0.0;

static IntervaloAlerta intervalosAlerta[MAX_ALERT_RANGES];
static int totalIntervalosAlerta = 0;
static int alertaGlobalAtivo = 0;

typedef struct
{
    SYSTEMTIME timestamp;
    char recurso[32];
    double valor;
    double limite;
    char culpado[128];
} AlertaHistoricoItem;

static AlertaHistoricoItem g_alertHistory[ALERT_HISTORY_MAX];
static int g_alertHistoryCount = 0;
static int g_alertCpuAnterior = 0;
static int g_alertRamAnterior = 0;
static int g_alertDiscoAnterior = 0;
static HWND hAlertHistory = NULL;

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
#define OV_GPU (ovPerfis[ovPerfilActivo].mostrarGpu)
#define OV_PING (ovPerfis[ovPerfilActivo].mostrarPing)
#define OV_VRAM (ovPerfis[ovPerfilActivo].mostrarVram)
#define OV_FREQ (ovPerfis[ovPerfilActivo].mostrarFreq)
#define OV_CLICKTHRU (ovPerfis[ovPerfilActivo].clickThrough)

static int ovColW = 120;
static int ovRowH = 0;
static int ovTotalW = 0;
static int ovTotalH = 0;

static ProcessoInfo listaProcessos[MAX_PROCESSES];
static HistoricoMonitor historico;


/* Implementacao organizada por areas; compilada como uma unica unidade (ver README). */
#include "modules/01_network_status.inc"
#include "modules/02_utilities_and_collection.inc"
#include "modules/03_graphics.inc"
#include "modules/04_ui_and_tabs.inc"
#include "modules/05_processes_and_connections.inc"
#include "modules/06_local_network.inc"
#include "modules/07_panels_and_events.inc"
#include "modules/08_sessions_logging_alerts.inc"
#include "modules/09_style_dialogs.inc"
#include "modules/10_overlay.inc"
#include "modules/11_window_and_entrypoint.inc"
