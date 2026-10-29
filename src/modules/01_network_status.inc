/* 01 network status */
/* ------------------------------------------------------------------------- */
/* Latencia (ICMP) e Wi-Fi (WLAN API)                                        */
/* DLLs carregadas dinamicamente de System32 (sem novas libs no linker) e    */
/* recolha numa thread propria, para nunca bloquear a interface.             */
/* ------------------------------------------------------------------------- */

#define PING_ALVOS 3
#define PING_JANELA 60
#define PING_INTERVALO_MS 1000
#define PING_PAINEL_ALTURA_MIN 480

typedef struct
{
    ULONG Address;
    ULONG Status;
    ULONG RoundTripTime;
    USHORT DataSize;
    USHORT Reserved;
    PVOID Data;
    UCHAR Ttl;
    UCHAR Tos;
    UCHAR Flags;
    UCHAR OptionsSize;
    PUCHAR OptionsData;
} WmIcmpReply;

typedef HANDLE(WINAPI *PfnIcmpCreateFile)(void);
typedef BOOL(WINAPI *PfnIcmpCloseHandle)(HANDLE);
typedef DWORD(WINAPI *PfnIcmpSendEcho)(HANDLE, ULONG, LPVOID, WORD, LPVOID, LPVOID, DWORD, DWORD);

typedef struct
{
    GUID guid;
    WCHAR desc[256];
    int estado;
} WmWlanIntf;

typedef struct
{
    DWORD n;
    DWORD idx;
    WmWlanIntf intf[1];
} WmWlanIntfList;

typedef struct
{
    ULONG ssidLen;
    UCHAR ssid[32];
    int bssType;
    UCHAR bssid[6];
    int phy;
    ULONG phyIdx;
    ULONG sinal;
    ULONG rxKbps;
    ULONG txKbps;
} WmWlanAssoc;

typedef struct
{
    BOOL seg;
    BOOL onex;
    int auth;
    int cipher;
} WmWlanSeg;

typedef struct
{
    int estado;
    int modo;
    WCHAR perfil[256];
    WmWlanAssoc assoc;
    WmWlanSeg seguranca;
} WmWlanConn;

typedef DWORD(WINAPI *PfnWlanOpenHandle)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD(WINAPI *PfnWlanCloseHandle)(HANDLE, PVOID);
typedef DWORD(WINAPI *PfnWlanEnumInterfaces)(HANDLE, PVOID, WmWlanIntfList **);
typedef DWORD(WINAPI *PfnWlanQueryInterface)(HANDLE, const GUID *, int, PVOID, PDWORD, PVOID *, PVOID);
typedef VOID(WINAPI *PfnWlanFreeMemory)(PVOID);

typedef struct
{
    int ativo;
    int alvo;
    int semGateway;
    char alvoNome[48];
    double ultimoMs; /* NAN = sem dados, -1 = pacote perdido */
    double minMs;
    double maxMs;
    double mediaMs;
    double jitterMs;
    unsigned long enviados;
    unsigned long recebidos;
    double perdaJanela; /* % nos ultimos PING_JANELA pacotes */
} PingEstado;

typedef struct
{
    int adaptador;
    int ligado;
    char ssid[64];
    char adaptadorNome[96];
    int qualidade; /* 0..100 */
    double dbm;    /* estimativa a partir da qualidade */
    ULONG rxKbps;
    ULONG txKbps;
    int phy;
    int auth;
} WifiEstado;

static CRITICAL_SECTION g_pwCs;
static int g_pwCsInit = 0;
static HANDLE g_pwThread = NULL;
static HANDLE g_pwStopEvt = NULL;
static volatile LONG g_pingAlvoIdx = 1;
static PingEstado g_pingEst;
static WifiEstado g_wifiEst;
static RECT g_pingTitleRect = {0, 0, 0, 0};
static const char *const g_pingAlvoNome[PING_ALVOS] = {"Gateway", "1.1.1.1", "8.8.8.8"};
static const BYTE g_pingAlvoIp[PING_ALVOS][4] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {8, 8, 8, 8}};

static void PingGuardarAlvo(void)
{
    HKEY hk;
    DWORD v = (DWORD)g_pingAlvoIdx;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WinMon\\Ping", 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hk, "Alvo", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegCloseKey(hk);
    }
}

static void PingCarregarAlvo(void)
{
    HKEY hk;
    DWORD v = 0, sz = sizeof(v), t = 0;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\WinMon\\Ping", 0,
                      KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hk, "Alvo", NULL, &t, (BYTE *)&v, &sz) == ERROR_SUCCESS &&
            t == REG_DWORD && v < PING_ALVOS)
            g_pingAlvoIdx = (LONG)v;
        RegCloseKey(hk);
    }
}

static void PingCiclarAlvo(void)
{
    LONG n = (g_pingAlvoIdx + 1) % PING_ALVOS;
    InterlockedExchange(&g_pingAlvoIdx, n);
    PingGuardarAlvo();
}

static int PingObterGateway(ULONG *addr, char *txt, size_t n)
{
    ULONG len = 0;
    PIP_ADAPTER_INFO info, p;
    int ok = 0;

    if (GetAdaptersInfo(NULL, &len) != ERROR_BUFFER_OVERFLOW || len == 0)
        return 0;
    info = (PIP_ADAPTER_INFO)malloc(len);
    if (!info)
        return 0;
    if (GetAdaptersInfo(info, &len) == NO_ERROR)
    {
        for (p = info; p && !ok; p = p->Next)
        {
            unsigned a = 0, b = 0, c = 0, d = 0;
            const char *s = p->GatewayList.IpAddress.String;

            if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
                a < 256 && b < 256 && c < 256 && d < 256 && (a | b | c | d) != 0)
            {
                *addr = (ULONG)(a | (b << 8) | (c << 16) | ((ULONG)d << 24));
                snprintf(txt, n, "%u.%u.%u.%u", a, b, c, d);
                ok = 1;
            }
        }
    }
    free(info);
    return ok;
}

static void WifiSsidParaAnsi(const UCHAR *s, ULONG len, char *out, size_t n)
{
    WCHAR wb[64];
    char raw[40];
    int wl;

    if (len > 32)
        len = 32;
    if (len == 0)
    {
        snprintf(out, n, "(oculta)");
        return;
    }
    memcpy(raw, s, len);
    raw[len] = '\0';
    wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, raw, (int)len, wb, 63);
    if (wl > 0)
    {
        wb[wl] = L'\0';
        if (WideCharToMultiByte(CP_ACP, 0, wb, -1, out, (int)n, NULL, NULL) > 0)
            return;
    }
    snprintf(out, n, "%s", raw);
}

static const char *WifiPhyNome(int phy)
{
    switch (phy)
    {
    case 2:
        return "802.11 DSSS";
    case 4:
        return "802.11a";
    case 5:
        return "802.11b";
    case 6:
        return "802.11g";
    case 7:
        return "Wi-Fi 4 (802.11n)";
    case 8:
        return "Wi-Fi 5 (802.11ac)";
    case 10:
        return "Wi-Fi 6 (802.11ax)";
    case 11:
        return "Wi-Fi 7 (802.11be)";
    default:
        return "Norma n/d";
    }
}

static const char *WifiAuthNome(int auth)
{
    switch (auth)
    {
    case 1:
        return "Aberta";
    case 2:
        return "WEP";
    case 3:
        return "WPA";
    case 4:
        return "WPA-PSK";
    case 6:
        return "WPA2";
    case 7:
        return "WPA2-PSK";
    case 8:
        return "WPA3-Ent 192";
    case 9:
        return "WPA3";
    case 10:
        return "OWE";
    case 11:
        return "WPA3-Ent";
    default:
        return "Seg. n/d";
    }
}

/* Chamado apenas pela thread de recolha. */
static void WifiConsultar(WifiEstado *w)
{
    static HMODULE hWlan = NULL;
    static HANDLE cli = NULL;
    static int tentou = 0;
    static unsigned retry = 0;
    static PfnWlanOpenHandle fOpen;
    static PfnWlanCloseHandle fClose;
    static PfnWlanEnumInterfaces fEnum;
    static PfnWlanQueryInterface fQuery;
    static PfnWlanFreeMemory fFree;
    WmWlanIntfList *lista = NULL;
    DWORD i, k = 0;

    memset(w, 0, sizeof(*w));

    if (!tentou)
    {
        tentou = 1;
        hWlan = LoadLibraryExW(L"wlanapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (hWlan)
        {
            fOpen = (PfnWlanOpenHandle)(void *)GetProcAddress(hWlan, "WlanOpenHandle");
            fClose = (PfnWlanCloseHandle)(void *)GetProcAddress(hWlan, "WlanCloseHandle");
            fEnum = (PfnWlanEnumInterfaces)(void *)GetProcAddress(hWlan, "WlanEnumInterfaces");
            fQuery = (PfnWlanQueryInterface)(void *)GetProcAddress(hWlan, "WlanQueryInterface");
            fFree = (PfnWlanFreeMemory)(void *)GetProcAddress(hWlan, "WlanFreeMemory");
            if (!fOpen || !fClose || !fEnum || !fQuery || !fFree)
            {
                FreeLibrary(hWlan);
                hWlan = NULL;
            }
        }
    }
    if (!hWlan)
        return;

    if (!cli)
    {
        DWORD neg = 0;
        if ((retry++ % 5) != 0)
            return; /* servico WLAN parado: tenta de novo de vez em quando */
        if (fOpen(2, NULL, &neg, &cli) != ERROR_SUCCESS)
        {
            cli = NULL;
            return;
        }
    }

    if (fEnum(cli, NULL, &lista) != ERROR_SUCCESS || !lista)
    {
        fClose(cli, NULL); /* handle invalido (servico reiniciou): reabre depois */
        cli = NULL;
        return;
    }

    if (lista->n > 0)
    {
        WmWlanConn *c = NULL;
        DWORD sz = 0;

        for (i = 0; i < lista->n; i++)
            if (lista->intf[i].estado == 1)
            {
                k = i;
                break;
            }

        w->adaptador = 1;
        if (WideCharToMultiByte(CP_ACP, 0, lista->intf[k].desc, -1,
                                w->adaptadorNome, (int)sizeof(w->adaptadorNome),
                                NULL, NULL) <= 0)
            w->adaptadorNome[0] = '\0';

        if (lista->intf[k].estado == 1 &&
            fQuery(cli, &lista->intf[k].guid, 7 /* current_connection */, NULL, &sz,
                   (PVOID *)&c, NULL) == ERROR_SUCCESS &&
            c)
        {
            w->ligado = (c->estado == 1);
            if (w->ligado)
            {
                WifiSsidParaAnsi(c->assoc.ssid, c->assoc.ssidLen, w->ssid, sizeof(w->ssid));
                w->qualidade = (int)c->assoc.sinal;
                if (w->qualidade > 100)
                    w->qualidade = 100;
                w->dbm = (double)w->qualidade / 2.0 - 100.0;
                w->rxKbps = c->assoc.rxKbps;
                w->txKbps = c->assoc.txKbps;
                w->phy = c->assoc.phy;
                w->auth = c->seguranca.auth;
            }
            fFree(c);
        }
    }
    fFree(lista);
}

static DWORD WINAPI PingWifiThread(LPVOID arg)
{
    HMODULE hIp;
    PfnIcmpCreateFile fCreate = NULL;
    PfnIcmpCloseHandle fClose = NULL;
    PfnIcmpSendEcho fSend = NULL;
    HANDLE hIcmp = INVALID_HANDLE_VALUE;
    LONG lastAlvo = -1;
    ULONG gwAddr = 0;
    char gwTxt[24] = {0};
    int gwOk = 0;
    unsigned long iter = 0;
    double prevRtt = -1.0, jitter = 0.0, soma = 0.0, mn = 0.0, mx = 0.0;
    unsigned long env = 0, rec = 0;
    BYTE janela[PING_JANELA];
    int jPos = 0, jCount = 0;
    static const char payload[] = "WinMon ping 0123456789abcde";

    (void)arg;
    memset(janela, 0, sizeof(janela));

    hIp = LoadLibraryExW(L"iphlpapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hIp)
    {
        fCreate = (PfnIcmpCreateFile)(void *)GetProcAddress(hIp, "IcmpCreateFile");
        fClose = (PfnIcmpCloseHandle)(void *)GetProcAddress(hIp, "IcmpCloseHandle");
        fSend = (PfnIcmpSendEcho)(void *)GetProcAddress(hIp, "IcmpSendEcho");
        if (fCreate && fClose && fSend)
            hIcmp = fCreate();
    }

    while (WaitForSingleObject(g_pwStopEvt, 0) != WAIT_OBJECT_0)
    {
        ULONGLONG t0 = GetTickCount64();
        LONG alvo = g_pingAlvoIdx;
        ULONG addr = 0;
        char nome[48];
        int semGw = 0;
        double ultimo = NAN;
        ULONGLONG gasto;

        if (alvo < 0 || alvo >= PING_ALVOS)
            alvo = 1;

        if (alvo != lastAlvo)
        {
            lastAlvo = alvo;
            gwOk = 0;
            prevRtt = -1.0;
            jitter = soma = mn = mx = 0.0;
            env = rec = 0;
            jPos = jCount = 0;
            memset(janela, 0, sizeof(janela));
        }

        if (alvo == 0)
        {
            if (!gwOk || (iter % 15) == 0)
                gwOk = PingObterGateway(&gwAddr, gwTxt, sizeof(gwTxt));
            if (gwOk)
            {
                addr = gwAddr;
                snprintf(nome, sizeof(nome), "Gateway %s", gwTxt);
            }
            else
            {
                semGw = 1;
                snprintf(nome, sizeof(nome), "Gateway (nao encontrado)");
            }
        }
        else
        {
            addr = (ULONG)(g_pingAlvoIp[alvo][0] | (g_pingAlvoIp[alvo][1] << 8) |
                           (g_pingAlvoIp[alvo][2] << 16) | ((ULONG)g_pingAlvoIp[alvo][3] << 24));
            snprintf(nome, sizeof(nome), "%s", g_pingAlvoNome[alvo]);
        }

        if (!semGw)
        {
            int perdido = 1;

            if (hIcmp != INVALID_HANDLE_VALUE && hIcmp != NULL)
            {
                union
                {
                    WmIcmpReply r;
                    BYTE b[256];
                } rep;
                DWORD n;

                memset(&rep, 0, sizeof(rep));
                n = fSend(hIcmp, addr, (LPVOID)payload, (WORD)sizeof(payload),
                          NULL, rep.b, (DWORD)sizeof(rep.b), 1000);
                if (n > 0 && rep.r.Status == 0)
                {
                    double rtt = (double)rep.r.RoundTripTime;

                    perdido = 0;
                    rec++;
                    soma += rtt;
                    if (rec == 1 || rtt < mn)
                        mn = rtt;
                    if (rec == 1 || rtt > mx)
                        mx = rtt;
                    if (prevRtt >= 0.0)
                        jitter += (fabs(rtt - prevRtt) - jitter) / 16.0;
                    prevRtt = rtt;
                    ultimo = rtt;
                }
            }

            env++;
            if (perdido)
                ultimo = -1.0;
            janela[jPos] = (BYTE)perdido;
            jPos = (jPos + 1) % PING_JANELA;
            if (jCount < PING_JANELA)
                jCount++;
        }

        EnterCriticalSection(&g_pwCs);
        g_pingEst.ativo = 1;
        g_pingEst.alvo = (int)alvo;
        g_pingEst.semGateway = semGw;
        snprintf(g_pingEst.alvoNome, sizeof(g_pingEst.alvoNome), "%s", nome);
        g_pingEst.ultimoMs = ultimo;
        g_pingEst.minMs = mn;
        g_pingEst.maxMs = mx;
        g_pingEst.mediaMs = rec ? soma / (double)rec : 0.0;
        g_pingEst.jitterMs = jitter;
        g_pingEst.enviados = env;
        g_pingEst.recebidos = rec;
        {
            int perdas = 0, j;
            for (j = 0; j < jCount; j++)
                perdas += janela[j];
            g_pingEst.perdaJanela = jCount ? 100.0 * (double)perdas / (double)jCount : 0.0;
        }
        LeaveCriticalSection(&g_pwCs);

        if ((iter % 2) == 0)
        {
            WifiEstado w;
            WifiConsultar(&w);
            EnterCriticalSection(&g_pwCs);
            g_wifiEst = w;
            LeaveCriticalSection(&g_pwCs);
        }
        iter++;

        gasto = GetTickCount64() - t0;
        if (gasto < PING_INTERVALO_MS)
            WaitForSingleObject(g_pwStopEvt, (DWORD)(PING_INTERVALO_MS - gasto));
    }

    if (hIcmp != INVALID_HANDLE_VALUE && hIcmp != NULL && fClose)
        fClose(hIcmp);
    return 0;
}

static void PingWifiIniciar(void)
{
    if (g_pwThread)
        return;
    if (!g_pwCsInit)
    {
        InitializeCriticalSection(&g_pwCs);
        g_pwCsInit = 1;
    }
    memset(&g_pingEst, 0, sizeof(g_pingEst));
    g_pingEst.ultimoMs = NAN;
    memset(&g_wifiEst, 0, sizeof(g_wifiEst));
    PingCarregarAlvo();

    g_pwStopEvt = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_pwStopEvt)
        return;
    g_pwThread = CreateThread(NULL, 0, PingWifiThread, NULL, 0, NULL);
    if (!g_pwThread)
    {
        CloseHandle(g_pwStopEvt);
        g_pwStopEvt = NULL;
    }
}

static void PingWifiParar(void)
{
    if (!g_pwThread)
        return;
    SetEvent(g_pwStopEvt);
    WaitForSingleObject(g_pwThread, 3000);
    CloseHandle(g_pwThread);
    g_pwThread = NULL;
    CloseHandle(g_pwStopEvt);
    g_pwStopEvt = NULL;
}

static void PingWifiObter(PingEstado *p, WifiEstado *w)
{
    if (!g_pwCsInit)
    {
        memset(p, 0, sizeof(*p));
        p->ultimoMs = NAN;
        memset(w, 0, sizeof(*w));
        return;
    }
    EnterCriticalSection(&g_pwCs);
    *p = g_pingEst;
    *w = g_wifiEst;
    LeaveCriticalSection(&g_pwCs);
}

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
static HWND hBtnGraphPNG = NULL;
static HWND hCompact = NULL;

#define COMPACT_WIDTH 360
#define COMPACT_HEIGHT 160
static int g_compactMode = 0;
static LONG_PTR g_normalStyle = 0;
static LONG_PTR g_normalExStyle = 0;
static RECT g_normalRect = {0};

typedef struct
{
    ULONGLONG tickMs;
    SYSTEMTIME timestamp;
    double cpu, ram, temp;
} WinMonSample;

typedef struct
{
    ULONGLONG tickMs;
    SYSTEMTIME timestamp;
    char texto[128];
} WinMonMarker;

typedef struct
{
    ULONGLONG inicioTickMs;
    unsigned long amostras;
    double somaCpu, somaRam, somaTemp;
    double maxCpu, maxRam, maxTemp;
    double minCpu, minRam, minTemp;
    unsigned long alertas;
} WinMonSessionStats;

static WinMonSessionStats g_session = {0};
static WinMonSample *g_sessionSamples = NULL;
static size_t g_sessionSampleCount = 0;
static WinMonMarker g_sessionMarkers[SESSION_MAX_MARKERS];
static size_t g_sessionMarkerCount = 0;
static CRITICAL_SECTION g_sessionLock;
static int g_sessionLockReady = 0;

#define PROCESS_HISTORY_INVALID (NAN)
static int g_processHistoryPos = -1;
static int g_processHistoryCount = 0;

typedef struct
{
    int visible;
    int endOffset;
    int dragging;
    int dragX;
    int dragStartEnd;
    int hoverX;
    int isHovering;
} GraphViewState;

static GraphViewState g_graphViews[7] = {
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0},
    {GRAPH_DEFAULT_VISIBLE, 0, 0, 0, 0, 0, 0}};

static int ovMonitorIndex = 0;

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

static double ObterTemperaturaMaxima(void)
{
    double maxTemp = 0.0;
    int i;
    for (i = 0; i < numZonasTemp; i++)
        if (tempAtual[i] > maxTemp)
            maxTemp = tempAtual[i];
    return maxTemp;
}

#define ALERTAS_FICHEIRO "winmon_alertas.csv"
static unsigned g_alertHistoryRev = 0; /* incrementa a cada alteracao (refresco do dialogo) */

/* Caminho absoluto na pasta do executavel (evita depender do diretorio de trabalho) */
static void CaminhoDados(const char *nome, char *out, size_t n)
{
    char *p;
    DWORD len = GetModuleFileNameA(NULL, out, (DWORD)n);
    if (len == 0 || len >= n)
    {
        snprintf(out, n, "%s", nome);
        return;
    }
    p = strrchr(out, '\\');
    if (p)
        *(p + 1) = '\0';
    else
        out[0] = '\0';
    strncat_s(out, n, nome, _TRUNCATE);
}

static void AlertaAdicionarMemoria(const AlertaHistoricoItem *novo)
{
    if (g_alertHistoryCount >= ALERT_HISTORY_MAX)
    {
        memmove(&g_alertHistory[0], &g_alertHistory[1],
                sizeof(g_alertHistory[0]) * (ALERT_HISTORY_MAX - 1));
        g_alertHistoryCount = ALERT_HISTORY_MAX - 1;
    }
    g_alertHistory[g_alertHistoryCount++] = *novo;
    g_alertHistoryRev++;
}

static void AlertaGuardarNoFicheiro(const AlertaHistoricoItem *it)
{
    char caminho[MAX_PATH];
    FILE *f = NULL;
    long tam;

    CaminhoDados(ALERTAS_FICHEIRO, caminho, sizeof(caminho));
    if (fopen_s(&f, caminho, "a") != 0 || !f)
        return;

    fseek(f, 0, SEEK_END);
    tam = ftell(f);
    if (tam <= 0)
        fprintf(f, "timestamp,recurso,valor,limite,culpado\n");

    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d,%s,%.2f,%.2f,%s\n",
            it->timestamp.wYear, it->timestamp.wMonth, it->timestamp.wDay,
            it->timestamp.wHour, it->timestamp.wMinute, it->timestamp.wSecond,
            it->recurso, it->valor, it->limite, it->culpado);
    fflush(f);
    fclose(f);
}

/* ------------------------------------------------------------------------- */
