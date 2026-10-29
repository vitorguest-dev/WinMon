/* 05 processes and connections */
/* =========================================================================
 * Painel de processos full-list  (aba "Processos")
 * ========================================================================= */

/* Comparadores para qsort — usam g_lvSortCol / g_lvSortDesc */
static void DefinirAgrupamentoProcessos(int ligado, int persistir);

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
    case 1: /* PID (ou n.o de instancias na vista agrupada) */
        if (g_lvAgrupar)
            r = (p1->instancias > p2->instancias) - (p1->instancias < p2->instancias);
        else
            r = (p1->pid > p2->pid) - (p1->pid < p2->pid);
        break;
    case 2: /* CPU */
        r = (p1->cpuPercent > p2->cpuPercent) - (p1->cpuPercent < p2->cpuPercent);
        break;
    case 3: /* RAM */
        r = (p1->memUsageMB > p2->memUsageMB) - (p1->memUsageMB < p2->memUsageMB);
        break;
    case 4: /* Download TCP */
        r = (p1->netDownBps > p2->netDownBps) - (p1->netDownBps < p2->netDownBps);
        break;
    case 5: /* Upload TCP */
        r = (p1->netUpBps > p2->netUpBps) - (p1->netUpBps < p2->netUpBps);
        break;
    }
    if (r != 0)
        return g_lvSortDesc ? -r : r;

    /* desempate estavel: evita linhas a "saltar" quando os valores sao iguais */
    r = _stricmp(p1->exeFile, p2->exeFile);
    if (r == 0)
        r = (p1->pid > p2->pid) - (p1->pid < p2->pid);
    return r;
}

static void ParaMinusculas(char *dst, size_t n, const char *src)
{
    size_t i = 0;
    if (!dst || n == 0)
        return;
    for (; src && src[i] && i + 1 < n; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

/* filtro vazio aceita tudo; caso contrario testa nome e PID */
static int ProcessoPassaFiltro(const ProcessoInfo *p, const char *filtroLow)
{
    char nomeLow[MAX_PATH];
    char pidStr[16];

    if (filtroLow[0] == '\0')
        return 1;

    ParaMinusculas(nomeLow, sizeof(nomeLow), p->exeFile);
    if (strstr(nomeLow, filtroLow) != NULL)
        return 1;

    snprintf(pidStr, sizeof(pidStr), "%u", p->pid);
    return strstr(pidStr, filtroLow) != NULL;
}

/* Instancia "principal" de cada grupo = a de maior CPU (desempate: RAM).
 * O seu PID e usado em "Abrir Localizacao" na vista agrupada. */
static double g_grpMelhorCpu[MAX_PROCESSES];
static SIZE_T g_grpMelhorMem[MAX_PROCESSES];

static int EncontrarGrupoLv(const char *nome, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        if (_stricmp(g_lvProcessos[i].exeFile, nome) == 0)
            return i;
    }
    return -1;
}

/* Preenche g_lvProcessos com base no filtro de texto actual.
 * Com g_lvAgrupar ativo, as instancias com o mesmo nome fundem-se numa
 * linha (CPU e RAM somados). O filtro aplica-se antes de agrupar. */
static void FiltrarListaProcessos(void)
{
    char filtro[MAX_PATH];
    char filtroLow[MAX_PATH];
    int i, n = 0;

    filtro[0] = '\0';
    if (hEditPesquisaProc)
        GetWindowTextA(hEditPesquisaProc, filtro, (int)sizeof(filtro));
    ParaMinusculas(filtroLow, sizeof(filtroLow), filtro);

    for (i = 0; i < g_totalProcessosBruto && n < MAX_PROCESSES; i++)
    {
        const ProcessoInfo *p = &listaProcessos[i];
        int g;

        if (!ProcessoPassaFiltro(p, filtroLow))
            continue;

        g = g_lvAgrupar ? EncontrarGrupoLv(p->exeFile, n) : -1;

        if (g < 0)
        {
            g_lvProcessos[n] = *p;
            g_lvProcessos[n].instancias = 1;
            g_grpMelhorCpu[n] = p->cpuPercent;
            g_grpMelhorMem[n] = p->memUsageMB;
            n++;
        }
        else
        {
            ProcessoInfo *grp = &g_lvProcessos[g];

            grp->cpuPercent += p->cpuPercent;
            grp->memUsageMB += p->memUsageMB;
            grp->netDownBps += p->netDownBps;
            grp->netUpBps += p->netUpBps;
            grp->instancias++;

            if (p->cpuPercent > g_grpMelhorCpu[g] ||
                (p->cpuPercent == g_grpMelhorCpu[g] &&
                 p->memUsageMB > g_grpMelhorMem[g]))
            {
                g_grpMelhorCpu[g] = p->cpuPercent;
                g_grpMelhorMem[g] = p->memUsageMB;
                grp->pid = p->pid;
            }
        }
    }
    g_lvTotal = n;

    /* ordena conforme coluna activa */
    if (g_lvTotal > 1)
        qsort(g_lvProcessos, (size_t)g_lvTotal,
              sizeof(ProcessoInfo), CompararLvProc);
}

static void AtualizarLabelContagemProc(void)
{
    char buf[96];

    if (!hLabelProcCount)
        return;

    if (g_lvAgrupar)
        snprintf(buf, sizeof(buf), "%d processos em %d grupos",
                 g_totalProcessosBruto, g_lvTotal);
    else
        snprintf(buf, sizeof(buf), "%d processos  (%d visíveis)",
                 g_totalProcessosBruto, g_lvTotal);
    SetWindowTextA(hLabelProcCount, buf);
}

/* Reflete g_lvAgrupar na checkbox e no cabecalho da coluna 1 */
static void SincronizarUIAgrupamento(void)
{
    if (hChkAgrupar)
        Button_SetCheck(hChkAgrupar, g_lvAgrupar ? BST_CHECKED : BST_UNCHECKED);

    if (hListViewProc)
    {
        LVCOLUMNA col;
        ZeroMemory(&col, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = g_lvAgrupar ? "PID / Inst." : "PID";
        ListView_SetColumn(hListViewProc, 1, &col);
    }
    g_lvAgruparUI = g_lvAgrupar;
}

/* Desliga o agrupamento e filtra pelo nome: mostra as instancias individuais */
static void MostrarInstanciasGrupo(const char *nome)
{
    char copia[MAX_PATH];

    strncpy_s(copia, sizeof(copia), nome, _TRUNCATE);
    DefinirAgrupamentoProcessos(0, 0);
    if (hEditPesquisaProc)
        SetWindowTextA(hEditPesquisaProc, copia); /* EN_CHANGE refaz a lista */
}

static void CopiarTextoClipboard(HWND hwnd, const char *txt)
{
    size_t len = strlen(txt) + 1;
    HGLOBAL hGlb;

    if (!OpenClipboard(hwnd))
        return;
    EmptyClipboard();
    hGlb = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hGlb)
    {
        void *dst = GlobalLock(hGlb);
        if (dst)
        {
            memcpy(dst, txt, len);
            GlobalUnlock(hGlb);
            if (!SetClipboardData(CF_TEXT, hGlb))
                GlobalFree(hGlb);
        }
        else
            GlobalFree(hGlb);
    }
    CloseClipboard();
}

/* Menu de contexto de uma linha agrupada (varias instancias).
 * Os dados do grupo sao copiados antes de mostrar o menu, porque o timer
 * continua a correr e g_lvProcessos pode ser reconstruido entretanto. */
static void MenuContextoGrupo(HWND hwnd, const ProcessoInfo *grupo)
{
    char nome[MAX_PATH];
    char txt[256];
    DWORD pidPrincipal = grupo->pid;
    int inst = grupo->instancias;
    HMENU hMenu;
    POINT pt;
    UINT cmd;
    int i;

    strncpy_s(nome, sizeof(nome), grupo->exeFile, _TRUNCATE);

    hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    snprintf(txt, sizeof(txt), "Terminar todas as instancias (%d)", inst);
    AppendMenuA(hMenu, MF_STRING, 1, txt);
    snprintf(txt, sizeof(txt), "Definir Prioridade Alta (%d instancias)", inst);
    AppendMenuA(hMenu, MF_STRING, 2, txt);
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, 3, "Ver instancias individuais");
    AppendMenuA(hMenu, MF_STRING, 4, "Abrir Localizacao do Ficheiro");
    AppendMenuA(hMenu, MF_STRING, 5, "Copiar nome");
    AppendMenuA(hMenu, MF_STRING, 6, "Ver ligacoes deste processo");

    GetCursorPos(&pt);
    cmd = (UINT)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd)
    {
    case 1:
    {
        int total = 0, ok = 0;

        snprintf(txt, sizeof(txt),
                 "Terminar as %d instancias de %s?\n"
                 "Isto pode causar perda de dados e instabilidade.",
                 inst, nome);
        if (MessageBoxA(hwnd, txt, "Aviso", MB_YESNO | MB_ICONWARNING) != IDYES)
            break;

        for (i = 0; i < totalListaProcessos; i++)
        {
            HANDLE hProc;

            if (_stricmp(listaProcessos[i].exeFile, nome) != 0)
                continue;
            total++;
            hProc = OpenProcess(PROCESS_TERMINATE, FALSE, listaProcessos[i].pid);
            if (hProc)
            {
                if (TerminateProcess(hProc, 0))
                    ok++;
                CloseHandle(hProc);
            }
        }
        if (ok < total)
        {
            snprintf(txt, sizeof(txt),
                     "Foram terminadas %d de %d instancias "
                     "(as restantes exigem mais permissoes).",
                     ok, total);
            MessageBoxA(hwnd, txt, "WinMon", MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case 2:
        for (i = 0; i < totalListaProcessos; i++)
        {
            HANDLE hProc;

            if (_stricmp(listaProcessos[i].exeFile, nome) != 0)
                continue;
            hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, listaProcessos[i].pid);
            if (hProc)
            {
                SetPriorityClass(hProc, HIGH_PRIORITY_CLASS);
                CloseHandle(hProc);
            }
        }
        break;
    case 3:
        MostrarInstanciasGrupo(nome);
        break;
    case 4:
    {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pidPrincipal);
        if (hProc)
        {
            char path[MAX_PATH];
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameA(hProc, 0, path, &size))
            {
                char args[MAX_PATH + 32];
                snprintf(args, sizeof(args), "/select,\"%s\"", path);
                ShellExecuteA(NULL, "open", "explorer.exe", args, NULL, SW_SHOW);
            }
            CloseHandle(hProc);
        }
        break;
    }
    case 5:
        CopiarTextoClipboard(hwnd, nome);
        break;
    case 6:
        MostrarLigacoesDoProcesso(nome);
        break;
    }
}

/* Actualiza o conteúdo do ListView com os dados mais recentes */
static void AtualizarListViewProcessos(void)
{
    if (!hListViewProc)
        return;

    /* (MonitorarProcessos() já preencheu listaProcessos[],
       e usamos totalListaProcessos, preenchido pelo mesmo ciclo de recolha. */
    g_totalProcessosBruto = totalListaProcessos; /* mesmo ciclo */

    if (g_lvAgruparUI != g_lvAgrupar)
        SincronizarUIAgrupamento();

    FiltrarListaProcessos();

    /* Suspende redesenho para evitar flicker */
    SendMessage(hListViewProc, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemCountEx(hListViewProc, g_lvTotal,
                            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    SendMessage(hListViewProc, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hListViewProc, NULL, FALSE);

    AtualizarLabelContagemProc();
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
    case 1: /* PID (ou n.o de instancias num grupo) */
        if (g_lvAgrupar && g_lvProcessos[idx].instancias > 1)
            snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                     "%d inst.", g_lvProcessos[idx].instancias);
        else
            snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                     "%u", g_lvProcessos[idx].pid);
        break;
    case 2: /* CPU */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                 "%.1f%%", g_lvProcessos[idx].cpuPercent);
        break;
    case 3: /* RAM */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax,
                 "%lu MB", (unsigned long)g_lvProcessos[idx].memUsageMB);
        break;
    case 4: /* Download */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax, "%.1f KB/s", g_lvProcessos[idx].netDownBps/1024.0);
        break;
    case 5: /* Upload */
        snprintf(pdi->item.pszText, (size_t)pdi->item.cchTextMax, "%.1f KB/s", g_lvProcessos[idx].netUpBps/1024.0);
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
        g_lvSortDesc = (col >= 2); /* CPU/RAM/rede descendente por omissao */
    }
    AtualizarListViewProcessos();
}

/* =========================================================================
 * Aba "Ligacoes" — ligacoes TCP/UDP (IPv4 e IPv6) com o processo dono,
 * ao estilo de "netstat -ano". Usa GetExtendedTcpTable/GetExtendedUdpTable
 * (iphlpapi, ja usada pelo resto do programa); sem novas dependencias.
 * A tabela so e lida enquanto a aba esta aberta.
 * ========================================================================= */
#define IDC_EDIT_PESQUISA_CX 6010
#define IDC_LISTVIEW_CX 6011
#define IDC_LABEL_CX_COUNT 6012
#define IDC_CHK_CX_ATIVAS 6013

#define MAX_CONEXOES 6000
#define CX_AF_INET 2
#define CX_AF_INET6 23
#define CX_TCP_LISTEN 2
#define CX_TCP_ESTABLISHED 5

typedef struct
{
    BYTE udp;          /* 0 = TCP, 1 = UDP */
    BYTE v6;           /* 1 = IPv6 */
    BYTE estabelecida; /* TCP ESTABLISHED */
    DWORD estado;      /* MIB_TCP_STATE (0 em UDP) */
    DWORD pid;
    char proc[64];
    char local[72];  /* "ip:porta" */
    char remoto[72]; /* "ip:porta" ou "*" */
    char dns[256];
    BYTE portaIncomum;
} ConexaoInfo;

static ConexaoInfo g_conexoes[MAX_CONEXOES];
static int g_conexoesTotal = 0;
static int g_cxTruncadas = 0;     /* linhas que nao couberam em g_conexoes */
static int g_cxIdx[MAX_CONEXOES]; /* indices visiveis (apos filtro + ordenacao) */
static int g_cxVisiveis = 0;
static int g_cxSortCol = 0;
static int g_cxSortDesc = 0;

static WORD CxPorta(DWORD p)
{
    /* dwLocalPort/dwRemotePort: porta em ordem de rede nos 16 bits baixos */
    return (WORD)(((p & 0xFFu) << 8) | ((p >> 8) & 0xFFu));
}

static void CxFormatarV4(DWORD addrRede, WORD porta, char *out, size_t n)
{
    const BYTE *b = (const BYTE *)&addrRede;
    snprintf(out, n, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], (unsigned)porta);
}

static void CxFormatarV6(const BYTE a[16], WORD porta, char *out, size_t n)
{
    WORD g[8];
    int i, melhorIni = -1, melhorLen = 0, ini = -1, len = 0;
    char buf[48];
    size_t pos = 0;

    for (i = 0; i < 8; i++)
        g[i] = (WORD)((a[i * 2] << 8) | a[i * 2 + 1]);

    /* maior sequencia de grupos a zero (>= 2) vira "::" */
    for (i = 0; i < 8; i++)
    {
        if (g[i] == 0)
        {
            if (ini < 0)
            {
                ini = i;
                len = 0;
            }
            len++;
            if (len > melhorLen)
            {
                melhorLen = len;
                melhorIni = ini;
            }
        }
        else
            ini = -1;
    }
    if (melhorLen < 2)
        melhorIni = -1;

    buf[0] = '\0';
    for (i = 0; i < 8; i++)
    {
        if (i == melhorIni)
        {
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "::");
            i += melhorLen - 1;
            continue;
        }
        if (pos > 0 && buf[pos - 1] != ':')
            pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, ":");
        pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%x", (unsigned)g[i]);
    }
    snprintf(out, n, "[%s]:%u", buf, (unsigned)porta);
}

static void CxNomeProcesso(DWORD pid, char *out, size_t n)
{
    int i;

    if (pid == 0)
    {
        strncpy_s(out, n, "(sistema)", _TRUNCATE);
        return;
    }
    if (pid == 4)
    {
        strncpy_s(out, n, "System", _TRUNCATE);
        return;
    }
    for (i = 0; i < totalListaProcessos; i++)
    {
        if (listaProcessos[i].pid == pid)
        {
            strncpy_s(out, n, listaProcessos[i].exeFile, _TRUNCATE);
            return;
        }
    }
    snprintf(out, n, "PID %lu", (unsigned long)pid);
}

#define CX_DNS_CACHE 256
#define CX_CONHECIDAS_MAX 6000
typedef struct { volatile LONG state; char ip[64]; char name[256]; } CxDnsEntry;
static CxDnsEntry g_cxDns[CX_DNS_CACHE];
static CRITICAL_SECTION g_cxDnsLock;
static int g_cxDnsLockReady=0;
static volatile LONG g_cxDnsThreads=0;
typedef struct { DWORD pid; BYTE udp,v6; char local[72],remoto[72]; } CxConhecida;
static CxConhecida g_cxConhecidas[CX_CONHECIDAS_MAX];
static int g_cxConhecidasN=0, g_cxConhecidasInit=0;
static ULONGLONG g_cxUltimoAlerta=0;
static int CxPortaIncomum(WORD p)
{
    /* Portas conhecidas sao neutras; realca portas de servidor/administracao
       comuns em maquinas pessoais e portas efemeras nao sao classificadas. */
    switch(p) { case 21: case 23: case 445: case 1433: case 1521: case 2375:
    case 3306: case 3389: case 4444: case 5432: case 5900: case 6379:
    case 8080: case 8443: case 9200: case 27017: return 1; default: return 0; }
}
static int CxExtrairIP(const char *endpoint, char *ip, size_t n)
{
    const char *start=endpoint, *end=NULL; size_t len;
    if(!endpoint || !endpoint[0] || strcmp(endpoint,"*")==0) return 0;
    if(endpoint[0]=='[') { start=endpoint+1; end=strchr(start,']'); }
    else { end=strrchr(endpoint,':'); }
    if(!end || end<=start) return 0;
    len=(size_t)(end-start); if(len>=n) return 0;
    memcpy(ip,start,len); ip[len]='\0'; return 1;
}
static DWORD WINAPI CxResolverThread(LPVOID arg)
{
    int idx=(int)(INT_PTR)arg; struct sockaddr_storage ss; int rc=-1; char host[NI_MAXHOST];
    char ip[64]; struct sockaddr_in *a4; struct sockaddr_in6 *a6;
    if(idx<0 || idx>=CX_DNS_CACHE) return 0;
    strncpy_s(ip,sizeof(ip),g_cxDns[idx].ip,_TRUNCATE); ZeroMemory(&ss,sizeof(ss));
    if(strchr(ip,':')) { a6=(struct sockaddr_in6*)&ss; a6->sin6_family=AF_INET6; if(InetPtonA(AF_INET6,ip,&a6->sin6_addr)==1) rc=getnameinfo((struct sockaddr*)a6,sizeof(*a6),host,sizeof(host),NULL,0,NI_NAMEREQD); }
    else { a4=(struct sockaddr_in*)&ss; a4->sin_family=AF_INET; if(InetPtonA(AF_INET,ip,&a4->sin_addr)==1) rc=getnameinfo((struct sockaddr*)a4,sizeof(*a4),host,sizeof(host),NULL,0,NI_NAMEREQD); }
    EnterCriticalSection(&g_cxDnsLock);
    if(rc==0) strncpy_s(g_cxDns[idx].name,sizeof(g_cxDns[idx].name),host,_TRUNCATE);
    else strncpy_s(g_cxDns[idx].name,sizeof(g_cxDns[idx].name),"(sem DNS inverso)",_TRUNCATE);
    InterlockedExchange(&g_cxDns[idx].state,2);
    LeaveCriticalSection(&g_cxDnsLock); InterlockedDecrement(&g_cxDnsThreads); return 0;
}
static const char *CxDnsNome(const char *endpoint)
{
    char ip[64]; int i,freeIdx=-1; static char pending[]="a resolver...";
    if(!g_cxDnsLockReady) return "(DNS indisponivel)";
    if(!CxExtrairIP(endpoint,ip,sizeof(ip))) return "-";
    /* Evita resolver enderecos privados/loopback, onde DNS inverso raramente ajuda. */
    if(strcmp(ip,"127.0.0.1")==0 || strcmp(ip,"::1")==0 || strncmp(ip,"10.",3)==0 || strncmp(ip,"192.168.",8)==0 || strncmp(ip,"172.16.",7)==0) return "(privado)";
    EnterCriticalSection(&g_cxDnsLock);
    for(i=0;i<CX_DNS_CACHE;i++) { if(g_cxDns[i].state && strcmp(g_cxDns[i].ip,ip)==0) { const char *r=(g_cxDns[i].state==2)?g_cxDns[i].name:pending; LeaveCriticalSection(&g_cxDnsLock); return r; } if(!g_cxDns[i].state && freeIdx<0) freeIdx=i; }
    if(freeIdx>=0) { strncpy_s(g_cxDns[freeIdx].ip,sizeof(g_cxDns[freeIdx].ip),ip,_TRUNCATE); g_cxDns[freeIdx].name[0]=0; InterlockedExchange(&g_cxDns[freeIdx].state,1); }
    LeaveCriticalSection(&g_cxDnsLock);
    if(freeIdx>=0) { HANDLE th; InterlockedIncrement(&g_cxDnsThreads); th=CreateThread(NULL,0,CxResolverThread,(LPVOID)(INT_PTR)freeIdx,0,NULL); if(th) CloseHandle(th); else { InterlockedDecrement(&g_cxDnsThreads); InterlockedExchange(&g_cxDns[freeIdx].state,2); } }
    return pending;
}
static int CxEhConhecida(const ConexaoInfo *c)
{
    int i; for(i=0;i<g_cxConhecidasN;i++) if(g_cxConhecidas[i].pid==c->pid && g_cxConhecidas[i].udp==c->udp && g_cxConhecidas[i].v6==c->v6 && strcmp(g_cxConhecidas[i].local,c->local)==0 && strcmp(g_cxConhecidas[i].remoto,c->remoto)==0) return 1;
    return 0;
}
static void CxVerificarNovasLigacoes(void)
{
    int i, alertou=0; ULONGLONG now=GetTickCount64();
    if(!g_cxConhecidasInit || g_cxConhecidasN + g_conexoesTotal >= CX_CONHECIDAS_MAX) {
        g_cxConhecidasN=0;
        for(i=0;i<g_conexoesTotal && g_cxConhecidasN<CX_CONHECIDAS_MAX;i++){ ConexaoInfo*c=&g_conexoes[i]; CxConhecida*k=&g_cxConhecidas[g_cxConhecidasN++]; k->pid=c->pid;k->udp=c->udp;k->v6=c->v6;strncpy_s(k->local,sizeof(k->local),c->local,_TRUNCATE);strncpy_s(k->remoto,sizeof(k->remoto),c->remoto,_TRUNCATE);}
        g_cxConhecidasInit=1; return;
    }
    for(i=0;i<g_conexoesTotal;i++) { ConexaoInfo*c=&g_conexoes[i]; if(CxEhConhecida(c)) continue;
        if(g_cxConhecidasN>=CX_CONHECIDAS_MAX) { g_cxConhecidasN=0; break; }
        { CxConhecida*k=&g_cxConhecidas[g_cxConhecidasN++];k->pid=c->pid;k->udp=c->udp;k->v6=c->v6;strncpy_s(k->local,sizeof(k->local),c->local,_TRUNCATE);strncpy_s(k->remoto,sizeof(k->remoto),c->remoto,_TRUNCATE); }
        if(!alertou && !c->udp && c->estabelecida && strcmp(c->remoto,"*")!=0 && now-g_cxUltimoAlerta>15000) { char msg[180]; snprintf(msg,sizeof(msg),"Nova ligacao de %s (PID %lu): %s",c->proc,(unsigned long)c->pid,c->remoto); EnviarBalloon("WinMon — nova ligacao",msg); g_cxUltimoAlerta=now; alertou=1; }
    }
}

static void CxAdicionar(int udp, int v6, DWORD estado, DWORD pid,
                        const char *local, const char *remoto)
{
    ConexaoInfo *c;

    if (g_conexoesTotal >= MAX_CONEXOES)
    {
        g_cxTruncadas++;
        return;
    }
    c = &g_conexoes[g_conexoesTotal++];
    c->udp = (BYTE)udp;
    c->v6 = (BYTE)v6;
    c->estado = estado;
    c->pid = pid;
    c->estabelecida = (BYTE)(!udp && estado == CX_TCP_ESTABLISHED);
    CxNomeProcesso(pid, c->proc, sizeof(c->proc));
    strncpy_s(c->local, sizeof(c->local), local, _TRUNCATE);
    strncpy_s(c->remoto, sizeof(c->remoto), remoto, _TRUNCATE);
    { char *colon=strrchr(c->local,':'); WORD lp=colon?(WORD)atoi(colon+1):0; colon=strrchr(c->remoto,':'); WORD rp=colon?(WORD)atoi(colon+1):0; c->portaIncomum=(BYTE)(CxPortaIncomum(lp)||CxPortaIncomum(rp)); }
    strncpy_s(c->dns, sizeof(c->dns), "", _TRUNCATE);
}

/* Le uma tabela do sistema; o tamanho pode crescer entre chamadas, por isso
 * repete com folga. Devolve memoria (free) ou NULL. */
static void *CxObterTabela(int tcp, ULONG af)
{
    DWORD sz = 0, r;
    void *buf = NULL;
    int t;

    for (t = 0; t < 5; t++)
    {
        r = tcp ? GetExtendedTcpTable(buf, &sz, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0)
                : GetExtendedUdpTable(buf, &sz, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (r == NO_ERROR)
            return buf;

        free(buf);
        buf = NULL;
        if (r != ERROR_INSUFFICIENT_BUFFER)
            return NULL;

        sz += sz / 8 + 1024;
        buf = malloc(sz);
        if (!buf)
            return NULL;
    }
    free(buf);
    return NULL;
}

static void ConexoesRecolher(void)
{
    char loc[72], rem[72];
    DWORD i;
    void *t;

    g_conexoesTotal = 0;
    g_cxTruncadas = 0;

    t = CxObterTabela(1, CX_AF_INET);
    if (t)
    {
        MIB_TCPTABLE_OWNER_PID *tb = (MIB_TCPTABLE_OWNER_PID *)t;
        for (i = 0; i < tb->dwNumEntries; i++)
        {
            const MIB_TCPROW_OWNER_PID *r = &tb->table[i];
            CxFormatarV4(r->dwLocalAddr, CxPorta(r->dwLocalPort), loc, sizeof(loc));
            if (r->dwState == CX_TCP_LISTEN)
                strncpy_s(rem, sizeof(rem), "*", _TRUNCATE);
            else
                CxFormatarV4(r->dwRemoteAddr, CxPorta(r->dwRemotePort), rem, sizeof(rem));
            CxAdicionar(0, 0, r->dwState, r->dwOwningPid, loc, rem);
        }
        free(t);
    }

    t = CxObterTabela(1, CX_AF_INET6);
    if (t)
    {
        MIB_TCP6TABLE_OWNER_PID *tb = (MIB_TCP6TABLE_OWNER_PID *)t;
        for (i = 0; i < tb->dwNumEntries; i++)
        {
            const MIB_TCP6ROW_OWNER_PID *r = &tb->table[i];
            CxFormatarV6(r->ucLocalAddr, CxPorta(r->dwLocalPort), loc, sizeof(loc));
            if (r->dwState == CX_TCP_LISTEN)
                strncpy_s(rem, sizeof(rem), "*", _TRUNCATE);
            else
                CxFormatarV6(r->ucRemoteAddr, CxPorta(r->dwRemotePort), rem, sizeof(rem));
            CxAdicionar(0, 1, r->dwState, r->dwOwningPid, loc, rem);
        }
        free(t);
    }

    t = CxObterTabela(0, CX_AF_INET);
    if (t)
    {
        MIB_UDPTABLE_OWNER_PID *tb = (MIB_UDPTABLE_OWNER_PID *)t;
        for (i = 0; i < tb->dwNumEntries; i++)
        {
            const MIB_UDPROW_OWNER_PID *r = &tb->table[i];
            CxFormatarV4(r->dwLocalAddr, CxPorta(r->dwLocalPort), loc, sizeof(loc));
            CxAdicionar(1, 0, 0, r->dwOwningPid, loc, "*");
        }
        free(t);
    }

    t = CxObterTabela(0, CX_AF_INET6);
    if (t)
    {
        MIB_UDP6TABLE_OWNER_PID *tb = (MIB_UDP6TABLE_OWNER_PID *)t;
        for (i = 0; i < tb->dwNumEntries; i++)
        {
            const MIB_UDP6ROW_OWNER_PID *r = &tb->table[i];
            CxFormatarV6(r->ucLocalAddr, CxPorta(r->dwLocalPort), loc, sizeof(loc));
            CxAdicionar(1, 1, 0, r->dwOwningPid, loc, "*");
        }
        free(t);
    }
}

static const char *CxProtoTxt(const ConexaoInfo *c)
{
    if (c->udp)
        return c->v6 ? "UDP6" : "UDP";
    return c->v6 ? "TCP6" : "TCP";
}

static const char *CxEstadoTxt(const ConexaoInfo *c)
{
    if (c->udp)
        return "-";
    switch (c->estado)
    {
    case 1:
        return "Fechada";
    case 2:
        return "A escutar";
    case 3:
        return "SYN enviado";
    case 4:
        return "SYN recebido";
    case 5:
        return "Estabelecida";
    case 6:
        return "FIN_WAIT_1";
    case 7:
        return "FIN_WAIT_2";
    case 8:
        return "CLOSE_WAIT";
    case 9:
        return "CLOSING";
    case 10:
        return "LAST_ACK";
    case 11:
        return "TIME_WAIT";
    case 12:
        return "A eliminar";
    default:
        return "?";
    }
}

static int CxComparar(const void *a, const void *b)
{
    int ia = *(const int *)a, ib = *(const int *)b;
    const ConexaoInfo *x = &g_conexoes[ia];
    const ConexaoInfo *y = &g_conexoes[ib];
    int r = 0;

    switch (g_cxSortCol)
    {
    case 0:
        r = _stricmp(x->proc, y->proc);
        break;
    case 1:
        r = (x->pid > y->pid) - (x->pid < y->pid);
        break;
    case 2:
        r = strcmp(CxProtoTxt(x), CxProtoTxt(y));
        break;
    case 3:
        r = strcmp(x->local, y->local);
        break;
    case 4:
        r = strcmp(x->remoto, y->remoto);
        break;
    case 5:
        r = strcmp(CxEstadoTxt(x), CxEstadoTxt(y));
        break;
    }
    if (g_cxSortDesc)
        r = -r;
    if (r == 0) /* desempate estavel: ordem original da tabela */
        r = (ia > ib) - (ia < ib);
    return r;
}

static int CxPassaFiltro(const ConexaoInfo *c, const char *filtroLow, int soEstabelecidas)
{
    char tmp[320], low[320];

    if (soEstabelecidas && !c->estabelecida)
        return 0;
    if (!filtroLow[0])
        return 1;

    snprintf(tmp, sizeof(tmp), "%s %lu %s %s %s %s",
             c->proc, (unsigned long)c->pid, CxProtoTxt(c),
             c->local, c->remoto, CxEstadoTxt(c));
    ParaMinusculas(low, sizeof(low), tmp);
    return strstr(low, filtroLow) != NULL;
}

/* Chave da linha selecionada, para a manter selecionada entre refrescos */
static int CxMesmaLinha(const ConexaoInfo *c, const ConexaoInfo *k)
{
    return c->udp == k->udp && c->v6 == k->v6 && c->pid == k->pid &&
           strcmp(c->local, k->local) == 0 && strcmp(c->remoto, k->remoto) == 0;
}

static void AtualizarLigacoes(void)
{
    char filtro[128], filtroLow[128];
    int soEstab, i, estab = 0, haSel = 0;
    ConexaoInfo selChave;
    int selIdx;

    if (!hListViewLig)
        return;

    /* linha selecionada antes de reconstruir a lista */
    selIdx = ListView_GetNextItem(hListViewLig, -1, LVNI_SELECTED);
    if (selIdx >= 0 && selIdx < g_cxVisiveis)
    {
        selChave = g_conexoes[g_cxIdx[selIdx]];
        haSel = 1;
    }

    ConexoesRecolher();
    CxVerificarNovasLigacoes();

    filtro[0] = '\0';
    if (hEditLig)
        GetWindowTextA(hEditLig, filtro, sizeof(filtro));
    ParaMinusculas(filtroLow, sizeof(filtroLow), filtro);
    soEstab = hChkLigAtivas && Button_GetCheck(hChkLigAtivas) == BST_CHECKED;

    g_cxVisiveis = 0;
    for (i = 0; i < g_conexoesTotal; i++)
    {
        if (g_conexoes[i].estabelecida)
            estab++;
        if (CxPassaFiltro(&g_conexoes[i], filtroLow, soEstab))
            g_cxIdx[g_cxVisiveis++] = i;
    }
    qsort(g_cxIdx, (size_t)g_cxVisiveis, sizeof(int), CxComparar);

    SendMessage(hListViewLig, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemCountEx(hListViewLig, g_cxVisiveis,
                            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);

    if (haSel)
    {
        int novo = -1;
        for (i = 0; i < g_cxVisiveis; i++)
        {
            if (CxMesmaLinha(&g_conexoes[g_cxIdx[i]], &selChave))
            {
                novo = i;
                break;
            }
        }
        if (novo != selIdx)
        {
            if (selIdx >= 0)
                ListView_SetItemState(hListViewLig, selIdx, 0, LVIS_SELECTED | LVIS_FOCUSED);
            if (novo >= 0)
                ListView_SetItemState(hListViewLig, novo,
                                      LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }

    SendMessage(hListViewLig, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hListViewLig, NULL, FALSE);

    if (hLabelLigCount)
    {
        char txt[160];
        snprintf(txt, sizeof(txt), "%d/%d ligacoes (%d estabelecidas) | vermelho = porta de risco%s",
                 g_cxVisiveis, g_conexoesTotal, estab,
                 g_cxTruncadas ? " [lista truncada]" : "");
        SetWindowTextA(hLabelLigCount, txt);
    }
}

static void LvLigGetDispInfo(NMLVDISPINFOA *pdi)
{
    const ConexaoInfo *c;
    int idx;
    char *dst;
    size_t n;

    if (!(pdi->item.mask & LVIF_TEXT))
        return;

    idx = pdi->item.iItem;
    if (idx < 0 || idx >= g_cxVisiveis)
        return;

    c = &g_conexoes[g_cxIdx[idx]];
    dst = pdi->item.pszText;
    n = (size_t)pdi->item.cchTextMax;

    switch (pdi->item.iSubItem)
    {
    case 0:
        strncpy_s(dst, n, c->proc, _TRUNCATE);
        break;
    case 1:
        snprintf(dst, n, "%lu", (unsigned long)c->pid);
        break;
    case 2:
        strncpy_s(dst, n, CxProtoTxt(c), _TRUNCATE);
        break;
    case 3:
        strncpy_s(dst, n, c->local, _TRUNCATE);
        break;
    case 4:
        strncpy_s(dst, n, c->remoto, _TRUNCATE);
        break;
    case 5:
        strncpy_s(dst, n, CxEstadoTxt(c), _TRUNCATE);
        break;
    case 6:
        strncpy_s(dst, n, CxDnsNome(c->remoto), _TRUNCATE);
        break;
    }
}

static void LvLigColumnClick(int col)
{
    if (g_cxSortCol == col)
        g_cxSortDesc = !g_cxSortDesc;
    else
    {
        g_cxSortCol = col;
        g_cxSortDesc = 0;
    }
    AtualizarLigacoes();
}

/* Filtra a lista pelo nome do processo e passa para a aba Ligacoes */
static void MostrarLigacoesDoProcesso(const char *exe)
{
    if (hEditLig)
        SetWindowTextA(hEditLig, exe);
    if (hChkLigAtivas)
        Button_SetCheck(hChkLigAtivas, BST_UNCHECKED);
    TabCtrl_SetCurSel(hTab, 8);
    MostrarAba(8);
}

static void AbrirDetalheLigacao(int visIdx)
{
    DWORD pid;
    char proc[64];

    if (visIdx < 0 || visIdx >= g_cxVisiveis)
        return;
    pid = g_conexoes[g_cxIdx[visIdx]].pid;
    strncpy_s(proc, sizeof(proc), g_conexoes[g_cxIdx[visIdx]].proc, _TRUNCATE);
    if (pid > 4)
        AbrirDetalheProcesso(pid, proc);
}

static void MenuContextoLigacao(HWND hwnd, int visIdx)
{
    const ConexaoInfo *c;
    DWORD pid;
    char proc[64], local[72], remoto[72], txt[160];
    HMENU hMenu;
    POINT pt;
    UINT cmd;

    if (visIdx < 0 || visIdx >= g_cxVisiveis)
        return;

    /* copia os dados: o refresco pode reconstruir a tabela com o menu aberto */
    c = &g_conexoes[g_cxIdx[visIdx]];
    pid = c->pid;
    strncpy_s(proc, sizeof(proc), c->proc, _TRUNCATE);
    strncpy_s(local, sizeof(local), c->local, _TRUNCATE);
    strncpy_s(remoto, sizeof(remoto), c->remoto, _TRUNCATE);

    hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    snprintf(txt, sizeof(txt), "Filtrar por %s", proc);
    AppendMenuA(hMenu, MF_STRING, 1, txt);
    AppendMenuA(hMenu, MF_STRING | (strcmp(remoto, "*") == 0 ? MF_GRAYED : 0), 2,
                "Copiar endereco remoto");
    AppendMenuA(hMenu, MF_STRING, 3, "Copiar endereco local");
    AppendMenuA(hMenu, MF_STRING | (strcmp(remoto, "*")==0 ? MF_GRAYED : 0), 6, "Geolocalizar IP remoto no browser");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (pid <= 4 ? MF_GRAYED : 0), 4, "Abrir historico do processo");
    AppendMenuA(hMenu, MF_STRING | (pid <= 4 ? MF_GRAYED : 0), 5, "Terminar processo");

    GetCursorPos(&pt);
    cmd = (UINT)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                               pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (cmd)
    {
    case 1:
        if (hEditLig)
            SetWindowTextA(hEditLig, proc); /* EN_CHANGE atualiza a lista */
        break;
    case 2:
        CopiarTextoClipboard(hwnd, remoto);
        break;
    case 3:
        CopiarTextoClipboard(hwnd, local);
        break;
    case 6:
        { char ip[64], url[160]; if(CxExtrairIP(remoto,ip,sizeof(ip)) && !(strncmp(ip,"10.",3)==0 || strncmp(ip,"192.168.",8)==0 || strncmp(ip,"127.",4)==0 || strcmp(ip,"::1")==0)) { snprintf(url,sizeof(url),"https://ipinfo.io/%s",ip); ShellExecuteA(NULL,"open",url,NULL,NULL,SW_SHOWNORMAL); } else MessageBoxA(hwnd,"O endereco e local/privado ou nao pode ser interpretado.","WinMon",MB_OK|MB_ICONINFORMATION); }
        break;
    case 4:
        AbrirDetalheProcesso(pid, proc);
        break;
    case 5:
        if (MessageBoxA(hwnd, "Terminar este processo pode causar instabilidade. Deseja continuar?",
                        "Aviso", MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (hProc)
            {
                TerminateProcess(hProc, 0);
                CloseHandle(hProc);
            }
        }
        break;
    }
}

