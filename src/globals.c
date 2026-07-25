#include "globals.h"

HWND hMainWindow = NULL;
HWND hEdit = NULL;

PDH_HQUERY hQuery = NULL;
PDH_HCOUNTER hCounterCPU = NULL;
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
int numNucleosMonitorizados = 0;

char ultimoSnapshot[BUFFER_SIZE];
double ultimoRamPercent = 0.0;
double ultimoCpuPercent = 0.0;

IntervaloAlerta intervalosAlerta[MAX_ALERT_RANGES];
int totalIntervalosAlerta = 0;
int alertaGlobalAtivo = 0;
