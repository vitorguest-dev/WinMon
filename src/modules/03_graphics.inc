/* 03 graphics */
/* ------------------------------------------------------------------------- */
/* Graficos GDI                                                              */
/* ------------------------------------------------------------------------- */

typedef enum GraphType
{
    GRAPH_CPU,
    GRAPH_RAM,
    GRAPH_TEMP,
    GRAPH_DISK,
    GRAPH_NET,
    GRAPH_GPU,
    GRAPH_PROCESS
} GraphType;

static void DrawGraphGrid(HDC hdc, RECT rc, double maxY)
{
    HPEN penGrid = CreatePen(PS_SOLID, 1, g_mainConfig.corGrelhaGrafico);
    HPEN penAxis = CreatePen(PS_SOLID, 1, g_mainConfig.corEixoGrafico);
    HPEN oldPen;
    HFONT oldFont;
    RECT labelRect;
    int i;

    oldPen = (HPEN)SelectObject(hdc, penGrid);

    if (g_mainConfig.mostrarGrelha)
        for (i = 0; i <= 4; i++)
        {
            int y = rc.top + ((rc.bottom - rc.top) * i) / 4;
            MoveToEx(hdc, rc.left, y, NULL);
            LineTo(hdc, rc.right, y);

            labelRect.left = 2;
            labelRect.right = rc.left - 6;
            labelRect.top = y - 8;
            labelRect.bottom = y + 8;

            {
                char label[32];
                double value = maxY * (1.0 - (double)i / 4.0);

                if (maxY <= 100.0)
                    snprintf(label, sizeof(label), "%.0f", value);
                else if (maxY >= 1048576.0)
                    snprintf(label, sizeof(label), "%.0f MB", value / 1048576.0);
                else
                    snprintf(label, sizeof(label), "%.0f", value);

                oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
                SetTextColor(hdc, RGB(105, 110, 116));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextA(hdc, label, -1, &labelRect,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, oldFont);
            }
        }

    if (g_mainConfig.mostrarEixos)
    {
        SelectObject(hdc, penAxis);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.left, rc.bottom);
        LineTo(hdc, rc.right, rc.bottom);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(penGrid);
    DeleteObject(penAxis);
}

static void ObterFaixaGrafico(GraphType type, int *start, int *count)
{
    GraphViewState *view;
    int visible;
    int maxEnd;
    int endOffset;

    if (type < GRAPH_CPU || type > GRAPH_PROCESS)
        type = GRAPH_CPU;

    view = &g_graphViews[type];
    visible = view->visible;
    if (visible < GRAPH_MIN_VISIBLE)
        visible = GRAPH_MIN_VISIBLE;
    if (visible > HISTORICO_PONTOS)
        visible = HISTORICO_PONTOS;
    if (visible > historico.count)
        visible = historico.count > 0 ? historico.count : 1;

    if (historico.count <= 0)
    {
        *start = 0;
        *count = 0;
        return;
    }

    maxEnd = historico.count - visible;
    endOffset = view->endOffset;
    if (endOffset < 0)
        endOffset = 0;
    if (endOffset > maxEnd)
        endOffset = maxEnd;

    view->visible = visible;
    view->endOffset = endOffset;

    *start = historico.count - visible - endOffset;
    *count = visible;
}

static void ResetGraphView(GraphType type)
{
    if (type < GRAPH_CPU || type > GRAPH_PROCESS)
        return;
    g_graphViews[type].visible = GRAPH_DEFAULT_VISIBLE;
    g_graphViews[type].endOffset = 0;
    g_graphViews[type].dragging = 0;
}

static void ZoomGraph(GraphType type, int delta, int anchorX, int graphWidth)
{
    GraphViewState *view;
    int oldVisible;
    int newVisible;
    int oldStart;
    int oldCount;
    int oldEndSample;
    double rel;
    int anchorOffset;
    int newStart;
    int newEndOffset;

    if (type < GRAPH_CPU || type > GRAPH_PROCESS || historico.count <= 0)
        return;

    view = &g_graphViews[type];
    ObterFaixaGrafico(type, &oldStart, &oldCount);
    if (oldCount <= 0 || graphWidth <= 0)
        return;

    oldVisible = oldCount;
    oldEndSample = oldStart + oldCount - 1;
    rel = (double)(anchorX - 58) / (double)(graphWidth - 78);
    if (rel < 0.0)
        rel = 0.0;
    if (rel > 1.0)
        rel = 1.0;
    anchorOffset = (int)floor(rel * (double)(oldVisible - 1) + 0.5);

    if (delta > 0)
        newVisible = (oldVisible * 3) / 4;
    else
        newVisible = (oldVisible * 4) / 3;

    if (newVisible < GRAPH_MIN_VISIBLE)
        newVisible = GRAPH_MIN_VISIBLE;
    if (newVisible > HISTORICO_PONTOS)
        newVisible = HISTORICO_PONTOS;
    if (newVisible > historico.count)
        newVisible = historico.count;
    if (newVisible == oldVisible)
        return;

    if (oldVisible > 1)
        anchorOffset = (int)(((long long)anchorOffset * (newVisible - 1)) /
                             (oldVisible - 1));
    newStart = oldEndSample - (newVisible - 1 - anchorOffset);
    if (newStart < 0)
        newStart = 0;
    if (newStart > historico.count - newVisible)
        newStart = historico.count - newVisible;

    newEndOffset = historico.count - newVisible - newStart;
    view->visible = newVisible;
    view->endOffset = newEndOffset;
}

static void DrawSeriesView(HDC hdc, RECT rc, const double *data,
                           int start, int count, double maxY,
                           COLORREF color, int thickness)
{
    HPEN pen;
    HPEN oldPen;
    int i;
    BOOL havePoint = FALSE;

    if (count <= 0 || maxY <= 0.0)
        return;

    pen = CreatePen(PS_SOLID, thickness, color);
    if (!pen)
        return;

    oldPen = (HPEN)SelectObject(hdc, pen);
    for (i = 0; i < count; i++)
    {
        int idx = HistoricoIndex(start + i);
        double value = data[idx];
        int x;
        int y;

        if (isnan(value))
        {
            havePoint = FALSE;
            continue;
        }
        if (value < 0.0)
            value = 0.0;
        if (value > maxY)
            value = maxY;

        x = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));
        y = rc.bottom - (int)((value / maxY) * (rc.bottom - rc.top));

        if (!havePoint)
            MoveToEx(hdc, x, y, NULL);
        else
            LineTo(hdc, x, y);
        havePoint = TRUE;
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawTimeLabelsView(HDC hdc, RECT rc, int start, int count)
{
    int i;

    if (count <= 0)
        return;

    for (i = 0; i < 5; i++)
    {
        int sample = (count == 1) ? 0 : (int)(((long long)(count - 1) * i) / 4);
        int idx = HistoricoIndex(start + sample);
        char text[64];
        SYSTEMTIME *st = &historico.timestamp[idx];

        if (sample == count - 1 && start + sample == historico.count - 1)
        {
            strcpy_s(text, sizeof(text), "agora");
        }
        else if (st->wYear != 0)
        {
            snprintf(text, sizeof(text), "%02u:%02u:%02u",
                     (unsigned)st->wHour, (unsigned)st->wMinute,
                     (unsigned)st->wSecond);
        }
        else
        {
            snprintf(text, sizeof(text), "ponto %d", sample + 1);
        }

        {
            int x = rc.left + ((rc.right - rc.left) * i) / 4;
            RECT tr = {x - 44, rc.bottom + 6, x + 44, rc.bottom + 24};
            SetTextColor(hdc, RGB(115, 120, 126));
            DrawTextA(hdc, text, -1, &tr, DT_CENTER | DT_SINGLELINE);
        }
    }
}

static void DrawAlertRegionsView(HDC hdc, RECT rc, const double *data,
                                 int start, int count, double maxY,
                                 double threshold)
{
    HBRUSH brush;
    int i;
    int yLimite;

    if (count <= 0 || maxY <= 0.0 || threshold <= 0.0 || threshold >= maxY)
        return;

    brush = CreateSolidBrush(RGB(255, 232, 232));
    if (!brush)
        return;

    yLimite = rc.bottom - (int)((threshold / maxY) * (rc.bottom - rc.top));
    if (yLimite < rc.top)
        yLimite = rc.top;
    if (yLimite > rc.bottom)
        yLimite = rc.bottom;

    for (i = 0; i < count; i++)
    {
        int idx = HistoricoIndex(start + i);
        double value = data[idx];
        int x1, x2;
        RECT zone;

        if (isnan(value) || value <= threshold)
            continue;

        x1 = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));
        x2 = (i == count - 1) ? rc.right : rc.left + (int)(((double)(i + 1) / (double)(count - 1)) * (rc.right - rc.left));
        if (x2 <= x1)
            x2 = x1 + 1;

        zone.left = x1;
        zone.right = x2;
        zone.top = rc.top;
        zone.bottom = yLimite;
        FillRect(hdc, &zone, brush);
    }

    DeleteObject(brush);
}

static void DrawGraphViewportInfo(HDC hdc, RECT client, RECT graph, GraphType type,
                                  int start, int count)
{
    char text[160];
    int secondsSpan = 0;

    if (count > 1)
    {
        int first = HistoricoIndex(start);
        int last = HistoricoIndex(start + count - 1);
        if (historico.tickMs[last] >= historico.tickMs[first])
            secondsSpan = (int)((historico.tickMs[last] - historico.tickMs[first]) / 1000ULL);
        else
            secondsSpan = (int)(((long long)(count - 1) * intervaloAtualizacaoMs) / 1000);
    }

    if (secondsSpan >= 60)
        snprintf(text, sizeof(text), "Janela: %dm %02ds  |  arrastar = navegar  |  roda = zoom  |  duplo-clique = agora",
                 secondsSpan / 60, secondsSpan % 60);
    else
        snprintf(text, sizeof(text), "Janela: %ds  |  arrastar = navegar  |  roda = zoom  |  duplo-clique = agora",
                 secondsSpan);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(120, 125, 130));
    {
        RECT tr = {12, graph.bottom + 24, client.right - 12, graph.bottom + 40};
        DrawTextA(hdc, text, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
    (void)type;
}

static void DrawGraphLegend(HDC hdc, RECT *rc, const char *title,
                            const char **names, COLORREF *colors, int n,
                            const char *currentText)
{
    HFONT oldFont;
    RECT titleRect;
    int x;
    int i;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_mainConfig.corTextoGrafico);

    oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));

    titleRect = *rc;
    titleRect.left += 6;
    titleRect.top += 4;
    titleRect.bottom = titleRect.top + 24;
    DrawTextA(hdc, title, -1, &titleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    x = rc->left + 10;

    for (i = 0; i < n; i++)
    {
        HBRUSH brush = CreateSolidBrush(colors[i]);
        RECT box = {x, rc->top + 34, x + 10, rc->top + 44};
        RECT text = {x + 15, rc->top + 29, x + 130, rc->top + 49};

        FillRect(hdc, &box, brush);
        DeleteObject(brush);

        DrawTextA(hdc, names[i], -1, &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        x += 125;
    }

    if (currentText)
    {
        RECT current = *rc;
        current.left = rc->right - 260;
        current.top += 5;
        current.bottom = current.top + 22;
        DrawTextA(hdc, currentText, -1, &current,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, oldFont);
}

static HFONT g_hValueFont = NULL;
static char g_cardSubTxt[64]; /* sub-valor opcional (canto superior direito do cartao) */

static void DrawDashboardCardLively(HDC hdc, RECT rc, const char *label, const char *value,
                                    COLORREF accentBase, const double *historyData, double maxLim)
{
    double v=0.0; COLORREF markerColor=accentBase; HBRUSH panel,marker; RECT markerRect,labelRect,valueRect; HFONT oldFont;
    if(historico.count>0&&historyData)v=historyData[HistoricoIndex(historico.count-1)];
    if(maxLim>0.0){if(v>=maxLim)markerColor=RGB(220,50,50);else if(v>=maxLim*.85)markerColor=RGB(220,150,20);}
    panel=CreateSolidBrush(RGB(255,255,255));marker=CreateSolidBrush(markerColor);markerRect=(RECT){rc.left,rc.top,rc.left+5,rc.bottom};labelRect=(RECT){rc.left+16,rc.top+7,rc.right-10,rc.top+24};valueRect=(RECT){rc.left+16,rc.top+23,rc.right-10,rc.bottom-6};FillRect(hdc,&rc,panel);FillRect(hdc,&markerRect,marker);{HPEN bp=CreatePen(PS_SOLID,1,RGB(224,228,233));HPEN op=(HPEN)SelectObject(hdc,bp);HBRUSH ob=(HBRUSH)SelectObject(hdc,GetStockObject(NULL_BRUSH));Rectangle(hdc,rc.left,rc.top,rc.right,rc.bottom);SelectObject(hdc,ob);SelectObject(hdc,op);DeleteObject(bp);}DeleteObject(panel);DeleteObject(marker);
    SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,RGB(102,108,116));oldFont=(HFONT)SelectObject(hdc,GetStockObject(DEFAULT_GUI_FONT));DrawTextA(hdc,label,-1,&labelRect,DT_LEFT|DT_SINGLELINE|DT_VCENTER);if(g_cardSubTxt[0]){RECT sr=labelRect;SetTextColor(hdc,RGB(120,126,134));DrawTextA(hdc,g_cardSubTxt,-1,&sr,DT_RIGHT|DT_SINGLELINE|DT_VCENTER);SetTextColor(hdc,RGB(102,108,116));}SelectObject(hdc,g_hValueFont);SetTextColor(hdc,RGB(32,38,45));DrawTextA(hdc,value,-1,&valueRect,DT_LEFT|DT_SINGLELINE|DT_VCENTER);SelectObject(hdc,oldFont);
}

static LRESULT CALLBACK DashboardProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
    (void)wParam;
    switch(msg){
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONUP:{POINT pt={GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};RECT rc;GetClientRect(hwnd,&rc);if(pt.y>=46&&pt.y<170){int gap=10,cw=(rc.right-32-gap*2)/3,col=(pt.x-16)/(cw+gap),row=(pt.y-46)/62;static const int targets[6]={TAB_CPU,TAB_MEMORIA,TAB_DISCO,TAB_REDE,TAB_GPU,TAB_TEMPERATURA};if(cw>100&&col>=0&&col<3&&row>=0&&row<2)SendMessageA(GetParent(hwnd),WM_COMMAND,MAKEWPARAM(targets[row*3+col],BN_CLICKED),0);}return 0;}
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT client;HDC mem=CreateCompatibleDC(dc);HBITMAP bmp,old;HBRUSH bg;int gap=10,cw,i;char value[64];GetClientRect(hwnd,&client);bmp=CreateCompatibleBitmap(dc,client.right,client.bottom);if(!mem||!bmp){if(mem)DeleteDC(mem);if(bmp)DeleteObject(bmp);EndPaint(hwnd,&ps);return 0;}old=(HBITMAP)SelectObject(mem,bmp);bg=CreateSolidBrush(RGB(241,244,247));FillRect(mem,&client,bg);DeleteObject(bg);SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(28,36,45));{RECT t={16,8,client.right-16,32};DrawTextA(mem,"Visao geral do sistema",-1,&t,DT_LEFT|DT_SINGLELINE|DT_VCENTER);}cw=(client.right-32-gap*2)/3;if(cw<100)cw=100;for(i=0;i<6;i++){g_cardSubTxt[0]=0;RECT card={16+(i%3)*(cw+gap),46+(i/3)*62,16+(i%3)*(cw+gap)+cw,102+(i/3)*62};switch(i){case 0:snprintf(value,sizeof(value),"%.1f%%",ultimoCpuPercent);if(ultimoCpuFreqMHz>0.0)snprintf(g_cardSubTxt,sizeof(g_cardSubTxt),"%.2f GHz",ultimoCpuFreqMHz/1000.0);DrawDashboardCardLively(mem,card,"CPU",value,g_mainConfig.corGraficoCpu,historico.cpu,limiteCpuPercent);break;case 1:snprintf(value,sizeof(value),"%.0f%%",ultimoRamPercent);DrawDashboardCardLively(mem,card,"Memoria",value,g_mainConfig.corGraficoRam,historico.ram,limiteRamPercent);break;case 2:snprintf(value,sizeof(value),"%.1f MB/s",(ultimoDiskRead+ultimoDiskWrite)/1048576.0);DrawDashboardCardLively(mem,card,"Disco I/O",value,g_mainConfig.corGraficoDiscoRead,historico.diskRead,500.0*1048576.0);break;case 3:snprintf(value,sizeof(value),"%.1f MB/s",(ultimoNetDown+ultimoNetUp)/1048576.0);DrawDashboardCardLively(mem,card,"Rede",value,g_mainConfig.corGraficoNetDown,historico.netDown,100.0*1048576.0);break;case 4:snprintf(value,sizeof(value),"%.1f%%",ultimoGpuPercent);if(ultimaVramUsadaMB>=0.0){char vt[40];TextoVramCurto(vt,sizeof(vt));snprintf(g_cardSubTxt,sizeof(g_cardSubTxt),"%s%s",g_vramIntegrada?"Mem ":"VRAM ",vt);}DrawDashboardCardLively(mem,card,"GPU",value,g_mainConfig.corGraficoGpu,historico.gpu,100.0);break;default:snprintf(value,sizeof(value),"%.1f C",ObterTemperaturaMaxima());DrawDashboardCardLively(mem,card,"Temperatura",value,RGB(205,100,55),historico.temp,100.0);break;}}{RECT st={16,174,client.right-16,220};char txt[256];snprintf(txt,sizeof(txt),"Estado: %s | Processo lider: %.1f%% CPU | Historico: %d pontos | Clique num cartao para abrir o grafico",alertaGlobalAtivo?"ALERTA":"NORMAL",processoCpuAtual,historico.count);SetTextColor(mem,alertaGlobalAtivo?RGB(190,55,45):RGB(82,91,101));DrawTextA(mem,txt,-1,&st,DT_LEFT|DT_SINGLELINE|DT_VCENTER);}BitBlt(dc,0,0,client.right,client.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
    case WM_SIZE:InvalidateRect(hwnd,NULL,FALSE);return 0;}
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

static void DrawAlertRegions(HDC hdc, RECT rc, const double *data,
                             int count, double maxY, double threshold)
{
    HBRUSH brush;
    int i;
    int yLimite;

    if (count <= 0 || maxY <= 0.0 || threshold <= 0.0 || threshold >= maxY)
        return;

    brush = CreateSolidBrush(RGB(255, 232, 232));
    if (!brush)
        return;

    yLimite = rc.bottom - (int)((threshold / maxY) * (rc.bottom - rc.top));
    if (yLimite < rc.top)
        yLimite = rc.top;
    if (yLimite > rc.bottom)
        yLimite = rc.bottom;

    for (i = 0; i < count; i++)
    {
        int idx = HistoricoIndex(i);
        double value = data[idx];
        int x1, x2;
        RECT zone;

        if (isnan(value) || value <= threshold)
            continue;

        x1 = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));
        x2 = (i == count - 1)
                 ? rc.right
                 : rc.left + (int)(((double)(i + 1) / (double)(count - 1)) *
                                   (rc.right - rc.left));

        if (x2 <= x1)
            x2 = x1 + 1;

        zone.left = x1;
        zone.right = x2;
        zone.top = rc.top;
        zone.bottom = yLimite;
        FillRect(hdc, &zone, brush);
    }

    DeleteObject(brush);
}

static void DrawLatencySeries(HDC hdc, RECT rc, int start, int count,
                              double maxY, COLORREF cor, int thickness)
{
    HPEN pen = CreatePen(PS_SOLID, thickness, cor);
    HPEN penLoss = CreatePen(PS_SOLID, 1, RGB(210, 60, 50));
    HPEN oldPen;
    BOOL havePoint = FALSE;
    int i;

    if (!pen || !penLoss || count <= 0 || maxY <= 0.0)
    {
        if (pen)
            DeleteObject(pen);
        if (penLoss)
            DeleteObject(penLoss);
        return;
    }

    oldPen = (HPEN)SelectObject(hdc, pen);
    for (i = 0; i < count; i++)
    {
        double v = historico.ping[HistoricoIndex(start + i)];
        int x = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));

        if (isnan(v))
        {
            havePoint = FALSE;
            continue;
        }
        if (v < 0.0) /* pacote perdido: marca vermelha vertical */
        {
            SelectObject(hdc, penLoss);
            MoveToEx(hdc, x, rc.bottom, NULL);
            LineTo(hdc, x, rc.top);
            SelectObject(hdc, pen);
            havePoint = FALSE;
            continue;
        }
        if (v > maxY)
            v = maxY;
        {
            int y = rc.bottom - (int)((v / maxY) * (rc.bottom - rc.top));
            if (!havePoint)
                MoveToEx(hdc, x, y, NULL);
            else
                LineTo(hdc, x, y);
            havePoint = TRUE;
        }
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(penLoss);
}

/* Painel por baixo do grafico de rede: latencia (esquerda) + Wi-Fi (direita). */
static void DesenharPainelLatenciaWifi(HDC hdc, RECT client, RECT upper,
                                       int viewStart, int viewCount)
{
    PingEstado p;
    WifiEstado w;
    RECT pr;
    HFONT oldFont;
    int top, usable, i;
    double maxY = 40.0;
    char txt[160];
    GraphViewState *vw = &g_graphViews[GRAPH_NET];

    SetRectEmpty(&g_pingTitleRect);
    if (client.bottom < PING_PAINEL_ALTURA_MIN || viewCount <= 0)
        return;

    PingWifiObter(&p, &w);

    top = upper.bottom + 52; /* deixa livre: rotulos de tempo + linha 'Janela: ...' do grafico de cima */
    usable = client.right - 20 - 58;
    pr.left = 58;
    pr.top = top + 46;
    pr.right = 58 + (usable * 58) / 100;
    pr.bottom = client.bottom - 40;
    if (pr.bottom - pr.top < 50 || pr.right - pr.left < 120)
        return;

    oldFont = (HFONT)SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);

    /* escala do eixo Y (valores "redondos" divisiveis por 4 para a grelha) */
    for (i = 0; i < viewCount; i++)
    {
        double v = historico.ping[HistoricoIndex(viewStart + i)];
        if (!isnan(v) && v > maxY * 0.9)
            maxY = v / 0.9;
    }
    {
        static const double passos[] = {40, 100, 200, 400, 800, 1600};
        int k;
        double sel = passos[5];
        for (k = 0; k < 6; k++)
            if (maxY <= passos[k])
            {
                sel = passos[k];
                break;
            }
        maxY = sel;
    }

    /* titulo (clicavel: muda o alvo) */
    snprintf(txt, sizeof(txt), "Latencia (ping) -> %s   [clique para mudar de alvo]",
             p.ativo ? p.alvoNome : g_pingAlvoNome[g_pingAlvoIdx % PING_ALVOS]);
    SetTextColor(hdc, g_mainConfig.corTextoGrafico);
    TextOutA(hdc, pr.left, top, txt, (int)strlen(txt));
    SetRect(&g_pingTitleRect, pr.left, top - 2, pr.right, top + 18);

    if (p.ativo && !p.semGateway && !isnan(p.ultimoMs))
    {
        char ult[32];
        RECT st = {pr.left, top + 19, pr.right, top + 36};

        if (p.ultimoMs < 0.0)
            snprintf(ult, sizeof(ult), "perdido");
        else if (p.ultimoMs < 1.0)
            snprintf(ult, sizeof(ult), "<1 ms");
        else
            snprintf(ult, sizeof(ult), "%.0f ms", p.ultimoMs);
        snprintf(txt, sizeof(txt), "Atual %s | Min %.0f | Med %.1f | Max %.0f ms | Jitter %.1f | Perda %.1f%% (%lu/%lu)",
                 ult, p.minMs, p.mediaMs, p.maxMs, p.jitterMs, p.perdaJanela,
                 p.enviados - p.recebidos, p.enviados);
        SetTextColor(hdc, RGB(105, 110, 116));
        DrawTextA(hdc, txt, -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    else if (p.ativo && p.semGateway)
    {
        SetTextColor(hdc, RGB(190, 55, 45));
        TextOutA(hdc, pr.left, top + 20, "Gateway nao encontrado - clique para escolher outro alvo", 56);
    }

    DrawGraphGrid(hdc, pr, maxY);
    DrawLatencySeries(hdc, pr, viewStart, viewCount, maxY, RGB(46, 160, 110),
                      g_mainConfig.espessuraLinhas);
    DrawTimeLabelsView(hdc, pr, viewStart, viewCount);

    /* cursor sincronizado com o grafico de cima */
    if (vw->isHovering && vw->hoverX >= upper.left && vw->hoverX <= upper.right)
    {
        double ratio = (double)(vw->hoverX - upper.left) / (double)(upper.right - upper.left);
        int x = pr.left + (int)(ratio * (pr.right - pr.left));
        int off = (int)(ratio * (viewCount - 1));
        double v = historico.ping[HistoricoIndex(viewStart + off)];
        HPEN cp = CreatePen(PS_DOT, 1, RGB(100, 100, 100));
        HPEN op = (HPEN)SelectObject(hdc, cp);

        MoveToEx(hdc, x, pr.top, NULL);
        LineTo(hdc, x, pr.bottom);
        SelectObject(hdc, op);
        DeleteObject(cp);

        if (isnan(v))
            snprintf(txt, sizeof(txt), "sem dados");
        else if (v < 0.0)
            snprintf(txt, sizeof(txt), "perdido");
        else
            snprintf(txt, sizeof(txt), "%.0f ms", v);
        SetTextColor(hdc, g_mainConfig.corTextoGrafico);
        TextOutA(hdc, x + 4 + 90 > pr.right ? x - 70 : x + 4, pr.top + 2, txt, (int)strlen(txt));
    }

    /* ---- Wi-Fi ---- */
    {
        int wl = pr.right + 28, wr = client.right - 20, y = top;

        if (wr - wl >= 150)
        {
            SetTextColor(hdc, g_mainConfig.corTextoGrafico);
            TextOutA(hdc, wl, y, "Wi-Fi", 5);
            y = top + 22;
            SetTextColor(hdc, RGB(105, 110, 116));

            if (!w.adaptador)
            {
                snprintf(txt, sizeof(txt), "Sem adaptador Wi-Fi (ou servico WLAN parado)");
                TextOutA(hdc, wl, y, txt, (int)strlen(txt));
            }
            else if (!w.ligado)
            {
                snprintf(txt, sizeof(txt), "Desligado");
                TextOutA(hdc, wl, y, txt, (int)strlen(txt));
                SetTextColor(hdc, RGB(130, 135, 140));
                TextOutA(hdc, wl, y + 18, w.adaptadorNome, (int)strlen(w.adaptadorNome));
            }
            else
            {
                RECT bar, bf, spark;
                COLORREF cb = w.qualidade >= 60 ? RGB(46, 160, 110)
                              : w.qualidade >= 35 ? RGB(215, 160, 40)
                                                  : RGB(210, 60, 50);

                SetTextColor(hdc, g_mainConfig.corTextoGrafico);
                snprintf(txt, sizeof(txt), "Rede: %s", w.ssid);
                TextOutA(hdc, wl, y, txt, (int)strlen(txt));
                SetTextColor(hdc, RGB(105, 110, 116));
                snprintf(txt, sizeof(txt), "Sinal: %d%% (~%.0f dBm)", w.qualidade, w.dbm);
                TextOutA(hdc, wl, y + 18, txt, (int)strlen(txt));

                bar.left = wl;
                bar.right = wr;
                bar.top = y + 38;
                bar.bottom = y + 48;
                bf = bar;
                bf.right = bar.left + ((bar.right - bar.left) * w.qualidade) / 100;
                {
                    HBRUSH bb = CreateSolidBrush(g_mainConfig.corGrelhaGrafico);
                    HBRUSH bs = CreateSolidBrush(cb);
                    FillRect(hdc, &bar, bb);
                    FillRect(hdc, &bf, bs);
                    DeleteObject(bb);
                    DeleteObject(bs);
                }

                snprintf(txt, sizeof(txt), "Velocidade: RX %lu / TX %lu Mbps",
                         (unsigned long)(w.rxKbps / 1000), (unsigned long)(w.txKbps / 1000));
                TextOutA(hdc, wl, y + 54, txt, (int)strlen(txt));
                snprintf(txt, sizeof(txt), "%s | %s", WifiPhyNome(w.phy), WifiAuthNome(w.auth));
                TextOutA(hdc, wl, y + 72, txt, (int)strlen(txt));

                spark.left = wl;
                spark.right = wr;
                spark.top = y + 112;
                spark.bottom = pr.bottom;
                if (spark.bottom - spark.top >= 24)
                {
                    HBRUSH fb = CreateSolidBrush(g_mainConfig.corEixoGrafico);

                    TextOutA(hdc, wl, y + 92, "Historico do sinal (%)", 22);
                    FrameRect(hdc, &spark, fb);
                    DeleteObject(fb);
                    DrawSeriesView(hdc, spark, historico.wifiSinal, viewStart, viewCount,
                                   100.0, cb, 1);
                }
            }
        }
    }

    SelectObject(hdc, oldFont);
}

static void PaintGraph(HWND hwnd, HDC hdc, GraphType type)
{
    RECT client;
    RECT graph;
    HBRUSH bg = CreateSolidBrush(g_mainConfig.corFundoGrafico);
    char currentText[128];
    const char *names[4];
    COLORREF colors[4];
    int n = 0;
    int i;
    int viewStart = 0;
    int viewCount = 0;

    ObterFaixaGrafico(type, &viewStart, &viewCount);

    GetClientRect(hwnd, &client);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    graph.left = 58;
    graph.top = 58;
    graph.right = client.right - 20;
    graph.bottom = client.bottom - 40;

    if (graph.right <= graph.left + 20 || graph.bottom <= graph.top + 20)
        return;

    currentText[0] = '\0';

    switch (type)
    {
    case GRAPH_CPU:
    {
        names[0] = "CPU total";
        colors[0] = g_mainConfig.corGraficoCpu;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Atual: %.1f%%", ultimoCpuPercent);

        DrawGraphLegend(hdc, &client, "CPU", names, colors, n, currentText);
        DrawAlertRegionsView(hdc, graph, historico.cpu, viewStart, viewCount,
                             100.0, limiteCpuPercent);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeriesView(hdc, graph, historico.cpu, viewStart, viewCount,
                       100.0, colors[0], g_mainConfig.espessuraLinhas);

        if (numNucleosMonitorizados > 0 && client.bottom > 430)
        {
            int smallTop = graph.bottom + 42;
            int smallBottom = client.bottom - 12;
            int smallHeight = smallBottom - smallTop;

            if (smallHeight > 45)
            {
                int cols = 4;
                int rows = (numNucleosMonitorizados + cols - 1) / cols;
                int cw = (client.right - 72) / cols;
                int rh = smallHeight / rows;

                for (i = 0; i < numNucleosMonitorizados; i++)
                {
                    int col = i % cols;
                    int row = i / cols;
                    RECT sr;

                    sr.left = 60 + col * cw;
                    sr.top = smallTop + row * rh;
                    sr.right = sr.left + cw - 8;
                    sr.bottom = sr.top + rh - 8;

                    DrawGraphGrid(hdc, sr, 100.0);
                    DrawSeriesView(hdc, sr, historico.core[i],
                                   viewStart, viewCount, 100.0,
                                   g_mainConfig.corGraficoCore, 1);

                    {
                        char label[32];
                        snprintf(label, sizeof(label), "C%d", i);
                        SetTextColor(hdc, RGB(70, 75, 80));
                        SetBkMode(hdc, TRANSPARENT);
                        TextOutA(hdc, sr.left + 4, sr.top + 3,
                                 label, (int)strlen(label));
                    }
                }
            }
        }

        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_RAM:
    {
        names[0] = "RAM usada";
        colors[0] = g_mainConfig.corGraficoRam;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Atual: %.0f%%", ultimoRamPercent);

        DrawGraphLegend(hdc, &client, "Memoria RAM — historico temporal",
                        names, colors, n, currentText);
        DrawAlertRegionsView(hdc, graph, historico.ram, viewStart, viewCount,
                             100.0, limiteRamPercent);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeriesView(hdc, graph, historico.ram, viewStart, viewCount,
                       100.0, colors[0], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_TEMP:
    {
        names[0] = "Temperatura";
        colors[0] = g_mainConfig.corGraficoCpu;
        n = 1;
        snprintf(currentText, sizeof(currentText),
                 "Atual: %.1f C", numZonasTemp > 0 ? historico.temp[historico.pos] : 0.0);
        DrawGraphLegend(hdc, &client, "Temperatura — historico temporal",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeriesView(hdc, graph, historico.temp, viewStart, viewCount,
                       100.0, colors[0], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_DISK:
    {
        double maxVal = 1024.0 * 1024.0;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (historico.diskRead[i] > maxVal)
                maxVal = historico.diskRead[i];
            if (historico.diskWrite[i] > maxVal)
                maxVal = historico.diskWrite[i];
        }

        names[0] = "Leitura";
        names[1] = "Escrita";
        colors[0] = g_mainConfig.corGraficoDiscoRead;
        colors[1] = g_mainConfig.corGraficoDiscoWrite;
        n = 2;

        snprintf(currentText, sizeof(currentText),
                 "R %.2f MB/s   W %.2f MB/s",
                 ultimoDiskRead / 1048576.0,
                 ultimoDiskWrite / 1048576.0);

        DrawGraphLegend(hdc, &client, "I/O",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxVal);
        DrawSeriesView(hdc, graph, historico.diskRead, viewStart, viewCount,
                       maxVal, colors[0], g_mainConfig.espessuraLinhas);
        DrawSeriesView(hdc, graph, historico.diskWrite, viewStart, viewCount,
                       maxVal, colors[1], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_NET:
    {
        double maxVal = 1024.0 * 1024.0;

        /* espaco para o painel de latencia + Wi-Fi por baixo */
        if (client.bottom >= PING_PAINEL_ALTURA_MIN)
            graph.bottom = graph.top + ((graph.bottom - graph.top) * 42) / 100;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (historico.netDown[i] > maxVal)
                maxVal = historico.netDown[i];
            if (historico.netUp[i] > maxVal)
                maxVal = historico.netUp[i];
        }

        names[0] = "Download";
        names[1] = "Upload";
        colors[0] = g_mainConfig.corGraficoNetDown;
        colors[1] = g_mainConfig.corGraficoNetUp;
        n = 2;

        snprintf(currentText, sizeof(currentText),
                 "D %.2f MB/s   U %.2f MB/s",
                 ultimoNetDown / 1048576.0,
                 ultimoNetUp / 1048576.0);

        DrawGraphLegend(hdc, &client, "Trafego de rede",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxVal);
        DrawSeriesView(hdc, graph, historico.netDown, viewStart, viewCount,
                       maxVal, colors[0], g_mainConfig.espessuraLinhas);
        DrawSeriesView(hdc, graph, historico.netUp, viewStart, viewCount,
                       maxVal, colors[1], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        DesenharPainelLatenciaWifi(hdc, client, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_GPU:
    {
        /* 4 series: 3D, Copy, Encode, Decode — igual ao Gestor de Tarefas */
        names[0] = "3D";
        names[1] = "Copy";
        names[2] = "Encode";
        names[3] = "Decode";
        colors[0] = g_mainConfig.corGraficoGpu; /* roxo  */
        colors[1] = RGB(80, 180, 220);          /* azul  */
        colors[2] = RGB(80, 210, 130);          /* verde */
        colors[3] = RGB(220, 160, 60);          /* laranja */
        n = 4;
        snprintf(currentText, sizeof(currentText),
                 "3D:%.1f%%  Cp:%.1f%%  En:%.1f%%  De:%.1f%%",
                 ultimoGpuPercent, ultimoGpuCopy,
                 ultimoGpuEncode, ultimoGpuDecode);
        DrawGraphLegend(hdc, &client, "GPU — utilizacao por engine", names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, 100.0);
        DrawSeriesView(hdc, graph, historico.gpu, viewStart, viewCount,
                       100.0, colors[0], g_mainConfig.espessuraLinhas);
        DrawSeriesView(hdc, graph, historico.gpuCopy, viewStart, viewCount,
                       100.0, colors[1], g_mainConfig.espessuraLinhas);
        DrawSeriesView(hdc, graph, historico.gpuEncode, viewStart, viewCount,
                       100.0, colors[2], g_mainConfig.espessuraLinhas);
        DrawSeriesView(hdc, graph, historico.gpuDecode, viewStart, viewCount,
                       100.0, colors[3], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);
        break;
    }

    case GRAPH_PROCESS:
    {
        double maxCpu = 100.0;

        for (i = 0; i < HISTORICO_PONTOS; i++)
        {
            if (processoCpuTop[i] > maxCpu)
                maxCpu = processoCpuTop[i];
        }

        names[0] = "Processo #1 CPU";
        colors[0] = g_mainConfig.corGraficoProcesso;
        n = 1;

        snprintf(currentText, sizeof(currentText),
                 "Pico atual: %.1f%% CPU",
                 processoCpuTop[historico.pos]);

        DrawGraphLegend(hdc, &client, "Processo com maior uso de CPU",
                        names, colors, n, currentText);
        DrawGraphGrid(hdc, graph, maxCpu);
        DrawSeriesView(hdc, graph, processoCpuTop, viewStart, viewCount,
                       maxCpu, colors[0], g_mainConfig.espessuraLinhas);
        DrawTimeLabelsView(hdc, graph, viewStart, viewCount);

        if (client.bottom > 360)
        {
            RECT info = {
                70, graph.bottom + 34,
                client.right - 30, client.bottom - 8};
            char line[256];

            snprintf(line, sizeof(line),
                     "RAM do processo lider neste instante: %.0f MB",
                     processoRamTop[historico.pos]);

            SetTextColor(hdc, RGB(75, 80, 86));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, line, -1, &info,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        break;
    }
    }

    if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
    {
        DrawGraphViewportInfo(hdc, client, graph, type, viewStart, viewCount);

        GraphViewState *view = &g_graphViews[type];
        if (view->isHovering && view->hoverX >= graph.left && view->hoverX <= graph.right && viewCount > 0)
        {
            HPEN crossPen = CreatePen(PS_DOT, 1, RGB(100, 100, 100));
            HPEN oldPen = (HPEN)SelectObject(hdc, crossPen);

            MoveToEx(hdc, view->hoverX, graph.top, NULL);
            LineTo(hdc, view->hoverX, graph.bottom);
            SelectObject(hdc, oldPen);
            DeleteObject(crossPen);

            double ratio = (double)(view->hoverX - graph.left) / (graph.right - graph.left);
            if (ratio < 0)
                ratio = 0;
            if (ratio > 1)
                ratio = 1;

            int sampleOffset = (int)(ratio * (viewCount - 1));
            int idx = HistoricoIndex(viewStart + sampleOffset);

            char tip[64];
            SYSTEMTIME st = historico.timestamp[idx];
            double val = 0.0;

            switch (type)
            {
            case GRAPH_CPU:
                val = historico.cpu[idx];
                break;
            case GRAPH_RAM:
                val = historico.ram[idx];
                break;
            case GRAPH_TEMP:
                val = historico.temp[idx];
                break;
            case GRAPH_DISK:
                val = (historico.diskRead[idx] + historico.diskWrite[idx]) / 1048576.0;
                break;
            case GRAPH_NET:
                val = (historico.netDown[idx] + historico.netUp[idx]) / 1048576.0;
                break;
            case GRAPH_GPU:
                val = historico.gpu[idx];
                break;
            case GRAPH_PROCESS:
                val = processoCpuTop[idx];
                break;
            }

            snprintf(tip, sizeof(tip), "%02d:%02d:%02d | %.1f", st.wHour, st.wMinute, st.wSecond, val);

            RECT tipRect = {view->hoverX + 10, graph.top + 10, view->hoverX + 140, graph.top + 30};
            /* Prevenir que a tooltip saia pelo limite direito do ecrã */
            if (tipRect.right > client.right)
            {
                tipRect.left = view->hoverX - 140;
                tipRect.right = view->hoverX - 10;
            }
            FillRect(hdc, &tipRect, (HBRUSH)GetStockObject(WHITE_BRUSH));
            FrameRect(hdc, &tipRect, (HBRUSH)GetStockObject(BLACK_BRUSH));
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextA(hdc, tip, -1, &tipRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

LRESULT CALLBACK GraphProc(HWND hwnd, UINT msg,
                           WPARAM wParam, LPARAM lParam)
{
    GraphType type = (GraphType)(GetWindowLongPtr(hwnd, GWLP_USERDATA) - 1);

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintGraph(hwnd, hdc, type);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        if (type == GRAPH_NET && !IsRectEmpty(&g_pingTitleRect))
        {
            POINT cp = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (PtInRect(&g_pingTitleRect, cp))
            {
                PingCiclarAlvo();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
        {
            GraphViewState *view = &g_graphViews[type];
            view->dragging = 1;
            view->dragX = GET_X_LPARAM(lParam);
            view->dragStartEnd = view->endOffset;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
        {
            GraphViewState *view = &g_graphViews[type];
            if (view->dragging && historico.count > 0)
            {
                RECT client;
                int x = GET_X_LPARAM(lParam);
                int visible = view->visible;
                int width;
                int deltaSamples;
                int maxEnd;
                GetClientRect(hwnd, &client);
                width = client.right - 78;
                if (visible < 2)
                    visible = 2;
                if (width < 1)
                    width = 1;
                deltaSamples = (int)(((long long)(view->dragX - x) * (visible - 1)) / width);
                view->endOffset = view->dragStartEnd + deltaSamples;
                maxEnd = historico.count - visible;
                if (maxEnd < 0)
                    maxEnd = 0;
                if (view->endOffset < 0)
                    view->endOffset = 0;
                if (view->endOffset > maxEnd)
                    view->endOffset = maxEnd;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else if (historico.count > 0)
            {
                view->hoverX = GET_X_LPARAM(lParam);
                view->isHovering = 1;

                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                TrackMouseEvent(&tme);

                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_MOUSELEAVE:
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
        {
            g_graphViews[type].isHovering = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

        /* Mantenha o resto que lá estava para WM_LBUTTONUP, WM_LBUTTONDBLCLK, etc. */

    case WM_LBUTTONUP:
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS &&
            g_graphViews[type].dragging)
        {
            g_graphViews[type].dragging = 0;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
        {
            ResetGraphView(type);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (type >= GRAPH_CPU && type <= GRAPH_PROCESS)
        {
            RECT client;
            POINT pt;
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            GetClientRect(hwnd, &client);
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);
            ZoomGraph(type, delta > 0 ? 1 : -1,
                      pt.x, client.right);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_RBUTTONUP:
    {
        HMENU menu = CreatePopupMenu();
        POINT pt;
        if (!menu)
            return 0;
        AppendMenuA(menu, MF_STRING, 1, "Guardar grafico como PNG");
        AppendMenuA(menu, MF_STRING, 2, "Voltar a agora");
        GetCursorPos(&pt);
        {
            UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      pt.x, pt.y, 0, hwnd, NULL);
            if (cmd == 1)
                ExportarGraficoPNG(hwnd);
            else if (cmd == 2)
            {
                ResetGraphView(type);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        DestroyMenu(menu);
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static HWND CriarGrafico(HWND parent, GraphType type)
{
    HWND hwnd = CreateWindowExA(
        0,
        "HardwareMonitorGraph",
        "",
        WS_CHILD | WS_VISIBLE,
        0, 0, 100, 100,
        parent,
        NULL,
        GetModuleHandle(NULL),
        NULL);

    if (hwnd)
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)type + 1);

    return hwnd;
}

static ProcessoSerieHistorico *EncontrarSerieProcesso(DWORD pid, int criar)
{
    int i;
    ProcessoSerieHistorico *slot = NULL;

    for (i = 0; i < g_processSeriesCount; i++)
    {
        if (g_processSeries[i].emUso && g_processSeries[i].pid == pid)
            return &g_processSeries[i];
    }

    if (!criar)
        return NULL;

    if (g_processSeriesCount < MAX_PROCESS_SERIES)
    {
        slot = &g_processSeries[g_processSeriesCount++];
        ZeroMemory(slot, sizeof(*slot));
        slot->pid = pid;
        slot->emUso = 1;
    }
    else
    {
        int oldest = 0;
        ULONGLONG tickOldest = ~0ULL;
        for (i = 0; i < MAX_PROCESS_SERIES; i++)
        {
            if (g_processSeries[i].ultimoVisto < tickOldest)
            {
                tickOldest = g_processSeries[i].ultimoVisto;
                oldest = i;
            }
        }
        slot = &g_processSeries[oldest];
        ZeroMemory(slot, sizeof(*slot));
        slot->pid = pid;
        slot->emUso = 1;
    }

    slot->pos = 0;
    slot->count = 0;
    for (i = 0; i < PROCESS_HISTORICO_PONTOS; i++)
    {
        slot->cpu[i] = NAN;
        slot->ram[i] = NAN;
    }
    return slot;
}

static void AtualizarHistoricoProcessos(void)
{
    int i;
    ULONGLONG agora = GetTickCount64();

    g_processHistoryPos = (g_processHistoryPos + 1) % PROCESS_HISTORICO_PONTOS;
    if (g_processHistoryCount < PROCESS_HISTORICO_PONTOS)
        g_processHistoryCount++;

    for (i = 0; i < g_processSeriesCount; i++)
    {
        if (g_processSeries[i].emUso)
        {
            g_processSeries[i].cpu[g_processHistoryPos] = NAN;
            g_processSeries[i].ram[g_processHistoryPos] = NAN;
        }
    }

    for (i = 0; i < totalListaProcessos; i++)
    {
        ProcessoSerieHistorico *serie = EncontrarSerieProcesso(listaProcessos[i].pid, 1);
        if (serie)
        {
            serie->cpu[g_processHistoryPos] = listaProcessos[i].cpuPercent;
            serie->ram[g_processHistoryPos] = (double)listaProcessos[i].memUsageMB;
            strncpy_s(serie->exeFile, sizeof(serie->exeFile),
                      listaProcessos[i].exeFile, _TRUNCATE);
            serie->ultimoVisto = agora;
            serie->pos = g_processHistoryPos;
            serie->count = g_processHistoryCount;
        }
    }
}

static void DrawSeriesProcessView(HDC hdc, RECT rc, const double *data,
                                  int count, int pos, double maxY,
                                  COLORREF color)
{
    HPEN pen;
    HPEN oldPen;
    int i;
    BOOL havePoint = FALSE;

    if (count <= 0 || maxY <= 0.0)
        return;

    pen = CreatePen(PS_SOLID, 2, color);
    if (!pen)
        return;
    oldPen = (HPEN)SelectObject(hdc, pen);

    for (i = 0; i < count; i++)
    {
        int idx = pos - (count - 1) + i;
        double value;
        int x, y;
        while (idx < 0)
            idx += PROCESS_HISTORICO_PONTOS;
        while (idx >= PROCESS_HISTORICO_PONTOS)
            idx -= PROCESS_HISTORICO_PONTOS;

        value = data[idx];
        if (isnan(value))
        {
            havePoint = FALSE;
            continue;
        }
        if (value < 0.0)
            value = 0.0;
        if (value > maxY)
            value = maxY;
        x = rc.left + (count == 1 ? 0 : (int)(((double)i / (double)(count - 1)) * (rc.right - rc.left)));
        y = rc.bottom - (int)((value / maxY) * (rc.bottom - rc.top));
        if (!havePoint)
            MoveToEx(hdc, x, y, NULL);
        else
            LineTo(hdc, x, y);
        havePoint = TRUE;
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawProcessTimeLabels(HDC hdc, RECT rc, int count)
{
    int i;
    if (count <= 0)
        return;
    for (i = 0; i < 5; i++)
    {
        int x = rc.left + ((rc.right - rc.left) * i) / 4;
        char text[32];
        int secondsAgo = (int)(((long long)(count - 1) * (4 - i) * intervaloAtualizacaoMs) / 4000);
        if (i == 4)
            strcpy_s(text, sizeof(text), "agora");
        else
            snprintf(text, sizeof(text), "-%ds", secondsAgo);
        {
            RECT tr = {x - 35, rc.bottom + 6, x + 35, rc.bottom + 24};
            SetTextColor(hdc, RGB(115, 120, 126));
            DrawTextA(hdc, text, -1, &tr, DT_CENTER | DT_SINGLELINE);
        }
    }
}

static void DesenharGraficoProcessoDetalhe(HDC hdc, RECT rc,
                                           const char *titulo,
                                           const double *data, int count,
                                           int pos, double maxY, COLORREF cor)
{
    const char *names[1] = {"Processo"};
    COLORREF colors[1] = {cor};
    DrawGraphLegend(hdc, &rc, titulo, names, colors, 1, NULL);
    DrawGraphGrid(hdc, rc, maxY);
    DrawSeriesProcessView(hdc, rc, data, count, pos, maxY, cor);
    DrawProcessTimeLabels(hdc, rc, count);
}

static LRESULT CALLBACK ProcessDetailProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC paint = BeginPaint(hwnd, &ps);
        RECT client;
        HDC dc;
        HBITMAP bmp, oldBmp;
        HBRUSH bg;
        ProcessoSerieHistorico *serie;
        char titulo[256];
        int mid;
        double maxCpu = 100.0;
        double maxRam = 256.0;
        int i;

        GetClientRect(hwnd, &client);
        dc = CreateCompatibleDC(paint);
        bmp = CreateCompatibleBitmap(paint, client.right, client.bottom);
        if (!dc || !bmp)
        {
            if (dc)
                DeleteDC(dc);
            if (bmp)
                DeleteObject(bmp);
            EndPaint(hwnd, &ps);
            return 0;
        }

        oldBmp = (HBITMAP)SelectObject(dc, bmp);
        bg = CreateSolidBrush(g_mainConfig.corFundoGrafico);
        FillRect(dc, &client, bg);
        DeleteObject(bg);

        serie = EncontrarSerieProcesso(g_processDetailPid, 0);
        if (serie)
        {
            for (i = 0; i < PROCESS_HISTORICO_PONTOS; i++)
            {
                if (!isnan(serie->cpu[i]) && serie->cpu[i] > maxCpu)
                    maxCpu = serie->cpu[i];
                if (!isnan(serie->ram[i]) && serie->ram[i] > maxRam)
                    maxRam = serie->ram[i];
            }
        }

        snprintf(titulo, sizeof(titulo), "Detalhe do processo  %s  (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "desconhecido",
                 g_processDetailPid);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_mainConfig.corTextoGrafico);
        {
            RECT tr = {16, 8, client.right - 16, 34};
            HFONT oldFont = (HFONT)SelectObject(dc,
                                                GetStockObject(DEFAULT_GUI_FONT));
            DrawTextA(dc, titulo, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, oldFont);
        }

        mid = (client.bottom - 70) / 2 + 42;
        if (mid < 180)
            mid = 180;
        {
            RECT rCpu = {58, 45, client.right - 20, mid - 12};
            RECT rRam = {58, mid + 12, client.right - 20, client.bottom - 42};
            char cpuCur[64], ramCur[64];

            if (serie)
            {
                double c = serie->cpu[serie->pos];
                double r = serie->ram[serie->pos];
                snprintf(cpuCur, sizeof(cpuCur), "Atual: %s",
                         isnan(c) ? "N/A" : "-");
                if (!isnan(c))
                    snprintf(cpuCur, sizeof(cpuCur), "Atual: %.1f%%", c);
                snprintf(ramCur, sizeof(ramCur), "Atual: %s",
                         isnan(r) ? "N/A" : "-");
                if (!isnan(r))
                    snprintf(ramCur, sizeof(ramCur), "Atual: %.0f MB", r);
            }
            else
            {
                strcpy_s(cpuCur, sizeof(cpuCur), "Sem historico");
                strcpy_s(ramCur, sizeof(ramCur), "Sem historico");
            }

            if (serie)
            {
                DesenharGraficoProcessoDetalhe(dc, rCpu, "CPU do processo",
                                               serie->cpu, serie->count, serie->pos,
                                               maxCpu, g_mainConfig.corGraficoProcesso);
                DesenharGraficoProcessoDetalhe(dc, rRam, "RAM do processo",
                                               serie->ram, serie->count, serie->pos,
                                               maxRam, g_mainConfig.corGraficoRam);
            }
            else
            {
                SetTextColor(dc, RGB(100, 105, 110));
                DrawTextA(dc, "Ainda nao existe historico para este PID.", -1,
                          &rCpu, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            {
                RECT inf = {16, client.bottom - 28, client.right - 16, client.bottom - 8};
                char info[160];
                snprintf(info, sizeof(info), "%s    |    %s    |    Historico: %d pontos",
                         cpuCur, ramCur, serie ? serie->count : 0);
                SetTextColor(dc, g_mainConfig.corTextoGrafico);
                DrawTextA(dc, info, -1, &inf, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
        }

        BitBlt(paint, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 820;
        mmi->ptMinTrackSize.y = 540;
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        hProcessDetail = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void AbrirDetalheProcesso(DWORD pid, const char *nome)
{
    WNDCLASSA wc;

    g_processDetailPid = pid;
    strncpy_s(g_processDetailName, sizeof(g_processDetailName),
              nome ? nome : "", _TRUNCATE);

    if (hProcessDetail && IsWindow(hProcessDetail))
    {
        char titulo[256];
        snprintf(titulo, sizeof(titulo), "WinMon — %s (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "Processo", pid);
        SetWindowTextA(hProcessDetail, titulo);
        ShowWindow(hProcessDetail, SW_SHOWNOACTIVATE);
        SetForegroundWindow(hProcessDetail);
        InvalidateRect(hProcessDetail, NULL, FALSE);
        return;
    }

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ProcessDetailProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "WinMonProcessDetailClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    {
        char titulo[256];
        snprintf(titulo, sizeof(titulo), "WinMon — %s (PID %u)",
                 g_processDetailName[0] ? g_processDetailName : "Processo", pid);
        hProcessDetail = CreateWindowExA(
            WS_EX_TOOLWINDOW,
            wc.lpszClassName, titulo,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
            CW_USEDEFAULT, CW_USEDEFAULT, 760, 540,
            hMainWindow, NULL, GetModuleHandle(NULL), NULL);
    }

    if (hProcessDetail)
    {
        ShowWindow(hProcessDetail, SW_SHOW);
        UpdateWindow(hProcessDetail);
    }
}

