/* 07 panels and events */
/* ------------------------------------------------------------------------- */
/* Painel: criacao, visibilidade, layout e despacho de eventos               */
/* ------------------------------------------------------------------------- */

static void RedeLocalMostrar(int visivel)
{
    int m = g_redeModo;
    int i;

    g_redeVisivel = visivel;
    for (i = 0; i < 3; i++)
        RedeMostrarCtl(hRedeModo[i], visivel);
    RedeMostrarCtl(hLanScan, visivel && m == REDE_MODO_LAN);
    RedeMostrarCtl(hLanAceitar, visivel && m == REDE_MODO_LAN);
    RedeMostrarCtl(hLanStatus, visivel && m == REDE_MODO_LAN);
    RedeMostrarCtl(hLanList, visivel && m == REDE_MODO_LAN);
    RedeMostrarCtl(hTraceLbl, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hTraceHost, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hTraceGo, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hTraceNomes, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hTraceStatus, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hTraceList, visivel && m == REDE_MODO_TRACE);
    RedeMostrarCtl(hDnsLbl, visivel && m == REDE_MODO_DNS);
    RedeMostrarCtl(hDnsDominio, visivel && m == REDE_MODO_DNS);
    RedeMostrarCtl(hDnsGo, visivel && m == REDE_MODO_DNS);
    RedeMostrarCtl(hDnsStatus, visivel && m == REDE_MODO_DNS);
    RedeMostrarCtl(hDnsList, visivel && m == REDE_MODO_DNS);
}

static void RedeDefinirModo(int modo)
{
    int i;

    g_redeModo = modo;
    for (i = 0; i < 3; i++)
        if (hRedeModo[i])
            Button_SetCheck(hRedeModo[i], i == modo ? BST_CHECKED : BST_UNCHECKED);
    RedeLocalMostrar(g_redeVisivel);
}

static void RedeLocalRedimensionar(int x, int y, int w, int h)
{
    int yBarra = y + 34;
    int yLista = y + 66;
    int hLista = h - 66;

    if (hLista < 20)
        hLista = 20;
    if (hRedeModo[0])
        MoveWindow(hRedeModo[0], x + 4, y + 3, 150, 26, TRUE);
    if (hRedeModo[1])
        MoveWindow(hRedeModo[1], x + 160, y + 3, 150, 26, TRUE);
    if (hRedeModo[2])
        MoveWindow(hRedeModo[2], x + 316, y + 3, 150, 26, TRUE);

    /* Dispositivos */
    if (hLanScan)
        MoveWindow(hLanScan, x + 4, yBarra, 150, 26, TRUE);
    if (hLanAceitar)
        MoveWindow(hLanAceitar, x + 162, yBarra, 190, 26, TRUE);
    if (hLanStatus)
        MoveWindow(hLanStatus, x + 360, yBarra + 1, w - 364 > 60 ? w - 364 : 60, 24, TRUE);
    if (hLanList)
        MoveWindow(hLanList, x, yLista, w, hLista, TRUE);

    /* Traceroute */
    if (hTraceLbl)
        MoveWindow(hTraceLbl, x + 4, yBarra + 1, 52, 24, TRUE);
    if (hTraceHost)
        MoveWindow(hTraceHost, x + 60, yBarra, 240, 26, TRUE);
    if (hTraceGo)
        MoveWindow(hTraceGo, x + 308, yBarra, 120, 26, TRUE);
    if (hTraceNomes)
        MoveWindow(hTraceNomes, x + 436, yBarra + 1, 140, 24, TRUE);
    if (hTraceStatus)
        MoveWindow(hTraceStatus, x + 584, yBarra + 1, w - 588 > 60 ? w - 588 : 60, 24, TRUE);
    if (hTraceList)
        MoveWindow(hTraceList, x, yLista, w, hLista, TRUE);

    /* DNS */
    if (hDnsLbl)
        MoveWindow(hDnsLbl, x + 4, yBarra + 1, 60, 24, TRUE);
    if (hDnsDominio)
        MoveWindow(hDnsDominio, x + 68, yBarra, 240, 26, TRUE);
    if (hDnsGo)
        MoveWindow(hDnsGo, x + 316, yBarra, 120, 26, TRUE);
    if (hDnsStatus)
        MoveWindow(hDnsStatus, x + 444, yBarra + 1, w - 448 > 60 ? w - 448 : 60, 24, TRUE);
    if (hDnsList)
        MoveWindow(hDnsList, x, yLista, w, hLista, TRUE);
}

static void CriarPainelRedeLocal(HWND pai)
{
    hRedeModo[0] = RedeCriarCtl(pai, "BUTTON", "Dispositivos", BS_AUTORADIOBUTTON | BS_PUSHLIKE | WS_GROUP, 0, IDC_REDE_MODO_LAN);
    hRedeModo[1] = RedeCriarCtl(pai, "BUTTON", "Traceroute", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, IDC_REDE_MODO_TRACE);
    hRedeModo[2] = RedeCriarCtl(pai, "BUTTON", "Teste de DNS", BS_AUTORADIOBUTTON | BS_PUSHLIKE, 0, IDC_REDE_MODO_DNS);

    /* --- Dispositivos --- */
    hLanScan = RedeCriarCtl(pai, "BUTTON", "Procurar dispositivos", BS_PUSHBUTTON, 0, IDC_LAN_SCAN);
    hLanAceitar = RedeCriarCtl(pai, "BUTTON", "Marcar como conhecidos", BS_PUSHBUTTON, 0, IDC_LAN_ACEITAR);
    hLanStatus = RedeCriarCtl(pai, "STATIC", "ARP ativo a sub-rede local; dispositivos novos sao assinalados.",
                              SS_LEFT | SS_CENTERIMAGE, 0, IDC_LAN_STATUS);
    hLanList = RedeCriarLista(pai, IDC_LAN_LIST);
    if (hLanAceitar)
        EnableWindow(hLanAceitar, FALSE);
    if (hLanList)
    {
        RedeAddCol(hLanList, 0, "Endereco IPv4", 130, LVCFMT_LEFT);
        RedeAddCol(hLanList, 1, "Endereco MAC", 150, LVCFMT_LEFT);
        RedeAddCol(hLanList, 2, "Fabricante", 190, LVCFMT_LEFT);
        RedeAddCol(hLanList, 3, "Nome", 220, LVCFMT_LEFT);
        RedeAddCol(hLanList, 4, "Estado", 90, LVCFMT_LEFT);
    }

    /* --- Traceroute --- */
    hTraceLbl = RedeCriarCtl(pai, "STATIC", "Destino", SS_LEFT | SS_CENTERIMAGE, 0, 0);
    hTraceHost = RedeCriarCtl(pai, "EDIT", "8.8.8.8", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_TRACE_HOST);
    hTraceGo = RedeCriarCtl(pai, "BUTTON", "Tracar rota", BS_PUSHBUTTON, 0, IDC_TRACE_GO);
    hTraceNomes = RedeCriarCtl(pai, "BUTTON", "Resolver nomes", BS_AUTOCHECKBOX, 0, IDC_TRACE_NOMES);
    hTraceStatus = RedeCriarCtl(pai, "STATIC", "Latencia por salto (3 sondas ICMP por salto, ate 30 saltos).",
                                SS_LEFT | SS_CENTERIMAGE, 0, IDC_TRACE_STATUS);
    hTraceList = RedeCriarLista(pai, IDC_TRACE_LIST);
    if (hTraceNomes)
        Button_SetCheck(hTraceNomes, BST_CHECKED);
    if (hTraceList)
    {
        RedeAddCol(hTraceList, 0, "Salto", 50, LVCFMT_RIGHT);
        RedeAddCol(hTraceList, 1, "Endereco", 130, LVCFMT_LEFT);
        RedeAddCol(hTraceList, 2, "Nome", 260, LVCFMT_LEFT);
        RedeAddCol(hTraceList, 3, "Sonda 1", 80, LVCFMT_RIGHT);
        RedeAddCol(hTraceList, 4, "Sonda 2", 80, LVCFMT_RIGHT);
        RedeAddCol(hTraceList, 5, "Sonda 3", 80, LVCFMT_RIGHT);
        RedeAddCol(hTraceList, 6, "Media", 80, LVCFMT_RIGHT);
        RedeAddCol(hTraceList, 7, "Perda", 60, LVCFMT_RIGHT);
    }

    /* --- Teste de DNS --- */
    hDnsLbl = RedeCriarCtl(pai, "STATIC", "Dominio", SS_LEFT | SS_CENTERIMAGE, 0, 0);
    hDnsDominio = RedeCriarCtl(pai, "EDIT", "example.com", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, IDC_DNS_DOMINIO);
    hDnsGo = RedeCriarCtl(pai, "BUTTON", "Testar DNS", BS_PUSHBUTTON, 0, IDC_DNS_GO);
    hDnsStatus = RedeCriarCtl(pai, "STATIC", "Tempo de resolucao por servidor DNS (do sistema e publicos).",
                              SS_LEFT | SS_CENTERIMAGE, 0, IDC_DNS_STATUS);
    hDnsList = RedeCriarLista(pai, IDC_DNS_LIST);
    if (hDnsList)
    {
        RedeAddCol(hDnsList, 0, "Servidor", 130, LVCFMT_LEFT);
        RedeAddCol(hDnsList, 1, "Origem", 100, LVCFMT_LEFT);
        RedeAddCol(hDnsList, 2, "1a consulta", 90, LVCFMT_RIGHT);
        RedeAddCol(hDnsList, 3, "Media", 80, LVCFMT_RIGHT);
        RedeAddCol(hDnsList, 4, "Min", 80, LVCFMT_RIGHT);
        RedeAddCol(hDnsList, 5, "Max", 80, LVCFMT_RIGHT);
        RedeAddCol(hDnsList, 6, "Falhas", 70, LVCFMT_RIGHT);
        RedeAddCol(hDnsList, 7, "Resposta", 160, LVCFMT_LEFT);
    }

    g_redeModo = REDE_MODO_LAN;
    if (hRedeModo[0])
        Button_SetCheck(hRedeModo[0], BST_CHECKED);
    RedeLocalMostrar(0);
}

/* Devolve 1 se o comando pertencia ao painel. */
static int RedeLocalComando(HWND hwnd, UINT id, UINT codigo)
{
    if (codigo != BN_CLICKED)
        return 0;
    switch (id)
    {
    case IDC_REDE_MODO_LAN:
        RedeDefinirModo(REDE_MODO_LAN);
        return 1;
    case IDC_REDE_MODO_TRACE:
        RedeDefinirModo(REDE_MODO_TRACE);
        return 1;
    case IDC_REDE_MODO_DNS:
        RedeDefinirModo(REDE_MODO_DNS);
        return 1;
    case IDC_LAN_SCAN:
        LanIniciarPesquisa(hwnd);
        return 1;
    case IDC_LAN_ACEITAR:
        LanAceitarNovos();
        return 1;
    case IDC_TRACE_GO:
        TraceIniciarOuParar(hwnd);
        return 1;
    case IDC_DNS_GO:
        DnsIniciar(hwnd);
        return 1;
    }
    return 0;
}

static void RedeLocalMensagem(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LAN_SCAN_DONE:
        LanScanConcluido();
        break;
    case WM_TRACE_HOP:
        if (lParam)
        {
            TraceMostrarHop((const TraceHopMsg *)lParam);
            free((void *)lParam);
        }
        break;
    case WM_TRACE_DONE:
        TraceConcluido((int)wParam);
        break;
    case WM_DNS_RESULT:
        DnsMostrarResultado((DnsResultado *)lParam);
        break;
    }
}

static void CriarPainelLigacoes(HWND hwndPai)
{
    LVCOLUMNA lvc;
    HINSTANCE hInst = GetModuleHandle(NULL);
    static const struct
    {
        const char *nome;
        int larg;
        int fmt;
    } cols[] = {
        {"Processo", 170, LVCFMT_LEFT},
        {"PID", 60, LVCFMT_RIGHT},
        {"Proto", 56, LVCFMT_LEFT},
        {"Endereco local", 220, LVCFMT_LEFT},
        {"Endereco remoto", 220, LVCFMT_LEFT},
        {"Estado", 110, LVCFMT_LEFT},
        {"DNS inverso", 210, LVCFMT_LEFT}};
    int i;

    hPainelLig = CreateWindowExA(0, "STATIC", "Filtrar ligacoes",
                                 WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                                 0, 0, 74, 22, hwndPai, NULL, hInst, NULL);

    hEditLig = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                               WS_CHILD | ES_AUTOHSCROLL,
                               0, 0, 220, 22, hwndPai,
                               (HMENU)(UINT_PTR)IDC_EDIT_PESQUISA_CX, hInst, NULL);

    hChkLigAtivas = CreateWindowExA(0, "BUTTON", "Apenas estabelecidas",
                                    WS_CHILD | BS_AUTOCHECKBOX,
                                    0, 0, 160, 22, hwndPai,
                                    (HMENU)(UINT_PTR)IDC_CHK_CX_ATIVAS, hInst, NULL);

    hLabelLigCount = CreateWindowExA(0, "STATIC", "0 ligacoes",
                                     WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
                                     0, 0, 260, 22, hwndPai,
                                     (HMENU)(UINT_PTR)IDC_LABEL_CX_COUNT, hInst, NULL);

    /* sem LVS_NOSORTHEADER: os cabecalhos tem de ser clicaveis para ordenar */
    hListViewLig = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
                                   WS_CHILD | WS_VSCROLL |
                                       LVS_REPORT | LVS_SINGLESEL | LVS_OWNERDATA |
                                       LVS_SHOWSELALWAYS,
                                   0, 0, 100, 100, hwndPai,
                                   (HMENU)(UINT_PTR)IDC_LISTVIEW_CX, hInst, NULL);
    if (!hListViewLig)
        return;

    ListView_SetExtendedListViewStyle(hListViewLig,
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(hListViewLig, RGB(255, 255, 255));
    ListView_SetTextBkColor(hListViewLig, RGB(255, 255, 255));
    ListView_SetTextColor(hListViewLig, RGB(42, 48, 56));

    ZeroMemory(&lvc, sizeof(lvc));
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    for (i = 0; i < (int)(sizeof(cols) / sizeof(cols[0])); i++)
    {
        lvc.pszText = (LPSTR)cols[i].nome;
        lvc.cx = cols[i].larg;
        lvc.fmt = cols[i].fmt;
        ListView_InsertColumn(hListViewLig, i, &lvc);
    }
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
                                       0, "STATIC", "Pesquisar processos",
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

    /* Checkbox "Agrupar por nome" */
    hChkAgrupar = CreateWindowExA(
        0, "BUTTON", "Agrupar por nome",
        WS_CHILD | BS_AUTOCHECKBOX,
        0, 0, 140, 22,
        hwndPai,
        (HMENU)(UINT_PTR)IDC_CHK_AGRUPAR,
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
                                      LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(hListViewProc, RGB(255, 255, 255));
    ListView_SetTextBkColor(hListViewProc, RGB(255, 255, 255));
    ListView_SetTextColor(hListViewProc, RGB(42, 48, 56));

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
    lvc.cx = 90; lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 3, &lvc);

    lvc.pszText = "Download TCP"; lvc.cx = 105; lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 4, &lvc);
    lvc.pszText = "Upload TCP"; lvc.cx = 105; lvc.fmt = LVCFMT_RIGHT;
    ListView_InsertColumn(hListViewProc, 5, &lvc);
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
        ShowWindow(hSettingsTitle, abaAtual == 9 ? SW_SHOW : SW_HIDE);

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
            ShowWindow(hSettingsHeaders[i], abaAtual == 9 ? SW_SHOW : SW_HIDE);
        }

        for (j = 0; j < 4; j++)
        {
            if (hSettingsActions[i][j])
                ShowWindow(hSettingsActions[i][j],
                           (abaAtual == 9 && g_settingsOpen[i] && j < (i == 0 ? 2 : (i == 2 ? 3 : (i == 4 ? 4 : 1))))
                               ? SW_SHOW
                               : SW_HIDE);
        }
    }

    if (hSettingsActions[2][1])
        SetWindowTextA(hSettingsActions[2][1],
                       overlayAtivo ? "Desativar overlay" : "Ativar overlay");
    if (hSettingsActions[2][2])
        SetWindowTextA(hSettingsActions[2][2],
                       g_compactMode ? "Sair do modo compacto" : "Modo compacto");
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

        for (j = 0; j < 4; j++)
        {
            if (hSettingsActions[i][j])
                MoveWindow(hSettingsActions[i][j], x + 24, y,
                           (w > 270 ? 250 : w - 24), 24, TRUE);
            if (g_settingsOpen[i] && (j < (i == 0 ? 2 : (i == 2 ? 3 : (i == 4 ? 4 : 1)))))
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
    const char *actions[SETTINGS_SECTIONS][4] = {
        {"Configurar limites de alerta", "Historico de alertas", NULL, NULL},
        {"Estilo e cores da janela principal", NULL, NULL, NULL},
        {"Configurar overlay", "Ativar overlay", "Modo compacto", NULL},
        {"Personalizar Tray", NULL, NULL, NULL},
        {"Mudar intervalo de atualizacao", "Iniciar/parar log CSV", "Exportar snapshot", "Atalhos e ajuda"}};
    const int headerIds[SETTINGS_SECTIONS] = {
        IDC_SETTINGS_HDR_ALERT, IDC_SETTINGS_HDR_APAR, IDC_SETTINGS_HDR_OVER,
        IDC_SETTINGS_HDR_TRAY, IDC_SETTINGS_HDR_DADOS};
    const int actionIds[SETTINGS_SECTIONS][4] = {
        {IDC_SETTINGS_ALERT, IDC_SETTINGS_ALERT_HISTORY, 0, 0},
        {IDC_SETTINGS_APAR, 0, 0, 0},
        {IDC_SETTINGS_OV_CFG, IDC_SETTINGS_OV_TOGGLE, IDC_SETTINGS_COMPACT, 0},
        {IDC_SETTINGS_TRAY, 0, 0, 0},
        {IDC_SETTINGS_INTERVAL, IDC_SETTINGS_LOG, IDC_SETTINGS_SNAPSHOT, ID_SETTINGS_HELP}};
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

        for (j = 0; j < 4; j++)
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
        "Resumo", "CPU", "Memoria", "Temperatura", "Disco", "Rede", "GPU", "Processos", "Ligacoes", "Definicoes", "Rede Local"};
    int i;

    hTab = CreateWindowExA(
        0, WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT;

    for (i = 0; i < NUM_ABAS; i++)
    {
        item.pszText = (LPSTR)nomes[i];
        TabCtrl_InsertItem(hTab, i, &item);
    }

    {
        const char *navNomes[] = {
            "Visao geral", "CPU", "Memoria", "Temperatura", "Disco", "Rede", "GPU", "Processos", "Ligacoes", "Definicoes", "Rede Local"};

        for (i = 0; i < NUM_ABAS; i++)
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

    hAppTitle = CreateWindowExA(
        0, "STATIC", "WINMON",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        18, 12, NAV_LARGURA - 36, 28,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    hAppSubtitle = CreateWindowExA(
        0, "STATIC", "Monitor de sistema",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        18, 36, NAV_LARGURA - 36, 18,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    hAppStatus = CreateWindowExA(
        0, "STATIC", "[OK] Sistema normal",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 190, 24,
        hwnd, NULL, GetModuleHandle(NULL), NULL);

    if (hAppTitle && g_hValueFont)
        SendMessage(hAppTitle, WM_SETFONT, (WPARAM)g_hValueFont, TRUE);
    if (hAppSubtitle && hFontUI)
        SendMessage(hAppSubtitle, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    if (hAppStatus && hFontUI)
        SendMessage(hAppStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);

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
    hGraphTemp = CriarGrafico(hwnd, GRAPH_TEMP);
    hGraphDisk = CriarGrafico(hwnd, GRAPH_DISK);
    hGraphNet = CriarGrafico(hwnd, GRAPH_NET);
    hGraphGPU = CriarGrafico(hwnd, GRAPH_GPU);
    hGraphProcesses = CriarGrafico(hwnd, GRAPH_PROCESS);

    CriarPainelProcessos(hwnd);
    CriarPainelLigacoes(hwnd);
    CriarPainelRedeLocal(hwnd);
    CriarPainelDefinicoes(hwnd);

    hCompact = CreateWindowExA(
        0, "WinMonCompactClass", "",
        WS_CHILD | WS_VISIBLE,
        0, 0, COMPACT_WIDTH, COMPACT_HEIGHT,
        hwnd, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(hCompact, SW_HIDE);

    hBtnOverlay = CreateWindowExA(
        0, "BUTTON", "Overlay [Ctrl+Shift+O]",
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


    hBtnGraphPNG = CreateWindowExA(
        0, "BUTTON", "PNG",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 58, 22,
        hwnd, (HMENU)(UINT_PTR)ID_EXPORT_GRAPH_PNG,
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
        SendMessage(hBtnGraphPNG, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    }

    MostrarAba(0);
}

