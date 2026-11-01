/*
 * 12_network_extras.inc — ferramentas de rede adicionais (janela propria).
 *
 * Ferramentas:
 *   0. Adaptadores   : placas ativas, IPs, gateway, DNS, MAC e velocidade.
 *   1. Monitor hosts : 10 pings por host (perda, min/med/max, jitter).
 *   2. Scan de portas: TCP connect em IPs privados/localhost (lista ou intervalo).
 *   3. Consumo dados : descarregado/enviado hoje e no mes (persistido no Registry).
 *
 * Desenho: modulo autonomo. Nao depende de nenhuma funcao dos outros .inc;
 * so usa os globais hFontUI/hFontMonitor (opcionais, ver abaixo). Todos os
 * simbolos internos tem prefixo nx/NX_ para evitar colisoes.
 *
 * API publica (2 funcoes):
 *   void NetExtras_Open(HWND parent);  abre (ou traz para a frente) a janela.
 *   void NetExtras_Tick(void);         opcional, chamar no timer principal para
 *                                      o consumo de dados ser contabilizado
 *                                      com continuidade (barato: 1 chamada API).
 *
 * Threads: as ferramentas lentas correm numa thread; a thread nunca toca em
 * controlos, so envia WM_NX_TEXT (string malloc'd) para a janela.
 *
 * Registry: HKCU\Software\WinMon\NetExtras
 * Bibliotecas: as mesmas ja usadas (iphlpapi, ws2_32, advapi32, user32).
 */

#include <netioapi.h>

#define NX_CLASS "WinMonNetExtrasClass"
#define NX_REG_KEY "Software\\WinMon\\NetExtras"

#define IDC_NX_TOOL 6100
#define IDC_NX_ARG 6101
#define IDC_NX_RUN 6102
#define IDC_NX_HINT 6103
#define IDC_NX_OUT 6104

#define WM_NX_TEXT (WM_APP + 40) /* lParam = char* (malloc), libertado no handler */
#define WM_NX_DONE (WM_APP + 41)

#define NX_PING_COUNT 10
#define NX_MAX_HOSTS 8
#define NX_MAX_PORTS 1024
#define NX_BATCH 32
#define NX_CONNECT_TIMEOUT_MS 800

typedef struct
{
    HWND hwnd;
    int tool;
    char arg[256];
} NxJob;

typedef struct
{
    int loaded;
    int haLast;
    DWORD dia, mes; /* yyyymmdd, yyyymm */
    ULONG64 hojeIn, hojeOut, mesIn, mesOut;
    ULONG64 lastIn, lastOut;
    ULONGLONG ultimoSave;
} NxUsage;

static HWND nxWnd = NULL;
static HWND nxTool = NULL;
static HWND nxArg = NULL;
static HWND nxRun = NULL;
static HWND nxHint = NULL;
static HWND nxOut = NULL;
static HANDLE nxThread = NULL; /* so acedido pela thread da UI */
static volatile LONG nxCancel = 0;
static NxUsage nxU;

/* ------------------------------------------------------------------ */
/* Utilitarios                                                        */
/* ------------------------------------------------------------------ */

static void nx_post(HWND w, const char *fmt, ...)
{
    char *buf = (char *)malloc(1024);
    va_list ap;
    size_t n;

    if (!buf)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, 1000, fmt, ap);
    va_end(ap);
    n = strlen(buf);
    buf[n] = '\r';
    buf[n + 1] = '\n';
    buf[n + 2] = 0;
    if (!PostMessageA(w, WM_NX_TEXT, 0, (LPARAM)buf))
        free(buf);
}

static void nx_w2a(const WCHAR *w, char *out, size_t cap)
{
    if (cap == 0)
        return;
    out[0] = 0;
    if (!w)
        return;
    if (!WideCharToMultiByte(CP_ACP, 0, w, -1, out, (int)cap, NULL, NULL))
        out[0] = 0;
}

static void nx_fmt_bytes(double b, char *out, size_t cap)
{
    static const char *u[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    while (b >= 1024.0 && i < 4)
    {
        b /= 1024.0;
        i++;
    }
    snprintf(out, cap, i == 0 ? "%.0f %s" : "%.2f %s", b, u[i]);
}

static int nx_addr_text(const SOCKADDR *sa, int len, char *out, size_t cap)
{
    return getnameinfo(sa, (socklen_t)len, out, (DWORD)cap, NULL, 0, NI_NUMERICHOST) == 0;
}

static int nx_resolve_v4(const char *host, struct in_addr *out)
{
    struct addrinfo hints, *res = NULL;
    int ok = 0;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) == 0 && res)
    {
        *out = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ok = 1;
    }
    if (res)
        freeaddrinfo(res);
    return ok;
}

static int nx_is_private_v4(struct in_addr a)
{
    DWORD h = ntohl(a.s_addr);
    return (h >> 24) == 10 || (h >> 24) == 127 || (h >> 20) == 0xAC1 ||
           (h >> 16) == 0xC0A8 || (h >> 16) == 0xA9FE;
}

static const char *nx_if_type(DWORD t)
{
    switch (t)
    {
    case IF_TYPE_ETHERNET_CSMACD:
        return "Ethernet";
    case IF_TYPE_IEEE80211:
        return "Wi-Fi";
    case IF_TYPE_TUNNEL:
        return "Tunel";
    case IF_TYPE_PPP:
        return "PPP/VPN";
    default:
        return "Outro";
    }
}

static IP_ADAPTER_ADDRESSES *nx_get_adapters(void)
{
    ULONG sz = 15000, rc = ERROR_BUFFER_OVERFLOW;
    IP_ADAPTER_ADDRESSES *b = NULL;
    int i;

    for (i = 0; i < 4 && rc == ERROR_BUFFER_OVERFLOW; i++)
    {
        free(b);
        b = (IP_ADAPTER_ADDRESSES *)malloc(sz);
        if (!b)
            return NULL;
        rc = GetAdaptersAddresses(AF_UNSPEC,
                                  GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                                  NULL, b, &sz);
    }
    if (rc != NO_ERROR)
    {
        free(b);
        return NULL;
    }
    return b;
}

static int nx_default_gateway(char *out, size_t cap)
{
    IP_ADAPTER_ADDRESSES *list = nx_get_adapters(), *a;
    IP_ADAPTER_GATEWAY_ADDRESS_LH *g;
    int ok = 0;

    for (a = list; a && !ok; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (g = a->FirstGatewayAddress; g; g = g->Next)
        {
            if (g->Address.lpSockaddr && g->Address.lpSockaddr->sa_family == AF_INET)
            {
                ok = nx_addr_text(g->Address.lpSockaddr, g->Address.iSockaddrLength, out, cap);
                break;
            }
        }
    }
    free(list);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Ferramenta 0: adaptadores                                          */
/* ------------------------------------------------------------------ */

static void nx_tool_adapters(HWND w)
{
    IP_ADAPTER_ADDRESSES *list = nx_get_adapters(), *a;
    int shown = 0, hidden = 0;

    if (!list)
    {
        nx_post(w, "Nao foi possivel obter a lista de adaptadores.");
        return;
    }
    for (a = list; a; a = a->Next)
    {
        char nome[128], desc[160], ip[96];
        IP_ADAPTER_UNICAST_ADDRESS_LH *u;
        IP_ADAPTER_GATEWAY_ADDRESS_LH *g;
        IP_ADAPTER_DNS_SERVER_ADDRESS_XP *d;

        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        if (a->OperStatus != IfOperStatusUp)
        {
            hidden++;
            continue;
        }
        shown++;
        nx_w2a(a->FriendlyName, nome, sizeof nome);
        nx_w2a(a->Description, desc, sizeof desc);
        nx_post(w, "[%s] %s", nx_if_type(a->IfType), nome);
        nx_post(w, "  %s", desc);
        if (a->ReceiveLinkSpeed && a->ReceiveLinkSpeed != (ULONG64)-1)
            nx_post(w, "  Velocidade : %llu Mbps", (unsigned long long)(a->ReceiveLinkSpeed / 1000000ULL));
        if (a->PhysicalAddressLength == 6)
            nx_post(w, "  MAC        : %02X:%02X:%02X:%02X:%02X:%02X",
                    a->PhysicalAddress[0], a->PhysicalAddress[1], a->PhysicalAddress[2],
                    a->PhysicalAddress[3], a->PhysicalAddress[4], a->PhysicalAddress[5]);
        for (u = a->FirstUnicastAddress; u; u = u->Next)
            if (u->Address.lpSockaddr && nx_addr_text(u->Address.lpSockaddr, u->Address.iSockaddrLength, ip, sizeof ip))
                nx_post(w, "  IP         : %s/%u", ip, (unsigned)u->OnLinkPrefixLength);
        for (g = a->FirstGatewayAddress; g; g = g->Next)
            if (g->Address.lpSockaddr && nx_addr_text(g->Address.lpSockaddr, g->Address.iSockaddrLength, ip, sizeof ip))
                nx_post(w, "  Gateway    : %s", ip);
        for (d = a->FirstDnsServerAddress; d; d = d->Next)
            if (d->Address.lpSockaddr && nx_addr_text(d->Address.lpSockaddr, d->Address.iSockaddrLength, ip, sizeof ip))
                nx_post(w, "  DNS        : %s", ip);
        nx_post(w, "");
    }
    nx_post(w, "%d adaptador(es) ativo(s), %d inativo(s) omitido(s).", shown, hidden);
    free(list);
}

/* ------------------------------------------------------------------ */
/* Ferramenta 1: monitor de hosts (ICMP)                              */
/* ------------------------------------------------------------------ */

static void nx_ping_host(HWND w, HANDLE icmp, const char *host)
{
    struct in_addr ia;
    char ip[16];
    unsigned char payload[32];
    DWORD rsz = (DWORD)(sizeof(ICMP_ECHO_REPLY) + sizeof payload + 8);
    unsigned char *reply;
    int sent = 0, recv = 0, jn = 0, i;
    double sum = 0.0, mn = 1e9, mx = 0.0, jsum = 0.0, prev = -1.0;

    if (!nx_resolve_v4(host, &ia))
    {
        nx_post(w, "%-24s nao foi possivel resolver (so IPv4)", host);
        return;
    }
    inet_ntop(AF_INET, &ia, ip, sizeof ip);
    reply = (unsigned char *)malloc(rsz);
    if (!reply)
        return;
    memset(payload, 'a', sizeof payload);

    for (i = 0; i < NX_PING_COUNT && !nxCancel; i++)
    {
        DWORD n = IcmpSendEcho(icmp, ia.s_addr, payload, (WORD)sizeof payload, NULL, reply, rsz, 1000);
        sent++;
        if (n > 0)
        {
            ICMP_ECHO_REPLY *r = (ICMP_ECHO_REPLY *)reply;
            if (r->Status == IP_SUCCESS)
            {
                double ms = (double)r->RoundTripTime;
                recv++;
                sum += ms;
                if (ms < mn)
                    mn = ms;
                if (ms > mx)
                    mx = ms;
                if (prev >= 0.0)
                {
                    jsum += fabs(ms - prev);
                    jn++;
                }
                prev = ms;
            }
        }
        Sleep(200);
    }
    free(reply);

    if (sent == 0)
        return;
    if (recv == 0)
        nx_post(w, "%-24s %-15s 100%% perda (%d enviados)", host, ip, sent);
    else
        nx_post(w, "%-24s %-15s perda %3.0f%%  min/med/max %.0f/%.1f/%.0f ms  jitter %.1f ms",
                host, ip, 100.0 * (sent - recv) / sent, mn, sum / recv, mx, jn ? jsum / jn : 0.0);
}

static void nx_tool_hosts(HWND w, const char *arg)
{
    char hosts[NX_MAX_HOSTS][64];
    int n = 0, i;
    HANDLE icmp;
    const char *p = arg;

    while (*p && n < NX_MAX_HOSTS)
    {
        size_t len = 0;
        while (*p == ',' || *p == ' ')
            p++;
        if (!*p)
            break;
        while (p[len] && p[len] != ',' && p[len] != ' ')
            len++;
        if (len >= sizeof hosts[0])
            len = sizeof hosts[0] - 1;
        memcpy(hosts[n], p, len);
        hosts[n][len] = 0;
        n++;
        p += len;
        while (*p && *p != ',' && *p != ' ')
            p++; /* descarta o excedente de nomes demasiado longos */
    }
    if (n == 0)
    {
        if (nx_default_gateway(hosts[n], sizeof hosts[n]))
            n++;
        snprintf(hosts[n++], sizeof hosts[0], "1.1.1.1");
        snprintf(hosts[n++], sizeof hosts[0], "8.8.8.8");
    }

    icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
    {
        nx_post(w, "IcmpCreateFile falhou (erro %lu).", GetLastError());
        return;
    }
    nx_post(w, "A enviar %d pings por host (1 s de timeout)...", NX_PING_COUNT);
    nx_post(w, "");
    for (i = 0; i < n && !nxCancel; i++)
        nx_ping_host(w, icmp, hosts[i]);
    IcmpCloseHandle(icmp);
}

/* ------------------------------------------------------------------ */
/* Ferramenta 2: scan de portas TCP (apenas IPs privados / localhost)  */
/* ------------------------------------------------------------------ */

static const unsigned short NX_COMMON_PORTS[] = {
    21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445, 993, 995, 1433, 3000,
    3306, 3389, 5000, 5432, 5900, 6379, 8000, 8080, 8443, 9200, 27017};

static const char *nx_service(unsigned p)
{
    switch (p)
    {
    case 21: return "FTP";
    case 22: return "SSH";
    case 23: return "Telnet";
    case 25: return "SMTP";
    case 53: return "DNS";
    case 80: return "HTTP";
    case 110: return "POP3";
    case 135: return "RPC";
    case 139: return "NetBIOS";
    case 143: return "IMAP";
    case 443: return "HTTPS";
    case 445: return "SMB";
    case 993: return "IMAPS";
    case 995: return "POP3S";
    case 1433: return "SQL Server";
    case 3000: return "Dev server";
    case 3306: return "MySQL/MariaDB";
    case 3389: return "RDP";
    case 5000: return "Dev server";
    case 5432: return "PostgreSQL";
    case 5900: return "VNC";
    case 6379: return "Redis";
    case 8000: return "HTTP alt";
    case 8080: return "HTTP alt";
    case 8443: return "HTTPS alt";
    case 9200: return "Elasticsearch";
    case 27017: return "MongoDB";
    default: return "";
    }
}

/* "22,80,443,8000-8010" -> lista. Devolve n, -1 (sintaxe) ou -2 (demasiadas). */
static int nx_parse_ports(const char *s, unsigned short *out, int max)
{
    int n = 0;
    const char *p = s;

    while (*p)
    {
        char *end;
        long a, b, x;

        while (*p == ',' || *p == ' ')
            p++;
        if (!*p)
            break;
        a = strtol(p, &end, 10);
        if (end == p)
            return -1;
        b = a;
        p = end;
        if (*p == '-')
        {
            p++;
            b = strtol(p, &end, 10);
            if (end == p)
                return -1;
            p = end;
        }
        if (a < 1 || b > 65535 || a > b)
            return -1;
        for (x = a; x <= b; x++)
        {
            if (n >= max)
                return -2;
            out[n++] = (unsigned short)x;
        }
    }
    return n;
}

static void nx_tool_scan(HWND w, const char *arg)
{
    char host[128] = {0}, ports[200] = {0}, ip[16];
    unsigned short list[NX_MAX_PORTS];
    struct in_addr ia;
    const char *sp = arg;
    size_t hl = 0;
    int np, base, open = 0;

    while (*sp == ' ')
        sp++;
    while (sp[hl] && sp[hl] != ' ')
        hl++;
    if (hl == 0 || hl >= sizeof host)
    {
        nx_post(w, "Uso: host [portas]   ex.: 192.168.1.1 22,80,443,8000-8010");
        return;
    }
    memcpy(host, sp, hl);
    sp += hl;
    while (*sp == ' ')
        sp++;
    snprintf(ports, sizeof ports, "%s", sp);

    if (!nx_resolve_v4(host, &ia))
    {
        nx_post(w, "Nao foi possivel resolver '%s' (so IPv4).", host);
        return;
    }
    inet_ntop(AF_INET, &ia, ip, sizeof ip);
    if (!nx_is_private_v4(ia))
    {
        nx_post(w, "Recusado: %s nao e um IP privado/localhost.", ip);
        nx_post(w, "Esta ferramenta so analisa a tua propria rede (10.x, 172.16-31.x, 192.168.x, 127.x, 169.254.x).");
        return;
    }
    if (ports[0])
    {
        np = nx_parse_ports(ports, list, NX_MAX_PORTS);
        if (np == -1)
        {
            nx_post(w, "Lista de portas invalida. Exemplo: 22,80,443,8000-8010");
            return;
        }
        if (np == -2)
        {
            nx_post(w, "Demasiadas portas (maximo %d por execucao).", NX_MAX_PORTS);
            return;
        }
    }
    else
    {
        np = (int)(sizeof NX_COMMON_PORTS / sizeof NX_COMMON_PORTS[0]);
        memcpy(list, NX_COMMON_PORTS, sizeof NX_COMMON_PORTS);
    }
    if (np == 0)
        return;

    nx_post(w, "A analisar %s (%s): %d porta(s)...", host, ip, np);
    nx_post(w, "");

    for (base = 0; base < np && !nxCancel; base += NX_BATCH)
    {
        SOCKET s[NX_BATCH];
        int state[NX_BATCH]; /* 0 = pendente, 1 = aberta, 2 = fechada */
        int cnt = np - base < NX_BATCH ? np - base : NX_BATCH;
        int pending = 0, i;
        ULONGLONG end;

        for (i = 0; i < cnt; i++)
        {
            struct sockaddr_in sa;
            u_long nb = 1;

            state[i] = 2;
            s[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s[i] == INVALID_SOCKET)
                continue;
            ioctlsocket(s[i], FIONBIO, &nb);
            memset(&sa, 0, sizeof sa);
            sa.sin_family = AF_INET;
            sa.sin_addr = ia;
            sa.sin_port = htons(list[base + i]);
            if (connect(s[i], (SOCKADDR *)&sa, sizeof sa) == 0)
                state[i] = 1;
            else if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                state[i] = 0;
                pending++;
            }
        }

        end = GetTickCount64() + NX_CONNECT_TIMEOUT_MS;
        while (pending > 0 && !nxCancel)
        {
            fd_set wr, ex;
            struct timeval tv;
            LONG64 left = (LONG64)(end - GetTickCount64());

            if (left <= 0)
                break;
            FD_ZERO(&wr);
            FD_ZERO(&ex);
            for (i = 0; i < cnt; i++)
                if (state[i] == 0)
                {
                    FD_SET(s[i], &wr);
                    FD_SET(s[i], &ex);
                }
            tv.tv_sec = (long)(left / 1000);
            tv.tv_usec = (long)((left % 1000) * 1000);
            if (select(0, NULL, &wr, &ex, &tv) <= 0)
                break;
            for (i = 0; i < cnt; i++)
            {
                if (state[i] != 0)
                    continue;
                if (FD_ISSET(s[i], &wr))
                {
                    int err = 1, len = (int)sizeof err;
                    getsockopt(s[i], SOL_SOCKET, SO_ERROR, (char *)&err, &len);
                    state[i] = (err == 0) ? 1 : 2;
                    pending--;
                }
                else if (FD_ISSET(s[i], &ex))
                {
                    state[i] = 2;
                    pending--;
                }
            }
        }

        for (i = 0; i < cnt; i++)
        {
            if (s[i] != INVALID_SOCKET)
                closesocket(s[i]);
            if (state[i] == 1)
            {
                open++;
                nx_post(w, "  %5u/tcp  ABERTA   %s", (unsigned)list[base + i], nx_service(list[base + i]));
            }
        }
    }
    nx_post(w, "");
    nx_post(w, "%d porta(s) aberta(s) de %d testada(s). As restantes estao fechadas ou filtradas.", open, np);
}

/* ------------------------------------------------------------------ */
/* Ferramenta 3: consumo de dados (hoje / mes), persistido            */
/* ------------------------------------------------------------------ */

static int nx_reg_get64(HKEY k, const char *name, ULONG64 *v)
{
    DWORD type = 0, sz = sizeof(ULONG64);
    ULONG64 tmp = 0;

    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&tmp, &sz) == ERROR_SUCCESS &&
        type == REG_QWORD && sz == sizeof tmp)
    {
        *v = tmp;
        return 1;
    }
    return 0;
}

static void nx_reg_set64(HKEY k, const char *name, ULONG64 v)
{
    RegSetValueExA(k, name, 0, REG_QWORD, (const BYTE *)&v, sizeof v);
}

static void nx_reg_getdw(HKEY k, const char *name, DWORD *v)
{
    DWORD type = 0, sz = sizeof(DWORD), tmp = 0;

    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&tmp, &sz) == ERROR_SUCCESS && type == REG_DWORD)
        *v = tmp;
}

static void nx_usage_load(void)
{
    HKEY k;

    if (nxU.loaded)
        return;
    nxU.loaded = 1;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, NX_REG_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return;
    nx_reg_getdw(k, "Dia", &nxU.dia);
    nx_reg_getdw(k, "Mes", &nxU.mes);
    nx_reg_get64(k, "HojeIn", &nxU.hojeIn);
    nx_reg_get64(k, "HojeOut", &nxU.hojeOut);
    nx_reg_get64(k, "MesIn", &nxU.mesIn);
    nx_reg_get64(k, "MesOut", &nxU.mesOut);
    nxU.haLast = nx_reg_get64(k, "UltimoIn", &nxU.lastIn) && nx_reg_get64(k, "UltimoOut", &nxU.lastOut);
    RegCloseKey(k);
}

static void nx_usage_save(void)
{
    HKEY k;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, NX_REG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    RegSetValueExA(k, "Dia", 0, REG_DWORD, (const BYTE *)&nxU.dia, sizeof(DWORD));
    RegSetValueExA(k, "Mes", 0, REG_DWORD, (const BYTE *)&nxU.mes, sizeof(DWORD));
    nx_reg_set64(k, "HojeIn", nxU.hojeIn);
    nx_reg_set64(k, "HojeOut", nxU.hojeOut);
    nx_reg_set64(k, "MesIn", nxU.mesIn);
    nx_reg_set64(k, "MesOut", nxU.mesOut);
    nx_reg_set64(k, "UltimoIn", nxU.lastIn);
    nx_reg_set64(k, "UltimoOut", nxU.lastOut);
    RegCloseKey(k);
    nxU.ultimoSave = GetTickCount64();
}

/* Soma dos octetos das placas fisicas (inclui as inativas, para o total nao recuar). */
static int nx_sum_octets(ULONG64 *in, ULONG64 *out)
{
    MIB_IF_TABLE2 *t = NULL;
    ULONG i;

    if (GetIfTable2(&t) != NO_ERROR || !t)
        return 0;
    *in = 0;
    *out = 0;
    for (i = 0; i < t->NumEntries; i++)
    {
        const MIB_IF_ROW2 *r = &t->Table[i];
        if (r->Type == IF_TYPE_SOFTWARE_LOOPBACK || !r->InterfaceAndOperStatusFlags.HardwareInterface)
            continue;
        *in += r->InOctets;
        *out += r->OutOctets;
    }
    FreeMibTable(t);
    return 1;
}

void NetExtras_Tick(void)
{
    ULONG64 in, out, dIn, dOut;
    SYSTEMTIME st;
    DWORD dia, mes;

    nx_usage_load();
    if (!nx_sum_octets(&in, &out))
        return;
    GetLocalTime(&st);
    dia = (DWORD)st.wYear * 10000u + (DWORD)st.wMonth * 100u + st.wDay;
    mes = (DWORD)st.wYear * 100u + st.wMonth;
    if (dia != nxU.dia)
    {
        nxU.dia = dia;
        nxU.hojeIn = nxU.hojeOut = 0;
    }
    if (mes != nxU.mes)
    {
        nxU.mes = mes;
        nxU.mesIn = nxU.mesOut = 0;
    }
    /* contadores menores que a ultima leitura = reboot/reset: conta desde zero */
    dIn = (nxU.haLast && in >= nxU.lastIn) ? in - nxU.lastIn : in;
    dOut = (nxU.haLast && out >= nxU.lastOut) ? out - nxU.lastOut : out;
    nxU.hojeIn += dIn;
    nxU.hojeOut += dOut;
    nxU.mesIn += dIn;
    nxU.mesOut += dOut;
    nxU.lastIn = in;
    nxU.lastOut = out;
    nxU.haLast = 1;
    if (nxU.ultimoSave == 0 || GetTickCount64() - nxU.ultimoSave >= 60000ULL)
        nx_usage_save();
}

static void nx_tool_usage(HWND w)
{
    char a[32], b[32], c[32];

    NetExtras_Tick();
    nx_usage_save();
    nx_post(w, "Consumo de dados (placas fisicas, contadores do Windows)");
    nx_post(w, "");
    nx_fmt_bytes((double)nxU.hojeIn, a, sizeof a);
    nx_fmt_bytes((double)nxU.hojeOut, b, sizeof b);
    nx_fmt_bytes((double)(nxU.hojeIn + nxU.hojeOut), c, sizeof c);
    nx_post(w, "Hoje : recebido %-10s enviado %-10s total %s", a, b, c);
    nx_fmt_bytes((double)nxU.mesIn, a, sizeof a);
    nx_fmt_bytes((double)nxU.mesOut, b, sizeof b);
    nx_fmt_bytes((double)(nxU.mesIn + nxU.mesOut), c, sizeof c);
    nx_post(w, "Mes   : recebido %-10s enviado %-10s total %s", a, b, c);
    nx_post(w, "");
    nx_post(w, "Os totais contam desde a primeira execucao desta ferramenta; o trafego");
    nx_post(w, "com o WinMon fechado e somado na abertura seguinte (exceto se houver reboot).");
}

/* ------------------------------------------------------------------ */
/* Thread de trabalho                                                 */
/* ------------------------------------------------------------------ */

static DWORD WINAPI nx_thread(LPVOID p)
{
    NxJob *j = (NxJob *)p;
    HWND w = j->hwnd;
    WSADATA wd;

    if (WSAStartup(MAKEWORD(2, 2), &wd) == 0)
    {
        switch (j->tool)
        {
        case 0:
            nx_tool_adapters(w);
            break;
        case 1:
            nx_tool_hosts(w, j->arg);
            break;
        case 2:
            nx_tool_scan(w, j->arg);
            break;
        default:
            break;
        }
        if (nxCancel)
            nx_post(w, "(interrompido)");
        WSACleanup();
    }
    else
        nx_post(w, "Falha ao iniciar o Winsock.");
    free(j);
    PostMessageA(w, WM_NX_DONE, 0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Janela                                                             */
/* ------------------------------------------------------------------ */

static void nx_update_hint(void)
{
    static const char *hints[] = {
        "Sem argumentos: mostra as placas ativas, IPs, gateway e DNS.",
        "Hosts separados por virgula. Vazio = gateway, 1.1.1.1 e 8.8.8.8.",
        "host [portas]  ex.: 192.168.1.1 22,80,443,8000-8010  (vazio = portas comuns; so IPs privados)",
        "Sem argumentos: dados recebidos/enviados hoje e neste mes."};
    int t = (int)SendMessageA(nxTool, CB_GETCURSEL, 0, 0);

    if (t < 0 || t > 3)
        t = 0;
    SetWindowTextA(nxHint, hints[t]);
    EnableWindow(nxArg, t == 1 || t == 2);
}

static void nx_run(HWND hwnd)
{
    int tool;
    NxJob *j;

    if (nxThread)
    {
        InterlockedExchange(&nxCancel, 1);
        return;
    }
    tool = (int)SendMessageA(nxTool, CB_GETCURSEL, 0, 0);
    SetWindowTextA(nxOut, "");
    if (tool == 3)
    {
        nx_tool_usage(hwnd); /* rapido: corre na thread da UI */
        return;
    }
    j = (NxJob *)calloc(1, sizeof *j);
    if (!j)
        return;
    GetWindowTextA(nxArg, j->arg, (int)sizeof j->arg);
    j->hwnd = hwnd;
    j->tool = tool;
    InterlockedExchange(&nxCancel, 0);
    nxThread = CreateThread(NULL, 0, nx_thread, j, 0, NULL);
    if (!nxThread)
    {
        free(j);
        nx_post(hwnd, "Nao foi possivel criar a thread de trabalho.");
        return;
    }
    SetWindowTextA(nxRun, "Parar");
}

static void nx_layout(int cw, int ch)
{
    const int m = 10, h = 26;
    int argX = m + 180, runW = 90;

    MoveWindow(nxTool, m, m, 170, 200, TRUE);
    MoveWindow(nxArg, argX, m, cw - argX - runW - 2 * m, h, TRUE);
    MoveWindow(nxRun, cw - runW - m, m, runW, h, TRUE);
    MoveWindow(nxHint, m, m + h + 6, cw - 2 * m, 20, TRUE);
    MoveWindow(nxOut, m, m + h + 30, cw - 2 * m, ch - (m + h + 30) - m, TRUE);
}

static LRESULT CALLBACK nx_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hi = GetModuleHandleA(NULL);
        HFONT fUi = hFontUI ? hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT fMono = hFontMonitor ? hFontMonitor : (HFONT)GetStockObject(ANSI_FIXED_FONT);
        static const char *nomes[] = {"Adaptadores", "Monitor de hosts", "Scan de portas", "Consumo de dados"};
        int i;

        nxTool = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_NX_TOOL, hi, NULL);
        nxArg = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_NX_ARG, hi, NULL);
        nxRun = CreateWindowExA(0, "BUTTON", "Executar", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_NX_RUN, hi, NULL);
        nxHint = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
                                 hwnd, (HMENU)(INT_PTR)IDC_NX_HINT, hi, NULL);
        nxOut = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                    ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_NX_OUT, hi, NULL);
        for (i = 0; i < 4; i++)
            SendMessageA(nxTool, CB_ADDSTRING, 0, (LPARAM)nomes[i]);
        SendMessageA(nxTool, CB_SETCURSEL, 0, 0);
        SendMessageA(nxOut, EM_SETLIMITTEXT, 2000000, 0);
        SendMessageA(nxTool, WM_SETFONT, (WPARAM)fUi, TRUE);
        SendMessageA(nxArg, WM_SETFONT, (WPARAM)fUi, TRUE);
        SendMessageA(nxRun, WM_SETFONT, (WPARAM)fUi, TRUE);
        SendMessageA(nxHint, WM_SETFONT, (WPARAM)fUi, TRUE);
        SendMessageA(nxOut, WM_SETFONT, (WPARAM)fMono, TRUE);
        nx_update_hint();
        return 0;
    }
    case WM_SIZE:
        if (nxOut && wp != SIZE_MINIMIZED)
            nx_layout(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = 520;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = 320;
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_NX_TOOL && HIWORD(wp) == CBN_SELCHANGE)
            nx_update_hint();
        else if (LOWORD(wp) == IDC_NX_RUN && HIWORD(wp) == BN_CLICKED)
            nx_run(hwnd);
        return 0;
    case WM_NX_TEXT:
    {
        char *s = (char *)lp;
        SendMessageA(nxOut, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
        SendMessageA(nxOut, EM_REPLACESEL, FALSE, (LPARAM)s);
        free(s);
        return 0;
    }
    case WM_NX_DONE:
        if (nxThread)
        {
            CloseHandle(nxThread);
            nxThread = NULL;
        }
        SetWindowTextA(nxRun, "Executar");
        return 0;
    case WM_CLOSE:
    {
        MSG m;
        if (nxThread)
        {
            InterlockedExchange(&nxCancel, 1);
            WaitForSingleObject(nxThread, 3000);
            CloseHandle(nxThread);
            nxThread = NULL;
        }
        while (PeekMessageA(&m, hwnd, WM_NX_TEXT, WM_NX_TEXT, PM_REMOVE))
            free((void *)m.lParam);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY:
        nxWnd = NULL;
        nxTool = nxArg = nxRun = nxHint = nxOut = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void NetExtras_Open(HWND parent)
{
    static int registered = 0;
    HINSTANCE hi = GetModuleHandleA(NULL);

    if (nxWnd)
    {
        ShowWindow(nxWnd, SW_RESTORE);
        SetForegroundWindow(nxWnd);
        return;
    }
    if (!registered)
    {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = nx_wndproc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = NX_CLASS;
        if (!RegisterClassExA(&wc))
            return;
        registered = 1;
    }
    nxWnd = CreateWindowExA(0, NX_CLASS, "WinMon - Ferramentas de rede", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            CW_USEDEFAULT, CW_USEDEFAULT, 780, 520, parent, NULL, hi, NULL);
}
