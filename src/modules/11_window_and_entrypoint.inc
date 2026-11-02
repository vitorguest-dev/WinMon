/* 11 window and entrypoint */
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
        hFontUI = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        g_hValueFont = CreateFontA(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

        CarregarHistoricoAlertas();

        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&icc);

        GetSystemInfo(&sysInfo);
        numProcessadores = (int)sysInfo.dwNumberOfProcessors;
        if (numProcessadores < 1)
            numProcessadores = 1;

        ZeroMemory(&historico, sizeof(historico));
        SessaoIniciar();
        PingWifiIniciar();

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

            /* Enumerar engines GPU expostos pelo WDDM, separados por tipo.
             * O Gestor de Tarefas mostra 3D, Copy, Encode e Decode somando
             * a utilizacao de todos os engines do mesmo tipo.
             * Os nomes dos tipos no path PDH sao:
             *   engtype_3D, engtype_Copy, engtype_VideoEncode, engtype_VideoDecode
             */
            {
                /* Padroes de filtragem por tipo de engine */
                static const struct
                {
                    int idx;
                    const char *engtype;
                } kEngTypes[GPU_ENG_COUNT] = {
                    {GPU_ENG_3D, "engtype_3D"},
                    {GPU_ENG_COPY, "engtype_Copy"},
                    {GPU_ENG_ENCODE, "engtype_VideoEncode"},
                    {GPU_ENG_DECODE, "engtype_VideoDecode"},
                };
                int t;

                /* Primeiro expandir todos os paths de GPU Engine de uma vez */
                DWORD gpuPathChars = 0;
                DWORD status = PdhExpandWildCardPathA(
                    NULL, "\\GPU Engine(*)\\Utilization Percentage",
                    NULL, &gpuPathChars, 0);
                if ((status == ERROR_SUCCESS || status == PDH_MORE_DATA) &&
                    gpuPathChars > 1 && gpuPathChars <= (1024UL * 1024UL))
                {
                    char *gpuPaths = (char *)malloc((size_t)gpuPathChars);
                    if (gpuPaths)
                    {
                        DWORD chars = gpuPathChars;
                        status = PdhExpandWildCardPathA(
                            NULL, "\\GPU Engine(*)\\Utilization Percentage",
                            gpuPaths, &chars, 0);
                        if (status == ERROR_SUCCESS)
                        {
                            /* Para cada tipo de engine, filtrar os paths
                             * que contem a substring do tipo correspondente */
                            for (t = 0; t < GPU_ENG_COUNT; t++)
                            {
                                const char *filterStr = kEngTypes[t].engtype;
                                int engIdx = kEngTypes[t].idx;
                                char *path = gpuPaths;

                                while (*path && numGpuCounters[engIdx] < MAX_GPU_COUNTERS)
                                {
                                    /* Verificar se este path pertence ao tipo */
                                    if (strstr(path, filterStr) != NULL)
                                    {
                                        PDH_HCOUNTER counter = NULL;
                                        if (PdhAddEnglishCounterA(hQuery, path, 0, &counter) == ERROR_SUCCESS && counter)
                                            hCounterGpu[engIdx][numGpuCounters[engIdx]++] = counter;
                                    }
                                    path += strlen(path) + 1;
                                }
                            }
                        }
                        free(gpuPaths);
                    }
                }
            }

            CpuFreqInicializar();
            GpuMemInicializar();

            PdhCollectQueryData(hQuery);
        }

        InicializarTemperaturaCPU();
        CarregarConfigIni();
        SincronizarUIAgrupamento();
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

        { WSADATA wd; if (WSAStartup(MAKEWORD(2,2),&wd)==0) { InitializeCriticalSection(&g_cxDnsLock); g_cxDnsLockReady=1; } }
        SessaoIniciar();
        SetTimer(hwnd, TIMER_ID, intervaloAtualizacaoMs, NULL);

        /* Registar hotkey global Ctrl+Shift+O para toggle overlay */
        if (!RegisterHotKey(hwnd, HOTKEY_ID_OVERLAY,
                            HOTKEY_MOD_OVERLAY, HOTKEY_VK_OVERLAY))
        {
        }
        if (!RegisterHotKey(hwnd, HOTKEY_ID_COMPACT,
                            HOTKEY_MOD_COMPACT, HOTKEY_VK_COMPACT))
        {
        }
        /* Ctrl+Shift+M cria um marcador temporal na sessao. */
        RegisterHotKey(hwnd, HOTKEY_ID_MARKER, HOTKEY_MOD_MARKER, HOTKEY_VK_MARKER);

        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        {
            HBRUSH bg = CreateSolidBrush(RGB(245, 247, 250));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);
        }
        {
            RECT rail = {0, 0, NAV_LARGURA, rc.bottom};
            HBRUSH side = CreateSolidBrush(RGB(20, 29, 40));
            FillRect(dc, &rail, side);
            DeleteObject(side);
        }
        return 1;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC dc = (HDC)wParam;
        HWND ctl = (HWND)lParam;
        if (ctl == hAppTitle || ctl == hAppSubtitle)
        {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, ctl == hAppTitle ? RGB(245, 248, 252) : RGB(145, 159, 174));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        if (ctl == hAppStatus)
        {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, alertaGlobalAtivo ? RGB(220, 92, 82) : RGB(78, 177, 124));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        break;
    }

    case WM_SIZE:
        RedimensionarConteudo(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 820;
        mmi->ptMinTrackSize.y = 540;
        return 0;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;

        if (dis && dis->CtlType == ODT_BUTTON &&
            dis->CtlID >= TAB_RESUMO && dis->CtlID <= TAB_REDE_LOCAL)
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

        /* Notificacoes do ListView de ligacoes */
        if (hListViewLig && hdr->hwndFrom == hListViewLig)
        {
            if (hdr->code == NM_CUSTOMDRAW)
            {
                NMLVCUSTOMDRAW *cd=(NMLVCUSTOMDRAW*)lParam;
                if(cd->nmcd.dwDrawStage==CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if(cd->nmcd.dwDrawStage==CDDS_ITEMPREPAINT) {
                    int vi=(int)cd->nmcd.dwItemSpec;
                    if(vi>=0 && vi<g_cxVisiveis && g_conexoes[g_cxIdx[vi]].portaIncomum) { cd->clrText=RGB(180,55,35); cd->clrTextBk=RGB(255,238,232); }
                    return CDRF_DODEFAULT;
                }
            }
            if (hdr->code == LVN_GETDISPINFOA)
            {
                LvLigGetDispInfo((NMLVDISPINFOA *)lParam);
                return 0;
            }
            if (hdr->code == LVN_COLUMNCLICK)
            {
                NMLISTVIEW *pnm = (NMLISTVIEW *)lParam;
                LvLigColumnClick(pnm->iSubItem);
                return 0;
            }
            if (hdr->code == NM_RCLICK)
            {
                NMITEMACTIVATE *pnm = (NMITEMACTIVATE *)lParam;
                if (pnm->iItem >= 0)
                    MenuContextoLigacao(hwnd, pnm->iItem);
                return 0;
            }
            if (hdr->code == NM_DBLCLK)
            {
                NMITEMACTIVATE *pnm = (NMITEMACTIVATE *)lParam;
                AbrirDetalheLigacao(pnm->iItem);
                return 0;
            }
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
                    if (g_lvAgrupar && g_lvProcessos[pnm->iItem].instancias > 1)
                        MostrarInstanciasGrupo(g_lvProcessos[pnm->iItem].exeFile);
                    else
                        AbrirDetalheProcesso(g_lvProcessos[pnm->iItem].pid,
                                             g_lvProcessos[pnm->iItem].exeFile);
                }
                return 0;
            }

            /* --- NOVO CÓDIGO: Menu de Contexto (Right-Click) --- */
            if (hdr->code == NM_RCLICK)
            {
                NMITEMACTIVATE *pnm = (NMITEMACTIVATE *)lParam;
                if (pnm->iItem != -1 && pnm->iItem < g_lvTotal)
                {
                    if (g_lvAgrupar && g_lvProcessos[pnm->iItem].instancias > 1)
                    {
                        MenuContextoGrupo(hwnd, &g_lvProcessos[pnm->iItem]);
                        return 0;
                    }

                    DWORD pid = g_lvProcessos[pnm->iItem].pid;
                    char pidStr[16];
                    snprintf(pidStr, sizeof(pidStr), "%u", pid);
                    char exeNome[MAX_PATH];
                    strncpy_s(exeNome, sizeof(exeNome), g_lvProcessos[pnm->iItem].exeFile, _TRUNCATE);

                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuA(hMenu, MF_STRING, 1, "Terminar Processo");
                    AppendMenuA(hMenu, MF_STRING, 2, "Definir Prioridade Alta");
                    AppendMenuA(hMenu, MF_STRING, 3, "Abrir Localizacao do Ficheiro");
                    AppendMenuA(hMenu, MF_STRING, 4, "Copiar PID");
                    AppendMenuA(hMenu, MF_STRING, 5, "Ver ligacoes deste processo");

                    POINT pt;
                    GetCursorPos(&pt);
                    UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    DestroyMenu(hMenu);

                    HANDLE hProc;
                    switch (cmd)
                    {
                    case 1:
                        if (MessageBoxA(hwnd, "Terminar este processo pode causar instabilidade. Deseja continuar?", "Aviso", MB_YESNO | MB_ICONWARNING) == IDYES)
                        {
                            hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                            if (hProc)
                            {
                                TerminateProcess(hProc, 0);
                                CloseHandle(hProc);
                            }
                        }
                        break;
                    case 2:
                        hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
                        if (hProc)
                        {
                            SetPriorityClass(hProc, HIGH_PRIORITY_CLASS);
                            CloseHandle(hProc);
                        }
                        break;
                    case 3:
                        hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
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
                    case 4:
                        if (OpenClipboard(hwnd))
                        {
                            EmptyClipboard();
                            HGLOBAL hGlb = GlobalAlloc(GMEM_MOVEABLE, strlen(pidStr) + 1);
                            if (hGlb)
                            {
                                memcpy(GlobalLock(hGlb), pidStr, strlen(pidStr) + 1);
                                GlobalUnlock(hGlb);
                                SetClipboardData(CF_TEXT, hGlb);
                            }
                            CloseClipboard();
                        }
                        break;
                    case 5:
                        MostrarLigacoesDoProcesso(exeNome);
                        break;
                    }
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
                            ID_TOGGLE_OVERLAY, "Overlay [Ctrl+Shift+O]");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_ESTILO_MAIN, "Estilo e cores... [Ctrl+E]");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_TRAY, "Configurar tray...");
                AppendMenuA(hMenu, MF_STRING, ID_CONFIG_SETTINGS, "Definicoes... [Ctrl+D]");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

                AppendMenuA(hMenu, MF_STRING, ID_TOGGLE_LOG,
                            logAtivo ? "Parar log CSV" : "Iniciar log CSV");
                AppendMenuA(hMenu, MF_STRING, ID_EXPORT_SNAPSHOT, "Exportar snapshot");
                AppendMenuA(hMenu, MF_STRING, ID_TOGGLE_COMPACT,
                            g_compactMode ? "Sair do modo compacto" : "Modo compacto");
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

    case WM_LAN_SCAN_DONE:
    case WM_TRACE_HOP:
    case WM_TRACE_DONE:
    case WM_DNS_RESULT:
        RedeLocalMensagem(uMsg, wParam, lParam);
        return 0;

    case WM_COMMAND:
        if (RedeLocalComando(hwnd, LOWORD(wParam), HIWORD(wParam)))
            return 0;
        if (LOWORD(wParam) >= TAB_RESUMO &&
            LOWORD(wParam) <= TAB_REDE_LOCAL &&
            HIWORD(wParam) == BN_CLICKED)
        {
            int indice = LOWORD(wParam) - TAB_RESUMO;
            TabCtrl_SetCurSel(hTab, indice);
            MostrarAba(indice);
            return 0;
        }
        if (LOWORD(wParam) == IDC_CHK_CX_ATIVAS &&
            HIWORD(wParam) == BN_CLICKED)
        {
            AtualizarLigacoes();
            return 0;
        }
        if (LOWORD(wParam) == IDC_EDIT_PESQUISA_CX &&
            HIWORD(wParam) == EN_CHANGE)
        {
            AtualizarLigacoes();
            return 0;
        }
        if (LOWORD(wParam) == IDC_CHK_AGRUPAR &&
            HIWORD(wParam) == BN_CLICKED)
        {
            DefinirAgrupamentoProcessos(
                Button_GetCheck(hChkAgrupar) == BST_CHECKED, 1);
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
        if (LOWORD(wParam) == IDC_SETTINGS_ALERT_HISTORY)
        {
            MostrarDialogoHistoricoAlertas(hwnd);
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
        if (LOWORD(wParam) == ID_SETTINGS_HELP)
        {
            MostrarAjudaAtalhos(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SETTINGS_COMPACT)
        {
            ToggleCompactMode();
            return 0;
        }
        if (LOWORD(wParam) == ID_EXPORT_GRAPH_PNG)
        {
            if (abaAtual >= 1 && abaAtual <= 6)
            {
                HWND graph = (abaAtual == 1) ? hGraphCPU : (abaAtual == 2) ? hGraphRAM
                                                       : (abaAtual == 3)   ? hGraphTemp
                                                       : (abaAtual == 4)   ? hGraphDisk
                                                       : (abaAtual == 5)   ? hGraphNet
                                                                           : hGraphGPU;
                ExportarGraficoPNG(graph);
            }
            return 0;
        }
        if (LOWORD(wParam) == ID_TOGGLE_COMPACT)
        {
            ToggleCompactMode();
            return 0;
        }
        if (LOWORD(wParam) == ID_EXPORT_SNAPSHOT)
        {
            ExportarSnapshot();
            return 0;
        }
        if (LOWORD(wParam) == ID_SESSION_EXPORT)
        {
            SessaoExportarRelatorio();
            return 0;
        }
        if (LOWORD(wParam) == ID_SESSION_RESET)
        {
            if (MessageBoxA(hwnd, "Reiniciar as estatisticas da sessao atual?", "WinMon", MB_YESNO | MB_ICONQUESTION) == IDYES)
                SessaoResetar();
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
            /* aba Definicoes (antes abria por engano a aba Processos) */
            TabCtrl_SetCurSel(hTab, 9);
            MostrarAba(9);
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
        else if ((int)wParam == HOTKEY_ID_COMPACT)
        {
            ToggleCompactMode();
            AtualizarDefinicoesVisibilidade();
        }
        else if ((int)wParam == HOTKEY_ID_MARKER)
        {
            SessaoAdicionarMarcador("Marcador manual");
            EnviarBalloon("WinMon", "Marcador de sessao registado (Ctrl+Shift+M).");
        }
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID)
            AtualizarMonitor();
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID_OVERLAY);
        UnregisterHotKey(hwnd, HOTKEY_ID_COMPACT);
        UnregisterHotKey(hwnd, HOTKEY_ID_MARKER);
        KillTimer(hwnd, TIMER_ID);
        /* Nao destruir a lock nem finalizar Winsock enquanto um DNS inverso
           assíncrono ainda estiver em curso. */
        { int espera=0; while(InterlockedCompareExchange(&g_cxDnsThreads,0,0)>0 && espera<200) { Sleep(10); espera++; } }
        if(g_cxDnsLockReady && InterlockedCompareExchange(&g_cxDnsThreads,0,0)==0) { DeleteCriticalSection(&g_cxDnsLock); g_cxDnsLockReady=0; WSACleanup(); }
        PingWifiParar();
        free(g_sessionSamples); g_sessionSamples = NULL; g_sessionSampleCount = 0;
        if (g_sessionLockReady) { DeleteCriticalSection(&g_sessionLock); g_sessionLockReady = 0; }

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

static void AplicarVisibilidadeModoNormal(void)
{
    int i, j;

    if (!hMainWindow)
        return;

    for (i = 0; i < NUM_ABAS; i++)
        if (hNav[i])
            ShowWindow(hNav[i], SW_SHOW);

    if (hTab)
        ShowWindow(hTab, SW_SHOW);
    if (hEdit)
        ShowWindow(hEdit, SW_SHOW);
    if (hDashboard)
        ShowWindow(hDashboard, SW_SHOW);
    if (hBtnOverlay)
        ShowWindow(hBtnOverlay, SW_SHOW);
    if (hBtnInterval)
        ShowWindow(hBtnInterval, SW_SHOW);
    if (hBtnEstiloMain)
        ShowWindow(hBtnEstiloMain, SW_SHOW);
    if (hBtnGraphPNG)
        ShowWindow(hBtnGraphPNG, SW_SHOW);

    if (hGraphCPU)
        ShowWindow(hGraphCPU, SW_SHOW);
    if (hGraphRAM)
        ShowWindow(hGraphRAM, SW_SHOW);
    if (hGraphTemp)
        ShowWindow(hGraphTemp, SW_SHOW);
    if (hGraphDisk)
        ShowWindow(hGraphDisk, SW_SHOW);
    if (hGraphNet)
        ShowWindow(hGraphNet, SW_SHOW);
    if (hGraphGPU)
        ShowWindow(hGraphGPU, SW_SHOW);
    if (hGraphProcesses)
        ShowWindow(hGraphProcesses, SW_SHOW);

    if (hPainelProcessos)
        ShowWindow(hPainelProcessos, SW_SHOW);
    if (hEditPesquisaProc)
        ShowWindow(hEditPesquisaProc, SW_SHOW);
    if (hLabelProcCount)
        ShowWindow(hLabelProcCount, SW_SHOW);
    if (hChkAgrupar)
        ShowWindow(hChkAgrupar, SW_SHOW);
    if (hListViewProc)
        ShowWindow(hListViewProc, SW_SHOW);

    if (hSettingsTitle)
        ShowWindow(hSettingsTitle, SW_SHOW);
    for (i = 0; i < SETTINGS_SECTIONS; i++)
    {
        if (hSettingsHeaders[i])
            ShowWindow(hSettingsHeaders[i], SW_SHOW);
        for (j = 0; j < 4; j++)
            if (hSettingsActions[i][j])
                ShowWindow(hSettingsActions[i][j], SW_SHOW);
    }

    {
        HWND hBtnCfg = GetDlgItem(hMainWindow, ID_CONFIG_OVERLAY);
        if (hBtnCfg)
            ShowWindow(hBtnCfg, SW_SHOW);
    }
}

static void ToggleCompactMode(void)
{
    RECT wa;

    if (!hMainWindow)
        return;

    if (!g_compactMode)
    {
        GetWindowRect(hMainWindow, &g_normalRect);
        g_normalStyle = GetWindowLongPtr(hMainWindow, GWL_STYLE);
        g_normalExStyle = GetWindowLongPtr(hMainWindow, GWL_EXSTYLE);

        g_compactMode = 1;
        if (hTab)
            ShowWindow(hTab, SW_HIDE);
        if (hEdit)
            ShowWindow(hEdit, SW_HIDE);
        if (hDashboard)
            ShowWindow(hDashboard, SW_HIDE);
        if (hAppTitle)
            ShowWindow(hAppTitle, SW_HIDE);
        if (hAppSubtitle)
            ShowWindow(hAppSubtitle, SW_HIDE);
        if (hAppStatus)
            ShowWindow(hAppStatus, SW_HIDE);
        if (hBtnOverlay)
            ShowWindow(hBtnOverlay, SW_HIDE);
        if (hBtnInterval)
            ShowWindow(hBtnInterval, SW_HIDE);
        if (hBtnEstiloMain)
            ShowWindow(hBtnEstiloMain, SW_HIDE);
        if (hBtnGraphPNG)
            ShowWindow(hBtnGraphPNG, SW_HIDE);
        for (int i = 0; i < NUM_ABAS; i++)
            if (hNav[i])
                ShowWindow(hNav[i], SW_HIDE);
        if (hGraphCPU)
            ShowWindow(hGraphCPU, SW_HIDE);
        if (hGraphRAM)
            ShowWindow(hGraphRAM, SW_HIDE);
        if (hGraphTemp)
            ShowWindow(hGraphTemp, SW_HIDE);
        if (hGraphDisk)
            ShowWindow(hGraphDisk, SW_HIDE);
        if (hGraphNet)
            ShowWindow(hGraphNet, SW_HIDE);
        if (hGraphGPU)
            ShowWindow(hGraphGPU, SW_HIDE);
        if (hGraphProcesses)
            ShowWindow(hGraphProcesses, SW_HIDE);
        if (hPainelProcessos)
            ShowWindow(hPainelProcessos, SW_HIDE);
        if (hEditPesquisaProc)
            ShowWindow(hEditPesquisaProc, SW_HIDE);
        if (hLabelProcCount)
            ShowWindow(hLabelProcCount, SW_HIDE);
        if (hChkAgrupar)
            ShowWindow(hChkAgrupar, SW_HIDE);
        if (hListViewProc)
            ShowWindow(hListViewProc, SW_HIDE);
        if (hPainelLig)
            ShowWindow(hPainelLig, SW_HIDE);
        if (hEditLig)
            ShowWindow(hEditLig, SW_HIDE);
        if (hChkLigAtivas)
            ShowWindow(hChkLigAtivas, SW_HIDE);
        if (hLabelLigCount)
            ShowWindow(hLabelLigCount, SW_HIDE);
        if (hListViewLig)
            ShowWindow(hListViewLig, SW_HIDE);
        if (hSettingsTitle)
            ShowWindow(hSettingsTitle, SW_HIDE);
        for (int i = 0; i < SETTINGS_SECTIONS; i++)
            for (int j = 0; j < 4; j++)
                if (hSettingsActions[i][j])
                    ShowWindow(hSettingsActions[i][j], SW_HIDE);
        for (int i = 0; i < SETTINGS_SECTIONS; i++)
            if (hSettingsHeaders[i])
                ShowWindow(hSettingsHeaders[i], SW_HIDE);
        {
            HWND hBtnCfg = GetDlgItem(hMainWindow, ID_CONFIG_OVERLAY);
            if (hBtnCfg)
                ShowWindow(hBtnCfg, SW_HIDE);
        }

        SetWindowLongPtr(hMainWindow, GWL_STYLE,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
        SetWindowLongPtr(hMainWindow, GWL_EXSTYLE,
                         g_normalExStyle | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(hMainWindow, HWND_TOPMOST,
                     wa.right - COMPACT_WIDTH - 12,
                     wa.bottom - COMPACT_HEIGHT - 12,
                     COMPACT_WIDTH, COMPACT_HEIGHT,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        if (hCompact)
            ShowWindow(hCompact, SW_SHOW);
        SetWindowTextA(hMainWindow, alertaGlobalAtivo ? "WinMon — Compacto  [!]" : "WinMon — Compacto");
        AtualizarCompacto();
    }
    else
    {
        g_compactMode = 0;
        if (hCompact)
            ShowWindow(hCompact, SW_HIDE);
        SetWindowLongPtr(hMainWindow, GWL_STYLE, g_normalStyle);
        SetWindowLongPtr(hMainWindow, GWL_EXSTYLE, g_normalExStyle);
        SetWindowPos(hMainWindow, NULL,
                     g_normalRect.left, g_normalRect.top,
                     g_normalRect.right - g_normalRect.left,
                     g_normalRect.bottom - g_normalRect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
        AplicarVisibilidadeModoNormal();
        if (hAppTitle)
            ShowWindow(hAppTitle, SW_SHOW);
        if (hAppSubtitle)
            ShowWindow(hAppSubtitle, SW_SHOW);
        if (hAppStatus)
            ShowWindow(hAppStatus, SW_SHOW);
        SetWindowTextA(hMainWindow,
                       alertaGlobalAtivo ? "WinMon — Monitor de sistema  [!] ALERTA" : "WinMon — Monitor de sistema");
        MostrarAba(abaAtual);
        RedimensionarConteudo(hMainWindow);
    }
}

static void AtualizarCompacto(void)
{
    if (hCompact && g_compactMode)
        InvalidateRect(hCompact, NULL, FALSE);
}

static LRESULT CALLBACK CompactProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){(void)wParam;(void)lParam;switch(msg){case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT ps;HDC hdc=BeginPaint(hwnd,&ps);RECT rc;HBRUSH bg;HPEN pen;int i;const char*lab[6]={"CPU","RAM","TEMP","GPU","DISCO","REDE"};char val[6][64];GetClientRect(hwnd,&rc);bg=CreateSolidBrush(alertaGlobalAtivo?RGB(48,36,36):RGB(30,35,42));FillRect(hdc,&rc,bg);DeleteObject(bg);pen=CreatePen(PS_SOLID,alertaGlobalAtivo?3:1,alertaGlobalAtivo?RGB(220,45,45):RGB(82,92,105));{HPEN old=(HPEN)SelectObject(hdc,pen);Rectangle(hdc,1,1,rc.right-1,rc.bottom-1);SelectObject(hdc,old);}DeleteObject(pen);snprintf(val[0],64,"%.1f%%",ultimoCpuPercent);snprintf(val[1],64,"%.0f%%",ultimoRamPercent);snprintf(val[2],64,"%.1f C",ObterTemperaturaMaxima());snprintf(val[3],64,"%.1f%%",ultimoGpuPercent);snprintf(val[4],64,"%.1f MB/s",(ultimoDiskRead+ultimoDiskWrite)/1048576.0);snprintf(val[5],64,"%.1f MB/s",(ultimoNetDown+ultimoNetUp)/1048576.0);SetBkMode(hdc,TRANSPARENT);for(i=0;i<6;i++){int col=i%3,row=i/3;RECT r={10+col*((rc.right-30)/3),34+row*58,10+(col+1)*((rc.right-30)/3)-5,88+row*58};RECT a=r,b=r;a.bottom=a.top+17;b.top+=16;SetTextColor(hdc,RGB(130,155,180));DrawTextA(hdc,lab[i],-1,&a,DT_LEFT|DT_SINGLELINE|DT_VCENTER);SetTextColor(hdc,RGB(238,241,245));DrawTextA(hdc,val[i],-1,&b,DT_LEFT|DT_SINGLELINE|DT_VCENTER);}SetTextColor(hdc,alertaGlobalAtivo?RGB(255,110,100):RGB(150,158,168));{RECT t={10,7,rc.right-10,28};DrawTextA(hdc,alertaGlobalAtivo?"WinMon  •  ALERTA":"WinMon  •  Compacto",-1,&t,DT_LEFT|DT_SINGLELINE|DT_VCENTER);}SetTextColor(hdc,RGB(110,118,128));{RECT t={rc.right-170,7,rc.right-10,28};DrawTextA(hdc,"Duplo-clique: abrir",-1,&t,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);}EndPaint(hwnd,&ps);return 0;}case WM_LBUTTONDBLCLK:ToggleCompactMode();return 0;case WM_RBUTTONUP:ToggleCompactMode();return 0;case WM_SIZE:InvalidateRect(hwnd,NULL,FALSE);return 0;}return DefWindowProc(hwnd,msg,wParam,lParam);}

/* ------------------------------------------------------------------------- */
/* Historico de alertas                                                      */
/* ------------------------------------------------------------------------- */
static void PreencherListaAlertas(HWND hList)
{
    int i;
    char linha[192];
    const SYSTEMTIME *t;
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);
    /* mais recentes primeiro */
    for (i = g_alertHistoryCount - 1; i >= 0; i--)
    {
        t = &g_alertHistory[i].timestamp;
        snprintf(linha, sizeof(linha), "%02d/%02d/%04d %02d:%02d:%02d  |  %s  %.1f (limite %.1f) - Culpado: %s",
                 t->wDay, t->wMonth, t->wYear, t->wHour, t->wMinute, t->wSecond,
                 g_alertHistory[i].recurso, g_alertHistory[i].valor, g_alertHistory[i].limite, g_alertHistory[i].culpado);
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)linha);
    }
}

static LRESULT CALLBACK AlertHistoryProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hList = NULL;
    static unsigned revMostrada = 0;
    (void)lParam;
    switch (msg)
    {
    case WM_CREATE:
        hList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                                10, 10, 540, 300, hwnd, NULL, GetModuleHandle(NULL), NULL);
        CreateWindowExA(0, "BUTTON", "Limpar historico", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        10, 320, 130, 28, hwnd, (HMENU)(UINT_PTR)IDC_ALERT_HISTORY_CLEAR,
                        GetModuleHandle(NULL), NULL);
        PreencherListaAlertas(hList);
        revMostrada = g_alertHistoryRev;
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    case WM_TIMER:
        if (hList && revMostrada != g_alertHistoryRev)
        {
            PreencherListaAlertas(hList);
            revMostrada = g_alertHistoryRev;
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ALERT_HISTORY_CLEAR && HIWORD(wParam) == BN_CLICKED)
        {
            g_alertHistoryCount = 0;
            g_alertHistoryRev++;
            AlertaLimparFicheiro();
            if (hList)
            {
                PreencherListaAlertas(hList);
                revMostrada = g_alertHistoryRev;
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        KillTimer(hwnd, 1);
        hAlertHistory = NULL;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void MostrarDialogoHistoricoAlertas(HWND hwndPai)
{
    if (hAlertHistory && IsWindow(hAlertHistory))
    {
        ShowWindow(hAlertHistory, SW_SHOW);
        SetForegroundWindow(hAlertHistory);
        return;
    }
    hAlertHistory = CreateWindowExA(WS_EX_DLGMODALFRAME, "WinMonAlertHistoryClass",
                                    "WinMon — Historico de alertas",
                                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                                    CW_USEDEFAULT, CW_USEDEFAULT, 575, 390,
                                    hwndPai, NULL, GetModuleHandle(NULL), NULL);
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
        {FVIRTKEY | FCONTROL, 'D', ID_CONFIG_SETTINGS},
        {FVIRTKEY | FCONTROL, 'K', ID_TOGGLE_COMPACT}};
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
        WNDCLASSA compactClass;
        ZeroMemory(&compactClass, sizeof(compactClass));
        compactClass.lpfnWndProc = CompactProc;
        compactClass.hInstance = hInstance;
        compactClass.lpszClassName = "WinMonCompactClass";
        compactClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        compactClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&compactClass);
    }

    {
        WNDCLASSA alertClass;
        ZeroMemory(&alertClass, sizeof(alertClass));
        alertClass.lpfnWndProc = AlertHistoryProc;
        alertClass.hInstance = hInstance;
        alertClass.lpszClassName = "WinMonAlertHistoryClass";
        alertClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        alertClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassA(&alertClass);
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
        1180, 780,
        NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return 0;

    hAccel = CreateAcceleratorTableA(accels, 7);

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