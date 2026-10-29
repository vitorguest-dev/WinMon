/* 06 local network */
/* ========================================================================= */
/* Painel Rede Local: Dispositivos (ARP ativo), Traceroute e Teste de DNS    */
/* ========================================================================= */

#define REDE_MODO_LAN 0
#define REDE_MODO_TRACE 1
#define REDE_MODO_DNS 2
#define LAN_THREADS 48
#define LAN_REG_CHAVE "Software\\WinMon\\RedeLocal"
#define TRACE_MAX_SALTOS 30
#define TRACE_SONDAS 3
#define TRACE_TIMEOUT_MS 1500
#define DNS_MAX_SERVIDORES 16
#define DNS_TENTATIVAS 5
/* BYPASS_CACHE | NO_HOSTS_FILE | WIRE_ONLY: mede mesmo o servidor, nao a cache local */
#define DNS_OPCOES_CONSULTA (0x00000008u | 0x00000040u | 0x00000100u)

static int g_redeModo = REDE_MODO_LAN;
static int g_redeVisivel = 0;

static void RedeMostrarCtl(HWND h, int on)
{
    if (h)
        ShowWindow(h, on ? SW_SHOW : SW_HIDE);
}

static HWND RedeCriarCtl(HWND pai, const char *cls, const char *txt, DWORD estilo, DWORD estiloEx, int id)
{
    HWND h = CreateWindowExA(estiloEx, cls, txt, WS_CHILD | estilo, 0, 0, 100, 24, pai,
                             (HMENU)(UINT_PTR)id, GetModuleHandle(NULL), NULL);
    if (h && hFontUI)
        SendMessage(h, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return h;
}

static HWND RedeCriarLista(HWND pai, int id)
{
    HWND h = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                             WS_CHILD | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             0, 0, 100, 100, pai, (HMENU)(UINT_PTR)id, GetModuleHandle(NULL), NULL);
    if (!h)
        return NULL;
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(h, RGB(255, 255, 255));
    ListView_SetTextBkColor(h, RGB(255, 255, 255));
    ListView_SetTextColor(h, RGB(42, 48, 56));
    if (hFontUI)
        SendMessage(h, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return h;
}

static void RedeAddCol(HWND lv, int idx, const char *nome, int larg, int fmt)
{
    LVCOLUMNA col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = fmt;
    col.pszText = (LPSTR)nome;
    col.cx = larg;
    ListView_InsertColumn(lv, idx, &col);
}

static int RedeAddLinha(HWND lv, int row, const char *col0)
{
    LVITEMA it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.pszText = (LPSTR)col0;
    return ListView_InsertItem(lv, &it);
}

/* ------------------------------------------------------------------------- */
/* Fabricantes (OUI)                                                         */
/* ------------------------------------------------------------------------- */

typedef struct
{
    BYTE oui[3];
    const char *nome;
} OuiBase;

/* Tabela minima (maquinas virtuais, Raspberry Pi, ESP). Para cobertura completa,
 * colocar um ficheiro "oui.txt" ao lado do .exe (ver OuiCarregarExterno). */
static const OuiBase g_ouiBase[] = {
    {{0x00, 0x0C, 0x29}, "VMware"},
    {{0x00, 0x50, 0x56}, "VMware"},
    {{0x00, 0x05, 0x69}, "VMware"},
    {{0x08, 0x00, 0x27}, "VirtualBox"},
    {{0x00, 0x15, 0x5D}, "Microsoft Hyper-V"},
    {{0x52, 0x54, 0x00}, "QEMU/KVM"},
    {{0xB8, 0x27, 0xEB}, "Raspberry Pi"},
    {{0xDC, 0xA6, 0x32}, "Raspberry Pi"},
    {{0xE4, 0x5F, 0x01}, "Raspberry Pi"},
    {{0xD8, 0x3A, 0xDD}, "Raspberry Pi"},
    {{0x2C, 0xCF, 0x67}, "Raspberry Pi"},
    {{0x24, 0x0A, 0xC4}, "Espressif"},
    {{0x30, 0xAE, 0xA4}, "Espressif"},
    {{0x24, 0x6F, 0x28}, "Espressif"},
    {{0xA4, 0xCF, 0x12}, "Espressif"},
    {{0x00, 0x00, 0x0C}, "Cisco"}};

typedef struct
{
    DWORD oui;
    char nome[40];
} OuiExt;

static OuiExt *g_ouiExt = NULL;
static int g_ouiExtN = 0;
static int g_ouiExtTentado = 0;

/* Carrega "oui.txt" (junto ao .exe), uma vez, na thread da interface.
 * Formatos aceites: "AABBCC Fabricante", "AA-BB-CC Fabricante" ou o oui.txt da IEEE
 * ("AA-BB-CC   (hex)   Fabricante"). */
static void OuiCarregarExterno(void)
{
    char path[MAX_PATH];
    char linha[256];
    char *p;
    FILE *f = NULL;
    int cap = 0;

    if (g_ouiExtTentado)
        return;
    g_ouiExtTentado = 1;
    if (!GetModuleFileNameA(NULL, path, sizeof(path)))
        return;
    p = strrchr(path, '\\');
    if (!p)
        return;
    *(p + 1) = '\0';
    if (strlen(path) + 8 >= sizeof(path))
        return;
    strcat_s(path, sizeof(path), "oui.txt");
    if (fopen_s(&f, path, "r") != 0 || !f)
        return;

    while (fgets(linha, sizeof(linha), f) && g_ouiExtN < 60000)
    {
        const char *s = linha;
        DWORD v = 0;
        int dig = 0;
        size_t len;

        while (*s == ' ' || *s == '\t')
            s++;
        while (dig < 6)
        {
            if (isxdigit((unsigned char)*s))
            {
                int c = tolower((unsigned char)*s);
                v = (v << 4) | (DWORD)(c <= '9' ? c - '0' : c - 'a' + 10);
                dig++;
                s++;
            }
            else if ((*s == '-' || *s == ':') && dig > 0 && dig % 2 == 0)
                s++;
            else
                break;
        }
        if (dig != 6 || isxdigit((unsigned char)*s))
            continue;
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';')
            s++;
        if (strncmp(s, "(hex)", 5) == 0)
            s += 5;
        else if (*s == '(')
            continue; /* linhas "(base 16)" duplicam as "(hex)" */
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';')
            s++;
        len = strcspn(s, "\r\n");
        if (len == 0)
            continue;
        if (g_ouiExtN >= cap)
        {
            int ncap = cap ? cap * 2 : 256;
            OuiExt *nv = (OuiExt *)realloc(g_ouiExt, (size_t)ncap * sizeof(OuiExt));
            if (!nv)
                break;
            g_ouiExt = nv;
            cap = ncap;
        }
        g_ouiExt[g_ouiExtN].oui = v;
        if (len >= sizeof(g_ouiExt[0].nome))
            len = sizeof(g_ouiExt[0].nome) - 1;
        memcpy(g_ouiExt[g_ouiExtN].nome, s, len);
        g_ouiExt[g_ouiExtN].nome[len] = '\0';
        g_ouiExtN++;
    }
    fclose(f);
}

/* So le dados ja carregados: pode ser chamada pelas threads de pesquisa. */
static void OuiProcurar(const BYTE *mac, char *out, size_t n)
{
    DWORD v = ((DWORD)mac[0] << 16) | ((DWORD)mac[1] << 8) | (DWORD)mac[2];
    size_t i;
    int k;

    for (k = 0; k < g_ouiExtN; k++)
    {
        if (g_ouiExt[k].oui == v)
        {
            strncpy_s(out, n, g_ouiExt[k].nome, _TRUNCATE);
            return;
        }
    }
    for (i = 0; i < sizeof(g_ouiBase) / sizeof(g_ouiBase[0]); i++)
    {
        if (g_ouiBase[i].oui[0] == mac[0] && g_ouiBase[i].oui[1] == mac[1] && g_ouiBase[i].oui[2] == mac[2])
        {
            strncpy_s(out, n, g_ouiBase[i].nome, _TRUNCATE);
            return;
        }
    }
    /* bit "administrado localmente": telemoveis com MAC aleatorio, VMs, etc. */
    if (mac[0] & 0x02)
        strncpy_s(out, n, "MAC privado/aleatorio", _TRUNCATE);
    else
        strncpy_s(out, n, "Desconhecido", _TRUNCATE);
}

/* ------------------------------------------------------------------------- */
/* Dispositivos conhecidos (persistidos no Registry)                         */
/* ------------------------------------------------------------------------- */

/* Formato: ";AA-BB-CC-DD-EE-FF;11-22-33-44-55-66;" */
static char *LanLerConhecidos(void)
{
    HKEY hk;
    DWORD sz = 0, t = 0;
    char *buf = NULL;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, LAN_REG_CHAVE, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hk, "Conhecidos", NULL, &t, NULL, &sz) == ERROR_SUCCESS &&
            t == REG_SZ && sz > 1 && sz < (1u << 20))
        {
            buf = (char *)malloc((size_t)sz + 2);
            if (buf)
            {
                DWORD sz2 = sz;
                if (RegQueryValueExA(hk, "Conhecidos", NULL, &t, (BYTE *)buf, &sz2) != ERROR_SUCCESS)
                {
                    free(buf);
                    buf = NULL;
                }
                else
                    buf[sz] = '\0';
            }
        }
        RegCloseKey(hk);
    }
    if (!buf)
    {
        buf = (char *)malloc(2);
        if (buf)
        {
            buf[0] = ';';
            buf[1] = '\0';
        }
    }
    return buf;
}

static int LanBaselineFeito(void)
{
    HKEY hk;
    DWORD v = 0, sz = sizeof(v), t = 0;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, LAN_REG_CHAVE, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(hk, "BaseCriada", NULL, &t, (BYTE *)&v, &sz) != ERROR_SUCCESS || t != REG_DWORD)
            v = 0;
        RegCloseKey(hk);
    }
    return v != 0;
}

static void LanGuardarConhecidos(const char *lista)
{
    HKEY hk;
    DWORD um = 1;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, LAN_REG_CHAVE, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS)
    {
        RegSetValueExA(hk, "Conhecidos", 0, REG_SZ, (const BYTE *)lista, (DWORD)strlen(lista) + 1);
        RegSetValueExA(hk, "BaseCriada", 0, REG_DWORD, (const BYTE *)&um, sizeof(um));
        RegCloseKey(hk);
    }
}

static int LanConhecido(const char *lista, const char *mac)
{
    char chave[32];
    snprintf(chave, sizeof(chave), ";%s;", mac);
    return lista && strstr(lista, chave) != NULL;
}

/* Junta todos os dispositivos atuais (exceto este PC) a lista de conhecidos. */
static void LanAdicionarConhecidos(void)
{
    char *atual = LanLerConhecidos();
    char *nova;
    size_t cap;
    int i;

    if (!atual)
        return;
    cap = strlen(atual) + (size_t)g_lanDeviceCount * 20 + 4;
    nova = (char *)malloc(cap);
    if (!nova)
    {
        free(atual);
        return;
    }
    strcpy_s(nova, cap, atual);
    for (i = 0; i < g_lanDeviceCount; i++)
    {
        if (g_lanDevices[i].self || LanConhecido(nova, g_lanDevices[i].macText))
            continue;
        strcat_s(nova, cap, g_lanDevices[i].macText);
        strcat_s(nova, cap, ";");
    }
    LanGuardarConhecidos(nova);
    free(nova);
    free(atual);
}

/* ------------------------------------------------------------------------- */
/* Scanner ARP (varrimento paralelo da sub-rede, no maximo 254 enderecos)    */
/* ------------------------------------------------------------------------- */

typedef struct
{
    DWORD baseHost; /* primeiro endereco a testar (ordem do host) */
    DWORD count;
    DWORD selfIp; /* ordem de rede */
    volatile LONG next;
    LanDevice *out;
    int n;
    CRITICAL_SECTION cs;
} LanScanCtx;

static void LanFormatarIp(DWORD raw, char *dst, size_t n)
{
    snprintf(dst, n, "%u.%u.%u.%u", (unsigned)(raw & 0xff), (unsigned)((raw >> 8) & 0xff),
             (unsigned)((raw >> 16) & 0xff), (unsigned)((raw >> 24) & 0xff));
}

static void LanFormatarMac(const BYTE *mac, char *dst, size_t n)
{
    snprintf(dst, n, "%02X-%02X-%02X-%02X-%02X-%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static DWORD WINAPI LanWorker(LPVOID arg)
{
    LanScanCtx *c = (LanScanCtx *)arg;

    for (;;)
    {
        LONG idx = InterlockedIncrement(&c->next) - 1;
        DWORD target;
        ULONG macBuf[2] = {0, 0};
        ULONG macLen = 6;
        struct in_addr addr;
        const BYTE *mb = (const BYTE *)macBuf;

        if (idx < 0 || (DWORD)idx >= c->count)
            break;
        target = c->baseHost + (DWORD)idx;
        addr.s_addr = htonl(target);
        if (addr.s_addr == c->selfIp)
            continue;

        if (SendARP((IPAddr)addr.s_addr, (IPAddr)c->selfIp, macBuf, &macLen) == NO_ERROR && macLen >= 6 &&
            (mb[0] | mb[1] | mb[2] | mb[3] | mb[4] | mb[5]) != 0)
        {
            LanDevice d;
            struct sockaddr_in sa;

            memset(&d, 0, sizeof(d));
            d.ip = addr.s_addr;
            memcpy(d.mac, mb, 6);
            LanFormatarIp(d.ip, d.ipText, sizeof(d.ipText));
            LanFormatarMac(d.mac, d.macText, sizeof(d.macText));
            OuiProcurar(d.mac, d.vendor, sizeof(d.vendor));

            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr = addr;
            if (getnameinfo((const struct sockaddr *)&sa, (socklen_t)sizeof(sa), d.host, sizeof(d.host),
                            NULL, 0, NI_NAMEREQD) != 0 ||
                strcmp(d.host, d.ipText) == 0)
                d.host[0] = '\0';

            EnterCriticalSection(&c->cs);
            if (c->n < LAN_MAX_DEVICES)
                c->out[c->n++] = d;
            LeaveCriticalSection(&c->cs);
        }
    }
    return 0;
}

static int LanCompararIp(const void *a, const void *b)
{
    DWORD x = ntohl(((const LanDevice *)a)->ip);
    DWORD y = ntohl(((const LanDevice *)b)->ip);
    return (x > y) - (x < y);
}

static DWORD WINAPI LanScanThread(LPVOID arg)
{
    HWND hwndPai = (HWND)arg;
    IP_ADAPTER_INFO *adapters = NULL, *ad;
    ULONG bytes = 0;
    LanScanCtx ctx;
    HANDLE th[LAN_THREADS];
    int nth = 0, i, ok = 0, csPronto = 0;
    DWORD selfIp = 0, base = 0, count = 0;
    BYTE selfMac[6] = {0};

    memset(&ctx, 0, sizeof(ctx));

    if (GetAdaptersInfo(NULL, &bytes) != ERROR_BUFFER_OVERFLOW || bytes == 0 || bytes > 1024 * 1024)
        goto finish;
    adapters = (IP_ADAPTER_INFO *)malloc(bytes);
    if (!adapters || GetAdaptersInfo(adapters, &bytes) != NO_ERROR)
        goto finish;

    /* primeiro adaptador IPv4 ligado e com gateway */
    for (ad = adapters; ad; ad = ad->Next)
    {
        DWORD ip, mask, m;
        unsigned prefix = 0;

        if (ad->Type == MIB_IF_TYPE_LOOPBACK)
            continue;
        if (ad->IpAddressList.IpAddress.String[0] == '\0' || ad->GatewayList.IpAddress.String[0] == '\0')
            continue;
        ip = ntohl(inet_addr(ad->IpAddressList.IpAddress.String));
        mask = ntohl(inet_addr(ad->IpAddressList.IpMask.String));
        if (ip == 0 || mask == 0)
            continue;
        for (m = mask; m & 0x80000000UL; m <<= 1)
            prefix++;
        if (prefix < 24)
        {
            base = ip & 0xFFFFFF00UL;
            count = 256;
        }
        else
        {
            base = ip & mask;
            count = (prefix >= 31) ? 0 : (1UL << (32 - prefix));
        }
        if (count < 4)
            continue;
        selfIp = htonl(ip);
        if (ad->AddressLength >= 6)
            memcpy(selfMac, ad->Address, 6);
        ok = 1;
        break;
    }
    if (!ok)
        goto finish;

    ctx.baseHost = base + 1; /* exclui endereco de rede e de broadcast */
    ctx.count = count - 2;
    ctx.selfIp = selfIp;
    ctx.out = (LanDevice *)calloc(LAN_MAX_DEVICES, sizeof(LanDevice));
    if (!ctx.out)
        goto finish;
    InitializeCriticalSection(&ctx.cs);
    csPronto = 1;

    /* este PC aparece sempre na lista */
    {
        LanDevice *d = &ctx.out[ctx.n++];
        DWORD tam = (DWORD)sizeof(d->host);
        d->self = 1;
        d->ip = selfIp;
        memcpy(d->mac, selfMac, 6);
        LanFormatarIp(d->ip, d->ipText, sizeof(d->ipText));
        LanFormatarMac(d->mac, d->macText, sizeof(d->macText));
        OuiProcurar(d->mac, d->vendor, sizeof(d->vendor));
        GetComputerNameA(d->host, &tam);
        strcpy_s(d->estado, sizeof(d->estado), "Este PC");
    }

    for (i = 0; i < LAN_THREADS && (DWORD)i < ctx.count; i++)
    {
        HANDLE h = CreateThread(NULL, 128 * 1024, LanWorker, &ctx, 0, NULL);
        if (h)
            th[nth++] = h;
    }
    if (nth == 0)
        LanWorker(&ctx);
    else
    {
        WaitForMultipleObjects((DWORD)nth, th, TRUE, INFINITE);
        for (i = 0; i < nth; i++)
            CloseHandle(th[i]);
    }

    qsort(ctx.out, (size_t)ctx.n, sizeof(LanDevice), LanCompararIp);
    memcpy(g_lanDevices, ctx.out, (size_t)ctx.n * sizeof(LanDevice));
    g_lanDeviceCount = ctx.n;

finish:
    if (csPronto)
        DeleteCriticalSection(&ctx.cs);
    free(ctx.out);
    free(adapters);
    InterlockedExchange(&g_lanScanRunning, 0);
    if (hwndPai)
        PostMessage(hwndPai, WM_LAN_SCAN_DONE, 0, 0);
    return 0;
}

static void LanPreencherLista(void)
{
    int i;

    if (!hLanList)
        return;
    ListView_DeleteAllItems(hLanList);
    for (i = 0; i < g_lanDeviceCount; i++)
    {
        int row = RedeAddLinha(hLanList, i, g_lanDevices[i].ipText);
        if (row >= 0)
        {
            ListView_SetItemText(hLanList, row, 1, g_lanDevices[i].macText);
            ListView_SetItemText(hLanList, row, 2, g_lanDevices[i].vendor);
            ListView_SetItemText(hLanList, row, 3, g_lanDevices[i].host);
            ListView_SetItemText(hLanList, row, 4, g_lanDevices[i].estado);
        }
    }
}

static void LanScanConcluido(void)
{
    char *known = LanLerConhecidos();
    int baseFeita = LanBaselineFeito();
    int i, novos = 0, outros = 0;
    char primeiro[96] = "";
    char status[200];

    if (hLanScan)
    {
        EnableWindow(hLanScan, TRUE);
        SetWindowTextA(hLanScan, "Procurar dispositivos");
    }

    for (i = 0; i < g_lanDeviceCount; i++)
    {
        LanDevice *d = &g_lanDevices[i];
        if (d->self)
            continue;
        outros++;
        if (!baseFeita || LanConhecido(known, d->macText))
            strcpy_s(d->estado, sizeof(d->estado), "Conhecido");
        else
        {
            strcpy_s(d->estado, sizeof(d->estado), "NOVO");
            if (novos == 0)
                snprintf(primeiro, sizeof(primeiro), "%s (%s)", d->ipText, d->macText);
            novos++;
        }
    }

    LanPreencherLista();

    if (outros == 0)
    {
        snprintf(status, sizeof(status), "Nenhum dispositivo encontrado (verifique a ligacao a rede).");
        if (hLanAceitar)
            EnableWindow(hLanAceitar, FALSE);
    }
    else if (!baseFeita)
    {
        LanAdicionarConhecidos();
        snprintf(status, sizeof(status),
                 "Primeira pesquisa: %d dispositivo(s) guardados como conhecidos. Os proximos serao assinalados como NOVO.",
                 outros);
        if (hLanAceitar)
            EnableWindow(hLanAceitar, FALSE);
    }
    else if (novos > 0)
    {
        char msg[160];
        snprintf(status, sizeof(status), "%d dispositivo(s), %d NOVO(s). Use \"Marcar como conhecidos\" depois de verificar.",
                 outros, novos);
        snprintf(msg, sizeof(msg), "%d novo(s) dispositivo(s) na rede local, p. ex. %s", novos, primeiro);
        EnviarBalloon("WinMon - Novo dispositivo na rede", msg);
        if (hLanAceitar)
            EnableWindow(hLanAceitar, TRUE);
    }
    else
    {
        snprintf(status, sizeof(status), "%d dispositivo(s) encontrados, todos conhecidos.", outros);
        if (hLanAceitar)
            EnableWindow(hLanAceitar, FALSE);
    }
    if (hLanStatus)
        SetWindowTextA(hLanStatus, status);
    free(known);
}

static void LanAceitarNovos(void)
{
    int i;

    LanAdicionarConhecidos();
    for (i = 0; i < g_lanDeviceCount; i++)
        if (!g_lanDevices[i].self)
            strcpy_s(g_lanDevices[i].estado, sizeof(g_lanDevices[i].estado), "Conhecido");
    LanPreencherLista();
    if (hLanAceitar)
        EnableWindow(hLanAceitar, FALSE);
    if (hLanStatus)
        SetWindowTextA(hLanStatus, "Dispositivos atuais marcados como conhecidos.");
}

static void LanIniciarPesquisa(HWND hwndPai)
{
    DWORD tid;

    if (InterlockedCompareExchange(&g_lanScanRunning, 1, 0) != 0)
        return;
    OuiCarregarExterno();
    if (hLanScan)
    {
        EnableWindow(hLanScan, FALSE);
        SetWindowTextA(hLanScan, "A procurar...");
    }
    if (hLanAceitar)
        EnableWindow(hLanAceitar, FALSE);
    if (hLanStatus)
        SetWindowTextA(hLanStatus, "A fazer ARP a sub-rede local (ate 254 enderecos, pode demorar alguns segundos)...");
    g_lanDeviceCount = 0;
    if (hLanList)
        ListView_DeleteAllItems(hLanList);
    g_lanThread = CreateThread(NULL, 0, LanScanThread, hwndPai, 0, &tid);
    if (!g_lanThread)
    {
        InterlockedExchange(&g_lanScanRunning, 0);
        if (hLanScan)
        {
            EnableWindow(hLanScan, TRUE);
            SetWindowTextA(hLanScan, "Procurar dispositivos");
        }
        if (hLanStatus)
            SetWindowTextA(hLanStatus, "Nao foi possivel iniciar a pesquisa.");
    }
    else
        CloseHandle(g_lanThread);
    g_lanThread = NULL;
}

/* ------------------------------------------------------------------------- */
/* Traceroute (ICMP com TTL crescente, 3 sondas por salto)                   */
/* ------------------------------------------------------------------------- */

typedef struct
{
    HWND hwnd;
    char host[128];
    int resolver;
} TraceArg;

typedef struct
{
    int hop;
    int atingido;
    int soNome; /* 1: apenas atualiza o nome de um salto ja listado */
    char ip[16];
    char nome[96];
    int ms[TRACE_SONDAS]; /* <0: sem resposta */
} TraceHopMsg;

static volatile LONG g_traceRunning = 0;
static volatile LONG g_traceCancel = 0;

static DWORD WINAPI TraceThread(LPVOID p)
{
    TraceArg a = *(TraceArg *)p;
    struct addrinfo hints, *res = NULL;
    DWORD dest = 0;
    HANDLE icmp = INVALID_HANDLE_VALUE;
    DWORD addrs[TRACE_MAX_SALTOS + 1];
    const char payload[] = "WinMon traceroute";
    int hop, s, ultimo = 0, atingido = 0, codigo = 0;

    free(p);
    memset(addrs, 0, sizeof(addrs));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;

    if (getaddrinfo(a.host, NULL, &hints, &res) != 0 || !res)
    {
        codigo = 1;
        goto fim;
    }
    dest = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    res = NULL;

    icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
    {
        codigo = 2;
        goto fim;
    }

    for (hop = 1; hop <= TRACE_MAX_SALTOS && !atingido && codigo == 0; hop++)
    {
        TraceHopMsg *m = (TraceHopMsg *)calloc(1, sizeof(*m));
        DWORD hopAddr = 0;
        int parar = 0;

        if (!m)
            break;
        m->hop = hop;
        for (s = 0; s < TRACE_SONDAS; s++)
        {
            union
            {
                ICMP_ECHO_REPLY r;
                BYTE b[sizeof(ICMP_ECHO_REPLY) + 128];
            } buf;
            IP_OPTION_INFORMATION opt;
            DWORD n;

            m->ms[s] = -1;
            if (InterlockedCompareExchange(&g_traceCancel, 0, 0))
            {
                codigo = 3;
                break;
            }
            memset(&opt, 0, sizeof(opt));
            opt.Ttl = (UCHAR)hop;
            memset(&buf, 0, sizeof(buf));
            n = IcmpSendEcho(icmp, dest, (LPVOID)payload, (WORD)sizeof(payload), &opt,
                             buf.b, (DWORD)sizeof(buf.b), TRACE_TIMEOUT_MS);
            if (n > 0)
            {
                ULONG st = buf.r.Status;
                if (st == IP_SUCCESS || st == IP_TTL_EXPIRED_TRANSIT)
                {
                    m->ms[s] = (int)buf.r.RoundTripTime;
                    hopAddr = buf.r.Address;
                    if (st == IP_SUCCESS)
                        atingido = 1;
                }
                else
                {
                    /* destino/rede inacessivel anunciado por um router */
                    hopAddr = buf.r.Address;
                    parar = 1;
                }
            }
        }
        if (codigo == 3)
        {
            free(m);
            break;
        }
        if (hopAddr)
            LanFormatarIp(hopAddr, m->ip, sizeof(m->ip));
        else
            strcpy_s(m->ip, sizeof(m->ip), "*");
        m->atingido = atingido;
        addrs[hop] = hopAddr;
        ultimo = hop;
        if (!PostMessage(a.hwnd, WM_TRACE_HOP, 0, (LPARAM)m))
            free(m);
        if (parar)
            codigo = 5;
    }
    if (codigo == 0 && !atingido)
        codigo = 4;

    /* nomes (DNS inverso) numa segunda passagem, para nao atrasar as medicoes */
    if (a.resolver)
    {
        for (hop = 1; hop <= ultimo && !InterlockedCompareExchange(&g_traceCancel, 0, 0); hop++)
        {
            struct sockaddr_in sa;
            char nome[96];

            if (!addrs[hop])
                continue;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_addr.s_addr = addrs[hop];
            if (getnameinfo((const struct sockaddr *)&sa, (socklen_t)sizeof(sa), nome, sizeof(nome),
                            NULL, 0, NI_NAMEREQD) == 0)
            {
                TraceHopMsg *m = (TraceHopMsg *)calloc(1, sizeof(*m));
                if (!m)
                    continue;
                m->hop = hop;
                m->soNome = 1;
                strncpy_s(m->nome, sizeof(m->nome), nome, _TRUNCATE);
                if (!PostMessage(a.hwnd, WM_TRACE_HOP, 0, (LPARAM)m))
                    free(m);
            }
        }
    }

fim:
    if (icmp != INVALID_HANDLE_VALUE)
        IcmpCloseHandle(icmp);
    if (res)
        freeaddrinfo(res);
    InterlockedExchange(&g_traceRunning, 0);
    PostMessage(a.hwnd, WM_TRACE_DONE, (WPARAM)codigo, (LPARAM)atingido);
    return 0;
}

static void TraceFormatarMs(int ms, char *dst, size_t n)
{
    if (ms < 0)
        strncpy_s(dst, n, "*", _TRUNCATE);
    else if (ms == 0)
        strncpy_s(dst, n, "<1 ms", _TRUNCATE);
    else
        snprintf(dst, n, "%d ms", ms);
}

static void TraceMostrarHop(const TraceHopMsg *m)
{
    int row, s, soma = 0, ok = 0;
    char txt[32];

    if (!hTraceList)
        return;
    if (m->soNome)
    {
        ListView_SetItemText(hTraceList, m->hop - 1, 2, (LPSTR)m->nome);
        return;
    }
    snprintf(txt, sizeof(txt), "%d", m->hop);
    row = RedeAddLinha(hTraceList, ListView_GetItemCount(hTraceList), txt);
    if (row < 0)
        return;
    ListView_SetItemText(hTraceList, row, 1, (LPSTR)m->ip);
    for (s = 0; s < TRACE_SONDAS; s++)
    {
        TraceFormatarMs(m->ms[s], txt, sizeof(txt));
        ListView_SetItemText(hTraceList, row, 3 + s, txt);
        if (m->ms[s] >= 0)
        {
            soma += m->ms[s];
            ok++;
        }
    }
    if (ok > 0)
        snprintf(txt, sizeof(txt), "%.1f ms", (double)soma / (double)ok);
    else
        strncpy_s(txt, sizeof(txt), "*", _TRUNCATE);
    ListView_SetItemText(hTraceList, row, 6, txt);
    snprintf(txt, sizeof(txt), "%d%%", (TRACE_SONDAS - ok) * 100 / TRACE_SONDAS);
    ListView_SetItemText(hTraceList, row, 7, txt);
}

static void TraceIniciarOuParar(HWND hwnd)
{
    TraceArg *a;
    DWORD tid;
    HANDLE h;
    char host[128];
    char *ini;
    size_t len;

    if (InterlockedCompareExchange(&g_traceRunning, 0, 0))
    {
        InterlockedExchange(&g_traceCancel, 1);
        if (hTraceStatus)
            SetWindowTextA(hTraceStatus, "A interromper...");
        return;
    }
    if (!hTraceHost)
        return;
    GetWindowTextA(hTraceHost, host, sizeof(host));
    ini = host;
    while (*ini == ' ' || *ini == '\t')
        ini++;
    len = strlen(ini);
    while (len > 0 && (ini[len - 1] == ' ' || ini[len - 1] == '\t'))
        ini[--len] = '\0';
    if (len == 0)
    {
        if (hTraceStatus)
            SetWindowTextA(hTraceStatus, "Indique um endereco IP ou nome de host.");
        return;
    }
    a = (TraceArg *)calloc(1, sizeof(*a));
    if (!a)
        return;
    a->hwnd = hwnd;
    a->resolver = (hTraceNomes && Button_GetCheck(hTraceNomes) == BST_CHECKED) ? 1 : 0;
    strncpy_s(a->host, sizeof(a->host), ini, _TRUNCATE);

    if (hTraceList)
        ListView_DeleteAllItems(hTraceList);
    InterlockedExchange(&g_traceCancel, 0);
    InterlockedExchange(&g_traceRunning, 1);
    h = CreateThread(NULL, 0, TraceThread, a, 0, &tid);
    if (!h)
    {
        InterlockedExchange(&g_traceRunning, 0);
        free(a);
        if (hTraceStatus)
            SetWindowTextA(hTraceStatus, "Nao foi possivel iniciar o traceroute.");
        return;
    }
    CloseHandle(h);
    if (hTraceGo)
        SetWindowTextA(hTraceGo, "Parar");
    if (hTraceStatus)
        SetWindowTextA(hTraceStatus, "A tracar a rota...");
}

static void TraceConcluido(int codigo)
{
    char status[160];
    int saltos = hTraceList ? ListView_GetItemCount(hTraceList) : 0;

    if (hTraceGo)
        SetWindowTextA(hTraceGo, "Tracar rota");
    switch (codigo)
    {
    case 0:
        snprintf(status, sizeof(status), "Destino alcancado em %d salto(s).", saltos);
        break;
    case 1:
        snprintf(status, sizeof(status), "Nao foi possivel resolver o destino.");
        break;
    case 2:
        snprintf(status, sizeof(status), "Nao foi possivel abrir o canal ICMP.");
        break;
    case 3:
        snprintf(status, sizeof(status), "Interrompido apos %d salto(s).", saltos);
        break;
    case 4:
        snprintf(status, sizeof(status), "Destino nao alcancado em %d saltos (muitos routers nao respondem a ICMP).",
                 TRACE_MAX_SALTOS);
        break;
    default:
        snprintf(status, sizeof(status), "Um router indicou que o destino e inacessivel (salto %d).", saltos);
        break;
    }
    if (hTraceStatus)
        SetWindowTextA(hTraceStatus, status);
}

/* ------------------------------------------------------------------------- */
/* Teste de DNS (DnsQuery direto a cada servidor, sem cache local)           */
/* ------------------------------------------------------------------------- */

typedef LONG(WINAPI *PfnWmDnsQuery)(const char *, WORD, DWORD, void *, void **, void **);
typedef void(WINAPI *PfnWmDnsFree)(void *, int);

/* Espelho minimo de DNS_RECORD (registo A) para nao depender de windns.h/dnsapi.lib */
typedef struct WmDnsRec
{
    struct WmDnsRec *pNext;
    char *pName;
    WORD wType;
    WORD wDataLength;
    DWORD Flags;
    DWORD dwTtl;
    DWORD dwReserved;
    DWORD ipv4;
} WmDnsRec;

typedef struct
{
    DWORD cnt;
    DWORD addr[1];
} WmIp4Array;

typedef struct
{
    HWND hwnd;
    int idx;
    DWORD ip4;
    char dominio[128];
} DnsJob;

typedef struct
{
    int idx;
    int falhas;
    int tentativas;
    int ok;
    double primeira, media, mn, mx; /* ms; primeira/media <0: sem dados */
    char resposta[20];
} DnsResultado;

static PfnWmDnsQuery g_dnsQuery = NULL;
static PfnWmDnsFree g_dnsFree = NULL;
static int g_dnsPendentes = 0; /* so usado na thread da interface */
static int g_dnsNServ = 0;
static char g_dnsIpTxt[DNS_MAX_SERVIDORES][16];
static char g_dnsOrigem[DNS_MAX_SERVIDORES][24];
static DnsResultado g_dnsRes[DNS_MAX_SERVIDORES];

static int DnsCarregarApi(void)
{
    HMODULE h;

    if (g_dnsQuery && g_dnsFree)
        return 1;
    h = LoadLibraryExA("dnsapi.dll", NULL, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    if (!h)
        return 0;
    g_dnsQuery = (PfnWmDnsQuery)(void *)GetProcAddress(h, "DnsQuery_A");
    g_dnsFree = (PfnWmDnsFree)(void *)GetProcAddress(h, "DnsFree");
    return g_dnsQuery && g_dnsFree;
}

static DWORD WINAPI DnsThread(LPVOID p)
{
    DnsJob j = *(DnsJob *)p;
    DnsResultado *r = (DnsResultado *)calloc(1, sizeof(*r));
    LARGE_INTEGER freq;
    double soma = 0.0;
    int t;

    free(p);
    if (!r)
        return 0;
    r->idx = j.idx;
    r->primeira = -1.0;
    r->media = -1.0;
    r->mn = 1e9;
    r->mx = 0.0;
    QueryPerformanceFrequency(&freq);

    for (t = 0; t < DNS_TENTATIVAS; t++)
    {
        WmIp4Array srv;
        void *rec = NULL;
        LARGE_INTEGER t0, t1;
        LONG st;
        double ms;
        int achou = 0;

        srv.cnt = 1;
        srv.addr[0] = j.ip4;
        QueryPerformanceCounter(&t0);
        st = g_dnsQuery(j.dominio, 1 /* DNS_TYPE_A */, DNS_OPCOES_CONSULTA, &srv, &rec, NULL);
        QueryPerformanceCounter(&t1);
        ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        r->tentativas++;

        if (st == 0 && rec)
        {
            const WmDnsRec *q;
            for (q = (const WmDnsRec *)rec; q; q = q->pNext)
            {
                if (q->wType == 1)
                {
                    achou = 1;
                    if (r->resposta[0] == '\0')
                        LanFormatarIp(q->ipv4, r->resposta, sizeof(r->resposta));
                    break;
                }
            }
        }
        if (rec)
            g_dnsFree(rec, 1 /* DnsFreeRecordList */);

        if (achou)
        {
            r->ok++;
            soma += ms;
            if (ms < r->mn)
                r->mn = ms;
            if (ms > r->mx)
                r->mx = ms;
            if (t == 0)
                r->primeira = ms;
        }
        else
        {
            r->falhas++;
            if (t == 0)
                break; /* servidor sem resposta: nao repetir (cada tentativa pode demorar segundos) */
        }
        if (t + 1 < DNS_TENTATIVAS)
            Sleep(60);
    }
    if (r->ok > 0)
        r->media = soma / (double)r->ok;
    else
        r->mn = 0.0;
    if (!PostMessage(j.hwnd, WM_DNS_RESULT, 0, (LPARAM)r))
        free(r);
    return 0;
}

/* Servidores DNS dos adaptadores ativos + alguns publicos de referencia. */
static int DnsRecolherServidores(DWORD *ips, char origem[][24], int max)
{
    static const struct
    {
        const char *ip;
        const char *nome;
    } publicos[] = {{"1.1.1.1", "Cloudflare"}, {"8.8.8.8", "Google"}, {"9.9.9.9", "Quad9"}, {"208.67.222.222", "OpenDNS"}};
    IP_ADAPTER_ADDRESSES *aa = NULL, *p;
    ULONG len = 16384, rc = ERROR_BUFFER_OVERFLOW;
    int n = 0, i, k, tent;

    for (tent = 0; tent < 3 && rc == ERROR_BUFFER_OVERFLOW; tent++)
    {
        free(aa);
        aa = (IP_ADAPTER_ADDRESSES *)malloc(len);
        if (!aa)
            break;
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST, NULL, aa, &len);
    }
    if (aa && rc == NO_ERROR)
    {
        for (p = aa; p && n < max; p = p->Next)
        {
            IP_ADAPTER_DNS_SERVER_ADDRESS *d;
            if (p->OperStatus != IfOperStatusUp || p->IfType == 24 /* loopback */)
                continue;
            for (d = p->FirstDnsServerAddress; d && n < max; d = d->Next)
            {
                DWORD ip;
                int dup = 0;
                if (!d->Address.lpSockaddr || d->Address.lpSockaddr->sa_family != AF_INET)
                    continue;
                ip = ((const struct sockaddr_in *)d->Address.lpSockaddr)->sin_addr.s_addr;
                if (ip == 0)
                    continue;
                for (k = 0; k < n; k++)
                    if (ips[k] == ip)
                        dup = 1;
                if (dup)
                    continue;
                ips[n] = ip;
                strcpy_s(origem[n], 24, "Sistema");
                n++;
            }
        }
    }
    free(aa);

    for (i = 0; i < (int)(sizeof(publicos) / sizeof(publicos[0])) && n < max; i++)
    {
        DWORD ip = inet_addr(publicos[i].ip);
        int dup = 0;
        for (k = 0; k < n; k++)
            if (ips[k] == ip)
                dup = 1;
        if (dup)
            continue;
        ips[n] = ip;
        strcpy_s(origem[n], 24, publicos[i].nome);
        n++;
    }
    return n;
}

static void DnsIniciar(HWND hwnd)
{
    DWORD ips[DNS_MAX_SERVIDORES];
    char origem[DNS_MAX_SERVIDORES][24];
    char dominio[128];
    char *ini;
    size_t len;
    int n, i, lancados = 0;

    if (g_dnsPendentes > 0 || !hDnsDominio)
        return;
    GetWindowTextA(hDnsDominio, dominio, sizeof(dominio));
    ini = dominio;
    while (*ini == ' ' || *ini == '\t')
        ini++;
    len = strlen(ini);
    while (len > 0 && (ini[len - 1] == ' ' || ini[len - 1] == '\t'))
        ini[--len] = '\0';
    if (len == 0)
    {
        if (hDnsStatus)
            SetWindowTextA(hDnsStatus, "Indique um dominio (ex.: example.com).");
        return;
    }
    if (!DnsCarregarApi())
    {
        if (hDnsStatus)
            SetWindowTextA(hDnsStatus, "Nao foi possivel carregar dnsapi.dll.");
        return;
    }

    n = DnsRecolherServidores(ips, origem, DNS_MAX_SERVIDORES);
    if (hDnsList)
        ListView_DeleteAllItems(hDnsList);
    g_dnsNServ = n;
    memset(g_dnsRes, 0, sizeof(g_dnsRes));

    for (i = 0; i < n; i++)
    {
        DnsJob *j;
        HANDLE h;

        LanFormatarIp(ips[i], g_dnsIpTxt[i], sizeof(g_dnsIpTxt[i]));
        strcpy_s(g_dnsOrigem[i], sizeof(g_dnsOrigem[i]), origem[i]);
        if (hDnsList)
        {
            int row = RedeAddLinha(hDnsList, i, g_dnsIpTxt[i]);
            if (row >= 0)
            {
                ListView_SetItemText(hDnsList, row, 1, g_dnsOrigem[i]);
                ListView_SetItemText(hDnsList, row, 7, "A testar...");
            }
        }
        j = (DnsJob *)calloc(1, sizeof(*j));
        if (!j)
            continue;
        j->hwnd = hwnd;
        j->idx = i;
        j->ip4 = ips[i];
        strncpy_s(j->dominio, sizeof(j->dominio), ini, _TRUNCATE);
        h = CreateThread(NULL, 0, DnsThread, j, 0, NULL);
        if (!h)
        {
            free(j);
            if (hDnsList)
                ListView_SetItemText(hDnsList, i, 7, "Erro ao iniciar");
            continue;
        }
        CloseHandle(h);
        lancados++;
    }

    g_dnsPendentes = lancados;
    if (lancados > 0)
    {
        if (hDnsGo)
        {
            EnableWindow(hDnsGo, FALSE);
            SetWindowTextA(hDnsGo, "A testar...");
        }
        if (hDnsStatus)
        {
            char st[120];
            snprintf(st, sizeof(st), "A consultar %d servidor(es) (%d consultas cada, sem cache local)...", lancados, DNS_TENTATIVAS);
            SetWindowTextA(hDnsStatus, st);
        }
    }
    else if (hDnsStatus)
        SetWindowTextA(hDnsStatus, "Nao foi possivel iniciar o teste.");
}

static void DnsMostrarResultado(DnsResultado *r)
{
    char txt[40];
    int i;

    if (!r)
        return;
    if (r->idx >= 0 && r->idx < g_dnsNServ && hDnsList)
    {
        g_dnsRes[r->idx] = *r;
        if (r->ok > 0)
        {
            snprintf(txt, sizeof(txt), "%.1f ms", r->primeira >= 0 ? r->primeira : r->media);
            ListView_SetItemText(hDnsList, r->idx, 2, txt);
            snprintf(txt, sizeof(txt), "%.1f ms", r->media);
            ListView_SetItemText(hDnsList, r->idx, 3, txt);
            snprintf(txt, sizeof(txt), "%.1f ms", r->mn);
            ListView_SetItemText(hDnsList, r->idx, 4, txt);
            snprintf(txt, sizeof(txt), "%.1f ms", r->mx);
            ListView_SetItemText(hDnsList, r->idx, 5, txt);
        }
        else
        {
            ListView_SetItemText(hDnsList, r->idx, 2, "-");
            ListView_SetItemText(hDnsList, r->idx, 3, "-");
            ListView_SetItemText(hDnsList, r->idx, 4, "-");
            ListView_SetItemText(hDnsList, r->idx, 5, "-");
        }
        snprintf(txt, sizeof(txt), "%d/%d", r->falhas, r->tentativas);
        ListView_SetItemText(hDnsList, r->idx, 6, txt);
        ListView_SetItemText(hDnsList, r->idx, 7, r->ok > 0 ? r->resposta : "Sem resposta");
    }
    free(r);

    if (g_dnsPendentes > 0)
        g_dnsPendentes--;
    if (g_dnsPendentes == 0)
    {
        int melhor = -1;
        char st[160];

        for (i = 0; i < g_dnsNServ; i++)
            if (g_dnsRes[i].ok > 0 && (melhor < 0 || g_dnsRes[i].media < g_dnsRes[melhor].media))
                melhor = i;
        if (hDnsGo)
        {
            EnableWindow(hDnsGo, TRUE);
            SetWindowTextA(hDnsGo, "Testar DNS");
        }
        if (melhor >= 0)
            snprintf(st, sizeof(st), "Concluido. Mais rapido: %s (%s), media %.1f ms. A 1a consulta pode incluir a resolucao recursiva do servidor.",
                     g_dnsIpTxt[melhor], g_dnsOrigem[melhor], g_dnsRes[melhor].media);
        else
            snprintf(st, sizeof(st), "Concluido: nenhum servidor respondeu para este dominio.");
        if (hDnsStatus)
            SetWindowTextA(hDnsStatus, st);
    }
}

