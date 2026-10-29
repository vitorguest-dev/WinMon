/* 09 style dialogs */
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

