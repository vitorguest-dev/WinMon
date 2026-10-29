/* 04 ui and tabs */
/* ------------------------------------------------------------------------- */
/* RichEdit e Interface                                                      */
/* ------------------------------------------------------------------------- */

void AplicarCoresDeAlerta()
{
    CHARFORMAT2A cf;
    int i;

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;

    SendMessage(hEdit, EM_SETSEL, 0, -1);
    cf.crTextColor = g_mainConfig.corTextoEdit;
    cf.dwEffects = 0;
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    cf.crTextColor = RGB(200, 0, 0);

    for (i = 0; i < totalIntervalosAlerta; i++)
    {
        SendMessage(hEdit, EM_SETSEL,
                    intervalosAlerta[i].inicio,
                    intervalosAlerta[i].fim);
        SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }

    SendMessage(hEdit, EM_SETSEL, 0, 0);
}

static void AplicarEstiloJanelaPrincipal(void)
{
    if (!hEdit)
        return;

    HFONT hNovaFonte = CreateFontIndirectA(&g_mainConfig.fonteEdit);
    if (hNovaFonte)
    {
        if (hFontMonitor)
            DeleteObject(hFontMonitor);
        hFontMonitor = hNovaFonte;
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMonitor, TRUE);
    }

    SendMessage(hEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)g_mainConfig.corFundoEdit);

    CHARFORMAT2A cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(CHARFORMAT2A);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = g_mainConfig.corTextoEdit;

    SendMessage(hEdit, EM_SETSEL, 0, -1);
    SendMessage(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessage(hEdit, EM_SETSEL, 0, 0);

    AplicarCoresDeAlerta();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphTemp, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphGPU, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);
    InvalidateRect(hDashboard, NULL, FALSE);
}

void AtualizarTituloJanela()
{
    char titulo[160];
    const char *base = g_compactMode ? "WinMon — Compacto" : "WinMon — Hardware";
    snprintf(titulo, sizeof(titulo), "%s | CPU %.1f%% | RAM %.0f%%%s",
             base, ultimoCpuPercent, ultimoRamPercent,
             alertaGlobalAtivo ? " | [!] ALERTA" : "");
    SetWindowTextA(hMainWindow, titulo);
}

void AtualizarTooltipTray()
{
    /* Atualizar sempre o szTip (mesmo sem tray visível) para que fique
       pronto no momento em que o ícone for adicionado. */

    char linha[128];
    linha[0] = '\0';
    if (g_trayConfig.mostrarCpu)
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "CPU: %.1f%%", ultimoCpuPercent);
    if (g_trayConfig.mostrarRam)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "RAM: %.0f%%", ultimoRamPercent);
    }
    if (g_trayConfig.mostrarTemperatura && numZonasTemp > 0)
    {
        double t = 0;
        int i;
        for (i = 0; i < numZonasTemp; i++)
            t += tempAtual[i];
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "TEMP: %.0fC", t / numZonasTemp);
    }
    if (g_trayConfig.mostrarRede)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "NET: D %.1f/U %.1f MB/s", ultimoNetDown / 1048576.0, ultimoNetUp / 1048576.0);
    }
    if (g_trayConfig.mostrarDisco)
    {
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "DISCO: R %.1f/W %.1f MB/s", ultimoDiskRead / 1048576.0, ultimoDiskWrite / 1048576.0);
    }
    if (g_trayConfig.mostrarUptime)
    {
        ULONGLONG u = GetTickCount64() / 1000ULL;
        if (linha[0])
            strncat_s(linha, sizeof(linha), " | ", _TRUNCATE);
        snprintf(linha + strlen(linha), sizeof(linha) - strlen(linha), "UP: %lluh", (unsigned long long)(u / 3600ULL));
    }
    if (!linha[0])
        strcpy_s(linha, sizeof(linha), "Sem metricas selecionadas");
    snprintf(nid.szTip, sizeof(nid.szTip), "WinMon\r\n%s", linha);

    /* Só notifica o Shell se o ícone estiver visível */
    if (trayIconAtivo)
        Shell_NotifyIconA(NIM_MODIFY, &nid);
}

void ExportarSnapshot()
{
    SYSTEMTIME st;
    char nomeFicheiro[MAX_PATH];
    FILE *f = NULL;
    errno_t err;
    char msg[MAX_PATH + 64];

    GetLocalTime(&st);

    snprintf(nomeFicheiro, sizeof(nomeFicheiro),
             "snapshot_%04d%02d%02d_%02d%02d%02d.txt",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);

    err = fopen_s(&f, nomeFicheiro, "wb");

    if (err == 0 && f != NULL)
    {
        fwrite(ultimoSnapshot, 1, strlen(ultimoSnapshot), f);
        fclose(f);

        snprintf(msg, sizeof(msg),
                 "Snapshot gravado como:\r\n%s", nomeFicheiro);

        MessageBoxA(hMainWindow, msg,
                    "Export concluido", MB_OK | MB_ICONINFORMATION);
    }
    else
    {
        MessageBoxA(hMainWindow,
                    "Nao foi possivel gravar o ficheiro de snapshot.",
                    "Erro", MB_OK | MB_ICONERROR);
    }
}

static void ExportarGraficoPNG(HWND hwndGraph)
{
    OPENFILENAMEA ofn;
    char nome[MAX_PATH] = "winmon_grafico.png";
    HDC screen = NULL;
    HDC mem = NULL;
    HBITMAP bmp = NULL;
    HBITMAP oldBmp = NULL;
    RECT rc;
    BITMAPINFO bi;
    BYTE *pixels = NULL;
    WCHAR wpath[MAX_PATH];
    size_t wlen;
    HRESULT hr;
    HRESULT cohr;
    int coNeedUninit = 0;
    IWICImagingFactory *factory = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IWICStream *stream = NULL;
    GUID format;
    DWORD stride;
    DWORD sizeBytes;
    int success = 0;
    UINT width;
    UINT height;

    if (!hwndGraph)
        return;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFile = nome;
    ofn.nMaxFile = sizeof(nome);
    ofn.lpstrFilter = "PNG (*.png)\0*.png\0Todos os ficheiros (*.*)\0*.*\0";
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameA(&ofn))
        return;

    GetClientRect(hwndGraph, &rc);
    width = (UINT)(rc.right - rc.left);
    height = (UINT)(rc.bottom - rc.top);
    if (width < 1 || height < 1)
        return;

    screen = GetDC(hwndGraph);
    mem = CreateCompatibleDC(screen);
    bmp = CreateCompatibleBitmap(screen, (int)width, (int)height);
    if (!mem || !bmp)
        goto cleanup;

    oldBmp = (HBITMAP)SelectObject(mem, bmp);
    PaintGraph(hwndGraph, mem,
               (GraphType)(GetWindowLongPtr(hwndGraph, GWLP_USERDATA) - 1));
    SelectObject(mem, oldBmp);
    oldBmp = NULL;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)width;
    bi.bmiHeader.biHeight = -(LONG)height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    stride = width * 4;
    sizeBytes = stride * height;
    pixels = (BYTE *)malloc(sizeBytes);
    if (!pixels)
        goto cleanup;
    if (GetDIBits(mem, bmp, 0, height, pixels, &bi, DIB_RGB_COLORS) == 0)
        goto cleanup;

    {
        DWORD px;
        for (px = 3; px < sizeBytes; px += 4)
            pixels[px] = 255;
    }

    wlen = MultiByteToWideChar(CP_ACP, 0, nome, -1, wpath, MAX_PATH);
    if (wlen == 0)
        goto cleanup;

    cohr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (cohr == S_OK || cohr == S_FALSE)
        coNeedUninit = 1;
    else if (cohr != RPC_E_CHANGED_MODE)
        goto cleanup;

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWICImagingFactory, (LPVOID *)&factory);
    if (FAILED(hr))
        goto cleanup;

    hr = factory->lpVtbl->CreateStream(factory, &stream);
    if (FAILED(hr))
        goto cleanup;
    hr = stream->lpVtbl->InitializeFromFilename(stream, wpath, GENERIC_WRITE);
    if (FAILED(hr))
        goto cleanup;

    hr = factory->lpVtbl->CreateEncoder(factory, &GUID_ContainerFormatPng,
                                        NULL, &encoder);
    if (FAILED(hr))
        goto cleanup;
    hr = encoder->lpVtbl->Initialize(encoder, (IStream *)stream,
                                     WICBitmapEncoderNoCache);
    if (FAILED(hr))
        goto cleanup;

    hr = encoder->lpVtbl->CreateNewFrame(encoder, &frame, NULL);
    if (FAILED(hr))
        goto cleanup;
    hr = frame->lpVtbl->Initialize(frame, NULL);
    if (FAILED(hr))
        goto cleanup;
    hr = frame->lpVtbl->SetSize(frame, width, height);
    if (FAILED(hr))
        goto cleanup;

    format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->lpVtbl->SetPixelFormat(frame, &format);
    if (FAILED(hr))
        goto cleanup;
    hr = frame->lpVtbl->WritePixels(frame, height, stride, sizeBytes, pixels);
    if (FAILED(hr))
        goto cleanup;
    hr = frame->lpVtbl->Commit(frame);
    if (FAILED(hr))
        goto cleanup;
    hr = encoder->lpVtbl->Commit(encoder);
    if (FAILED(hr))
        goto cleanup;
    success = 1;

cleanup:
    if (success)
        MessageBoxA(hMainWindow, "Grafico exportado para PNG.",
                    "Exportar grafico", MB_OK | MB_ICONINFORMATION);
    else if (ofn.lpstrFile[0] && nome[0])
        MessageBoxA(hMainWindow, "Nao foi possivel exportar o grafico para PNG.",
                    "Erro ao exportar grafico", MB_OK | MB_ICONERROR);
    if (frame)
        frame->lpVtbl->Release(frame);
    if (encoder)
        encoder->lpVtbl->Release(encoder);
    if (stream)
        stream->lpVtbl->Release(stream);
    if (factory)
        factory->lpVtbl->Release(factory);
    if (coNeedUninit)
        CoUninitialize();
    if (pixels)
        free(pixels);
    if (oldBmp)
        SelectObject(mem, oldBmp);
    if (bmp)
        DeleteObject(bmp);
    if (mem)
        DeleteDC(mem);
    if (screen)
        ReleaseDC(hwndGraph, screen);
}

/* ------------------------------------------------------------------------- */
/* Abas                                                                      */
/* ------------------------------------------------------------------------- */

static void MostrarAba(int indice)
{
    int i;

    if (g_compactMode)
        return;
    /* índices 1-6 mapeiam para os gráficos; índice 7 é o painel de processos */
    HWND graficos[] = {
        hGraphCPU, hGraphRAM, hGraphTemp, hGraphDisk, hGraphNet, hGraphGPU};

    abaAtual = indice;

    for (i = 0; i < NUM_ABAS; i++)
    {
        if (hNav[i])
            SendMessage(hNav[i], BM_SETSTATE, i == indice, 0);
    }

    if (hEdit)
        ShowWindow(hEdit, indice == 0 ? SW_SHOW : SW_HIDE);

    if (hDashboard)
        ShowWindow(hDashboard, indice == 0 ? SW_SHOW : SW_HIDE);

    for (i = 0; i < 6; i++)
    {
        if (graficos[i])
            ShowWindow(graficos[i], indice == i + 1 ? SW_SHOW : SW_HIDE);
    }

    /* hGraphProcesses não tem aba própria — mantém-se sempre oculto */
    if (hGraphProcesses)
        ShowWindow(hGraphProcesses, SW_HIDE);

    if (hBtnGraphPNG)
        ShowWindow(hBtnGraphPNG, (indice >= 1 && indice <= 6) ? SW_SHOW : SW_HIDE);

    /* painel de lista de processos: mostrar/esconder cada controlo */
    {
        int visProc = (indice == 7) ? SW_SHOW : SW_HIDE;
        if (hPainelProcessos)
            ShowWindow(hPainelProcessos, visProc);
        if (hEditPesquisaProc)
            ShowWindow(hEditPesquisaProc, visProc);
        if (hLabelProcCount)
            ShowWindow(hLabelProcCount, visProc);
        if (hChkAgrupar)
            ShowWindow(hChkAgrupar, visProc);
        if (hListViewProc)
            ShowWindow(hListViewProc, visProc);
    }

    /* painel de ligacoes (aba 8) */
    {
        int visLig = (indice == 8) ? SW_SHOW : SW_HIDE;
        if (hPainelLig)
            ShowWindow(hPainelLig, visLig);
        if (hEditLig)
            ShowWindow(hEditLig, visLig);
        if (hChkLigAtivas)
            ShowWindow(hChkLigAtivas, visLig);
        if (hLabelLigCount)
            ShowWindow(hLabelLigCount, visLig);
        if (hListViewLig)
            ShowWindow(hListViewLig, visLig);
    }

    /* painel Rede Local (aba 10) */
    {
        RedeLocalMostrar(indice == 10);
    }

    AtualizarDefinicoesVisibilidade();
    InvalidateRect(hMainWindow, NULL, TRUE);

    if (indice == 8)
        AtualizarLigacoes(); /* dados frescos ao abrir a aba */
}


static void RedimensionarConteudo(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    if (g_compactMode)
    {
        if (hCompact)
            MoveWindow(hCompact, 0, 0, rc.right, rc.bottom, TRUE);
        return;
    }

    /*
     * Posições calculadas da direita para a esquerda (com margem de 4px):
     * 1. Overlay [Ctrl+Shift+O]     : Largura 118 -> X = rc.right - 122
     * 2. Engrenagem (CFG)     : Largura 26  -> X = rc.right - 152
     * 3. Atualizacao: X s     : Largura 116 -> X = rc.right - 272
     * 4. Definicoes [Ctrl+D]  : Largura 116 -> X = rc.right - 392
     */

    if (hAppStatus)
        MoveWindow(hAppStatus, NAV_LARGURA + 18, 4, 190, 24, TRUE);

    if (hBtnOverlay)
        MoveWindow(hBtnOverlay, rc.right - 132, 4, 128, 24, TRUE);

    {
        HWND hBtnCfg = GetDlgItem(hwnd, ID_CONFIG_OVERLAY);
        if (hBtnCfg)
            MoveWindow(hBtnCfg, rc.right - 166, 4, 30, 24, TRUE);
    }

    if (hBtnInterval)
        MoveWindow(hBtnInterval, rc.right - 288, 4, 116, 24, TRUE);

    if (hBtnEstiloMain)
        MoveWindow(hBtnEstiloMain, rc.right - 408, 4, 116, 24, TRUE);

    if (hBtnGraphPNG)
        MoveWindow(hBtnGraphPNG, rc.right - 468, 4, 58, 24, TRUE);

    if (hTab)
    {
        MoveWindow(hTab, NAV_LARGURA, 0,
                   rc.right - NAV_LARGURA, rc.bottom, TRUE);

        TabCtrl_AdjustRect(hTab, FALSE, &rc);
        rc.left += NAV_LARGURA;

        for (int i = 0; i < NUM_ABAS; i++)
        {
            if (hNav[i])
                MoveWindow(hNav[i], 12, 66 + i * 39,
                           NAV_LARGURA - 24, 34, TRUE);
        }

        if (hEdit)
            MoveWindow(hEdit, rc.left, rc.top + 230,
                       rc.right - rc.left,
                       rc.bottom - rc.top - 230, TRUE);

        if (hDashboard)
            MoveWindow(hDashboard, rc.left, rc.top,
                       rc.right - rc.left, 230, TRUE);

        if (hGraphCPU)
            MoveWindow(hGraphCPU, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphRAM)
            MoveWindow(hGraphRAM, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphTemp)
            MoveWindow(hGraphTemp, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphDisk)
            MoveWindow(hGraphDisk, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphNet)
            MoveWindow(hGraphNet, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphGPU)
            MoveWindow(hGraphGPU, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        if (hGraphProcesses)
            MoveWindow(hGraphProcesses, rc.left, rc.top,
                       rc.right - rc.left, rc.bottom - rc.top, TRUE);

        /* Barra de pesquisa + ListView de processos */
        RedimensionarDefinicoes(&rc);

        {
            int cx = rc.left;
            int cy = rc.top;
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;
            int barH = 28;   /* altura da barra de pesquisa */
            int lblW = 74;   /* "Pesquisar:" */
            int editW = 240; /* caixa de texto */
            if (editW > cw - lblW - 8)
                editW = cw - lblW - 8;
            if (editW < 60)
                editW = 60;

            if (hPainelProcessos) /* label "Pesquisar:" */
                MoveWindow(hPainelProcessos,
                           cx + 4, cy + 4, lblW, 22, TRUE);

            if (hEditPesquisaProc)
                MoveWindow(hEditPesquisaProc,
                           cx + 4 + lblW + 4, cy + 3, editW, 22, TRUE);

            {
                int chkW = 140;
                int xChk = cx + 4 + lblW + 4 + editW + 8;
                int xCnt = xChk + chkW + 4;
                int wCnt = cx + cw - xCnt - 4;
                if (wCnt < 0)
                    wCnt = 0;

                if (hChkAgrupar)
                    MoveWindow(hChkAgrupar, xChk, cy + 4, chkW, 22, TRUE);
                if (hLabelProcCount)
                    MoveWindow(hLabelProcCount, xCnt, cy + 4, wCnt, 22, TRUE);
            }

            if (hListViewProc && ch > barH)
                MoveWindow(hListViewProc,
                           cx, cy + barH,
                           cw, ch - barH, TRUE);
        }

        /* Barra de filtro + ListView de ligacoes */
        {
            int cx = rc.left;
            int cy = rc.top;
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;
            int barH = 28;
            int lblW = 74;
            int editW = 240;
            int chkW = 160;
            int xChk, xCnt, wCnt;

            if (editW > cw - lblW - 8)
                editW = cw - lblW - 8;
            if (editW < 60)
                editW = 60;

            xChk = cx + 4 + lblW + 4 + editW + 8;
            xCnt = xChk + chkW + 4;
            wCnt = cx + cw - xCnt - 4;
            if (wCnt < 0)
                wCnt = 0;

            if (hPainelLig)
                MoveWindow(hPainelLig, cx + 4, cy + 4, lblW, 22, TRUE);
            if (hEditLig)
                MoveWindow(hEditLig, cx + 4 + lblW + 4, cy + 3, editW, 22, TRUE);
            if (hChkLigAtivas)
                MoveWindow(hChkLigAtivas, xChk, cy + 4, chkW, 22, TRUE);
            if (hLabelLigCount)
                MoveWindow(hLabelLigCount, xCnt, cy + 4, wCnt, 22, TRUE);
            if (hListViewLig && ch > barH)
                MoveWindow(hListViewLig, cx, cy + barH, cw, ch - barH, TRUE);
        }

        /* Painel de descoberta da LAN */
        {
            int cx = rc.left, cy = rc.top;
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            RedeLocalRedimensionar(cx, cy, cw, ch);
        }
    }
}

