#ifndef GLOBALS_H
#define GLOBALS_H

#include <windows.h>
#include <pdh.h>
#include <shellapi.h>

// ==================== Configuração geral ====================
#define TIMER_ID 1
#define BUFFER_SIZE 16384 // Buffer alargado para 16KB para evitar qualquer overflow
#define MAX_PROCESSES 2048
#define MAX_ALERT_RANGES 32
#define MAX_CORES 64       // Limite de núcleos monitorizados individualmente
#define MAX_HISTORICO 2048
#define ID_EXPORT_SNAPSHOT 1001
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1

// Limites que disparam alerta visual
#define LIMITE_RAM_PERCENT 90.0
#define LIMITE_DISCO_PERCENT 95.0

// ==================== Estruturas partilhadas ====================

// Dados de cada processo, usados na ordenação por CPU/RAM
typedef struct {
    DWORD pid;
    SIZE_T memUsageMB;
    double cpuPercent;
    char exeFile[MAX_PATH];
} ProcessoInfo;

// Tempos de CPU anteriores de um processo, para calcular o delta entre ciclos
typedef struct {
    DWORD pid;
    ULONGLONG lastKernelTime;
    ULONGLONG lastUserTime;
    int valido;
} ProcessoCpuHistorico;

// Um intervalo de texto (início/fim em caracteres) a colorir de vermelho no RichEdit
typedef struct {
    long inicio;
    long fim;
} IntervaloAlerta;

// ==================== Estado global partilhado ====================
// Definido a sério em globals.c; todos os outros ficheiros apenas o referenciam.

extern HWND hMainWindow;
extern HWND hEdit;

extern PDH_HQUERY hQuery;
extern PDH_HCOUNTER hCounterCPU;
extern PDH_HCOUNTER hCounterCoresCPU[MAX_CORES];
extern PDH_HCOUNTER hCounterDiskRead;
extern PDH_HCOUNTER hCounterDiskWrite;

extern HFONT hFontMonitor;
extern HMODULE hRichEditLib;

extern NOTIFYICONDATAA nid;
extern int trayIconAtivo;

extern DWORDLONG lastIn;
extern DWORDLONG lastOut;
extern int firstNetworkRead;

extern int numProcessadores;
extern int numNucleosMonitorizados;

// Estado do último ciclo, usado pelo export de snapshot (Ctrl+S) e pelo tooltip do tray
extern char ultimoSnapshot[BUFFER_SIZE];
extern double ultimoRamPercent;
extern double ultimoCpuPercent;

// Intervalos de alerta detetados no ciclo atual (para colorir a vermelho)
extern IntervaloAlerta intervalosAlerta[MAX_ALERT_RANGES];
extern int totalIntervalosAlerta;
extern int alertaGlobalAtivo;

#endif // GLOBALS_H
