/* 10 overlay */
/* ------------------------------------------------------------------------- */
/* Overlay Alargado                                                          */
/* ------------------------------------------------------------------------- */

#define OV_PAD 6
#define OV_BAR_H 5
#define OV_MARGIN_SCR 12
#define OV_SEP RGB(45, 47, 52)

static BOOL ovArrastar = FALSE;
static POINT ovPtArrastar = {0, 0};

static void InicializarPerfisOverlay(void)
{
    strncpy_s(ovPerfis[0].nome, OV_NOME_MAX, "Gaming", _TRUNCATE);
    ovPerfis[0].fontePt = 10;
    strncpy_s(ovPerfis[0].fonteNome, sizeof(ovPerfis[0].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[0].opacidade = 200;
    ovPerfis[0].mostrarTemp = 0;
    ovPerfis[0].mostrarDisco = 0;
    ovPerfis[0].mostrarNet = 1;
    ovPerfis[0].mostrarGpu = 0;
    ovPerfis[0].mostrarPing = 0;
    ovPerfis[0].mostrarVram = 0;
    ovPerfis[0].mostrarFreq = 0;
    ovPerfis[0].clickThrough = 1;
    ovPerfis[0].corBg = RGB(14, 14, 16);
    ovPerfis[0].corBorda = RGB(60, 63, 70);
    ovPerfis[0].corTextoLabel = RGB(100, 190, 255);
    ovPerfis[0].corTextoValor = RGB(230, 232, 235);
    ovPerfis[0].corBarraCpu = RGB(100, 190, 255);
    ovPerfis[0].corBarraRam = RGB(110, 220, 140);
    ovPerfis[0].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[0].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[0].corBarraNet = RGB(35, 145, 95);
    ovPerfis[0].corSeparador = RGB(45, 47, 52);
    ovPerfis[0].mostrarBarras = 1;
    ovPerfis[0].mostrarSeparadores = 1;
    ovPerfis[0].espessuraBorda = 1;

    strncpy_s(ovPerfis[1].nome, OV_NOME_MAX, "Trabalho", _TRUNCATE);
    ovPerfis[1].fontePt = 13;
    strncpy_s(ovPerfis[1].fonteNome, sizeof(ovPerfis[1].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[1].opacidade = 220;
    ovPerfis[1].mostrarTemp = 0;
    ovPerfis[1].mostrarDisco = 1;
    ovPerfis[1].mostrarNet = 1;
    ovPerfis[1].mostrarGpu = 0;
    ovPerfis[1].mostrarPing = 0;
    ovPerfis[1].mostrarVram = 0;
    ovPerfis[1].mostrarFreq = 0;
    ovPerfis[1].clickThrough = 0;
    ovPerfis[1].corBg = RGB(20, 22, 26);
    ovPerfis[1].corBorda = RGB(70, 75, 85);
    ovPerfis[1].corTextoLabel = RGB(120, 200, 255);
    ovPerfis[1].corTextoValor = RGB(240, 242, 245);
    ovPerfis[1].corBarraCpu = RGB(120, 200, 255);
    ovPerfis[1].corBarraRam = RGB(130, 230, 150);
    ovPerfis[1].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[1].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[1].corBarraNet = RGB(35, 145, 95);
    ovPerfis[1].corSeparador = RGB(45, 47, 52);
    ovPerfis[1].mostrarBarras = 1;
    ovPerfis[1].mostrarSeparadores = 1;
    ovPerfis[1].espessuraBorda = 1;

    strncpy_s(ovPerfis[2].nome, OV_NOME_MAX, "Completo", _TRUNCATE);
    ovPerfis[2].fontePt = 13;
    strncpy_s(ovPerfis[2].fonteNome, sizeof(ovPerfis[2].fonteNome), "Consolas", _TRUNCATE);
    ovPerfis[2].opacidade = 210;
    ovPerfis[2].mostrarTemp = 1;
    ovPerfis[2].mostrarDisco = 1;
    ovPerfis[2].mostrarNet = 1;
    ovPerfis[2].mostrarGpu = 0;
    ovPerfis[2].mostrarPing = 0;
    ovPerfis[2].mostrarVram = 0;
    ovPerfis[2].mostrarFreq = 0;
    ovPerfis[2].clickThrough = 0;
    ovPerfis[2].corBg = RGB(14, 14, 16);
    ovPerfis[2].corBorda = RGB(60, 63, 70);
    ovPerfis[2].corTextoLabel = RGB(100, 190, 255);
    ovPerfis[2].corTextoValor = RGB(230, 232, 235);
    ovPerfis[2].corBarraCpu = RGB(100, 190, 255);
    ovPerfis[2].corBarraRam = RGB(110, 220, 140);
    ovPerfis[2].corBarraTemp = RGB(255, 180, 50);
    ovPerfis[2].corBarraDisco = RGB(215, 120, 45);
    ovPerfis[2].corBarraNet = RGB(35, 145, 95);
    ovPerfis[2].corSeparador = RGB(45, 47, 52);
    ovPerfis[2].mostrarBarras = 1;
    ovPerfis[2].mostrarSeparadores = 1;
    ovPerfis[2].espessuraBorda = 1;

    ovNumPerfis = 3;
    ovPerfilActivo = 2;
}

static void GravarPerfisOverlay(void)
{
    HKEY hkBase;
    int i;

    if (RegCreateKeyExA(HKEY_CURRENT_USER,
                        "Software\\WinMon\\Overlay", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hkBase, NULL) == ERROR_SUCCESS)
    {
        DWORD v = (DWORD)ovPerfilActivo;
        RegSetValueExA(hkBase, "PerfilActivo", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovNumPerfis;
        RegSetValueExA(hkBase, "NumPerfis", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)(ovMonitorIndex < 0 ? 0 : ovMonitorIndex);
        RegSetValueExA(hkBase, "MonitorIndex", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegCloseKey(hkBase);
    }

    for (i = 0; i < ovNumPerfis; i++)
    {
        char subchave[64];
        HKEY hk;
        snprintf(subchave, sizeof(subchave),
                 "Software\\WinMon\\Overlay\\Perfis\\%d", i);

        if (RegCreateKeyExA(HKEY_CURRENT_USER, subchave, 0, NULL,
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
            continue;

        DWORD v;
        RegSetValueExA(hk, "Nome", 0, REG_SZ,
                       (BYTE *)ovPerfis[i].nome, (DWORD)(strlen(ovPerfis[i].nome) + 1));
        v = (DWORD)ovPerfis[i].fontePt;
        RegSetValueExA(hk, "FontePt", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        RegSetValueExA(hk, "FonteNome", 0, REG_SZ,
                       (BYTE *)ovPerfis[i].fonteNome, (DWORD)(strlen(ovPerfis[i].fonteNome) + 1));
        v = (DWORD)ovPerfis[i].opacidade;
        RegSetValueExA(hk, "Opacidade", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarTemp;
        RegSetValueExA(hk, "MostrarTemp", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarDisco;
        RegSetValueExA(hk, "MostrarDisco", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarNet;
        RegSetValueExA(hk, "MostrarNet", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarGpu;
        RegSetValueExA(hk, "MostrarGpu", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarPing;
        RegSetValueExA(hk, "MostrarPing", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarVram;
        RegSetValueExA(hk, "MostrarVram", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarFreq;
        RegSetValueExA(hk, "MostrarFreq", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].clickThrough;
        RegSetValueExA(hk, "ClickThrough", 0, REG_DWORD, (BYTE *)&v, sizeof(v));

        v = (DWORD)ovPerfis[i].corBg;
        RegSetValueExA(hk, "CorBg", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBorda;
        RegSetValueExA(hk, "CorBorda", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corTextoLabel;
        RegSetValueExA(hk, "CorTextoLabel", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corTextoValor;
        RegSetValueExA(hk, "CorTextoValor", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraCpu;
        RegSetValueExA(hk, "CorBarraCpu", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraRam;
        RegSetValueExA(hk, "CorBarraRam", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraTemp;
        RegSetValueExA(hk, "CorBarraTemp", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraDisco;
        RegSetValueExA(hk, "CorBarraDisco", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corBarraNet;
        RegSetValueExA(hk, "CorBarraNet", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].corSeparador;
        RegSetValueExA(hk, "CorSeparador", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarBarras;
        RegSetValueExA(hk, "MostrarBarras", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].mostrarSeparadores;
        RegSetValueExA(hk, "MostrarSeparadores", 0, REG_DWORD, (BYTE *)&v, sizeof(v));
        v = (DWORD)ovPerfis[i].espessuraBorda;
        RegSetValueExA(hk, "EspessuraBorda", 0, REG_DWORD, (BYTE *)&v, sizeof(v));

        RegCloseKey(hk);
    }
}

static void CarregarPerfisOverlay(void)
{
    HKEY hkBase;
    DWORD v, sz;
    int i, numGuardados;

    InicializarPerfisOverlay();

    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\WinMon\\Overlay", 0, KEY_QUERY_VALUE, &hkBase) != ERROR_SUCCESS)
        return;

    sz = sizeof(DWORD);
    numGuardados = 3;
    if (RegQueryValueExA(hkBase, "NumPerfis", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
        numGuardados = (int)v;
    if (numGuardados < 3)
        numGuardados = 3;
    if (numGuardados > OV_MAX_PERFIS)
        numGuardados = OV_MAX_PERFIS;

    sz = sizeof(DWORD);
    if (RegQueryValueExA(hkBase, "PerfilActivo", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
        ovPerfilActivo = (int)v;
    sz = sizeof(DWORD);
    if (RegQueryValueExA(hkBase, "MonitorIndex", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
        ovMonitorIndex = (int)v;
    if (ovMonitorIndex < 0 || ovMonitorIndex >= ContarMonitores())
        ovMonitorIndex = 0;
    RegCloseKey(hkBase);

    for (i = 0; i < numGuardados; i++)
    {
        char subchave[64];
        HKEY hk;
        snprintf(subchave, sizeof(subchave),
                 "Software\\WinMon\\Overlay\\Perfis\\%d", i);

        if (RegOpenKeyExA(HKEY_CURRENT_USER, subchave, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
            continue;

        sz = sizeof(ovPerfis[i].nome);
        RegQueryValueExA(hk, "Nome", NULL, NULL, (BYTE *)ovPerfis[i].nome, &sz);
        sz = sizeof(ovPerfis[i].fonteNome);
        RegQueryValueExA(hk, "FonteNome", NULL, NULL, (BYTE *)ovPerfis[i].fonteNome, &sz);
        sz = sizeof(DWORD);
        if (RegQueryValueExA(hk, "FontePt", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].fontePt = (int)v;
        if (RegQueryValueExA(hk, "Opacidade", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].opacidade = (BYTE)v;
        if (RegQueryValueExA(hk, "MostrarTemp", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarTemp = (int)v;
        if (RegQueryValueExA(hk, "MostrarDisco", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarDisco = (int)v;
        if (RegQueryValueExA(hk, "MostrarNet", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarNet = (int)v;
        if (RegQueryValueExA(hk, "MostrarGpu", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarGpu = (int)v;
        if (RegQueryValueExA(hk, "MostrarPing", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarPing = (int)v;
        if (RegQueryValueExA(hk, "MostrarVram", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarVram = (int)v;
        if (RegQueryValueExA(hk, "MostrarFreq", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarFreq = (int)v;
        if (RegQueryValueExA(hk, "ClickThrough", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].clickThrough = (int)v;

        if (RegQueryValueExA(hk, "CorBg", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBg = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBorda", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBorda = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorTextoLabel", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corTextoLabel = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorTextoValor", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corTextoValor = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraCpu", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraCpu = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraRam", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraRam = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraTemp", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraTemp = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraDisco", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraDisco = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorBarraNet", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corBarraNet = (COLORREF)v;
        if (RegQueryValueExA(hk, "CorSeparador", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].corSeparador = (COLORREF)v;
        if (RegQueryValueExA(hk, "MostrarBarras", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarBarras = (int)v;
        if (RegQueryValueExA(hk, "MostrarSeparadores", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].mostrarSeparadores = (int)v;
        if (RegQueryValueExA(hk, "EspessuraBorda", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            ovPerfis[i].espessuraBorda = (int)v;

        if (ovPerfis[i].fontePt < OV_FONT_MIN)
            ovPerfis[i].fontePt = OV_FONT_MIN;
        if (ovPerfis[i].fontePt > OV_FONT_MAX)
            ovPerfis[i].fontePt = OV_FONT_MAX;
        if (ovPerfis[i].opacidade < 30)
            ovPerfis[i].opacidade = 30;
        if (ovPerfis[i].fonteNome[0] == '\0')
            strncpy_s(ovPerfis[i].fonteNome, sizeof(ovPerfis[i].fonteNome), "Consolas", _TRUNCATE);

        RegCloseKey(hk);
    }

    ovNumPerfis = numGuardados;
    if (ovPerfilActivo < 0 || ovPerfilActivo >= ovNumPerfis)
        ovPerfilActivo = 0;
}

static void AtivarPerfil(int idx)
{
    if (idx < 0 || idx >= ovNumPerfis)
        return;
    ovPerfilActivo = idx;

    if (hOverlay && overlayAtivo)
    {
        SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
        AplicarClickThrough();
        RecalcOverlaySize();
        InvalidateRect(hOverlay, NULL, FALSE);
    }
}

static void AplicarClickThrough(void)
{
    if (!hOverlay)
        return;

    LONG_PTR ex = GetWindowLongPtr(hOverlay, GWL_EXSTYLE);

    if (OV_CLICKTHRU)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;

    SetWindowLongPtr(hOverlay, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
}

typedef struct
{
    int wanted;
    int current;
    RECT *workArea;
    BOOL found;
} MonitorEnumContext;

static BOOL CALLBACK EnumerarMonitorCallback(HMONITOR hMon,
                                             HDC hdcMon,
                                             LPRECT lprcMon,
                                             LPARAM lParam)
{
    MonitorEnumContext *ctx = (MonitorEnumContext *)lParam;
    MONITORINFO mi;
    (void)hdcMon;
    (void)lprcMon;

    if (!ctx)
        return FALSE;
    if (ctx->current == ctx->wanted)
    {
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(hMon, &mi))
        {
            *ctx->workArea = mi.rcWork;
            ctx->found = TRUE;
        }
        return FALSE;
    }
    ctx->current++;
    return TRUE;
}

static BOOL ObterAreaMonitor(int monitorIndex, RECT *workArea)
{
    MonitorEnumContext ctx;
    RECT dummy = {0};

    if (!workArea)
        workArea = &dummy;
    ctx.wanted = monitorIndex < 0 ? 0 : monitorIndex;
    ctx.current = 0;
    ctx.workArea = workArea;
    ctx.found = FALSE;
    EnumDisplayMonitors(NULL, NULL, EnumerarMonitorCallback, (LPARAM)&ctx);
    return ctx.found;
}

static BOOL CALLBACK ContarMonitorCallback(HMONITOR hMon, HDC hdcMon,
                                           LPRECT lprcMon, LPARAM lParam)
{
    int *count = (int *)lParam;
    (void)hMon;
    (void)hdcMon;
    (void)lprcMon;
    if (count)
        (*count)++;
    return TRUE;
}

static int ContarMonitores(void)
{
    int count = 0;
    EnumDisplayMonitors(NULL, NULL, ContarMonitorCallback, (LPARAM)&count);
    return count > 0 ? count : 1;
}

static void MoverOverlayParaMonitorSelecionado(void)
{
    RECT wa;
    if (!hOverlay || !ObterAreaMonitor(ovMonitorIndex, &wa))
        return;
    SetWindowPos(hOverlay, HWND_TOPMOST,
                 wa.right - ovTotalW - OV_MARGIN_SCR,
                 wa.top + OV_MARGIN_SCR,
                 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
}

static void SnapOverlayAoCanto(void)
{
    RECT wa = {0}, wr = {0};
    int wx, wy;
    int snapX = -1, snapY = -1;
    int distMin;

    if (!hOverlay)
        return;

    if (!ObterAreaMonitor(ovMonitorIndex, &wa))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    GetWindowRect(hOverlay, &wr);

    wx = wr.left;
    wy = wr.top;

    struct
    {
        int x;
        int y;
    } cantos[4] = {
        {wa.left, wa.top},
        {wa.right - ovTotalW, wa.top},
        {wa.left, wa.bottom - ovTotalH},
        {wa.right - ovTotalW, wa.bottom - ovTotalH}};

    distMin = OV_SNAP_DIST + 1;

    {
        int i;
        for (i = 0; i < 4; i++)
        {
            int dx = wx - cantos[i].x;
            int dy = wy - cantos[i].y;
            int dist = (int)sqrt((double)(dx * dx + dy * dy));
            if (dist < distMin)
            {
                distMin = dist;
                snapX = cantos[i].x;
                snapY = cantos[i].y;
            }
        }
    }

    if (snapX >= 0)
    {
        SetWindowPos(hOverlay, HWND_TOPMOST,
                     snapX, snapY, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

static void RecalcOverlaySize(void)
{
    int charW = (OV_FONTE_PT * 8) / 13;
    int lineH = (OV_FONTE_PT * 20) / 13;
    int ncols, nlinhas;

    if (charW < 5)
        charW = 5;
    if (lineH < 12)
        lineH = 12;

    ovRowH = lineH;
    ovColW = charW * 11 + OV_PAD * 2;
    if (ovColW < 80)
        ovColW = 80;

    ncols = 2;
    if (OV_NET)
        ncols++;
    if (OV_GPU)
        ncols++;
    if (OV_PING)
        ncols++;
    if (OV_VRAM)
        ncols++;
    if (OV_FREQ)
        ncols++;
    if (OV_TEMP)
        ncols++;
    if (OV_DISCO)
        ncols++;

    /*
     * O overlay original foi dimensionado para poucas colunas.
     * Com a GPU opcional podemos chegar a seis; reduzir ligeiramente
     * a coluna nesse caso evita que o overlay cresça excessivamente
     * sem alterar o layout dos perfis antigos.
     */
    if (ncols >= 6 && ovColW > 108)
        ovColW = 108;

    nlinhas = 3;

    ovTotalW = OV_PAD + ncols * ovColW + OV_PAD;
    ovTotalH = OV_PAD + nlinhas * ovRowH + OV_BAR_H + OV_PAD * 2;

    if (hOverlay && overlayAtivo)
    {
        RECT wr;
        GetWindowRect(hOverlay, &wr);
        SetWindowPos(hOverlay, HWND_TOPMOST,
                     wr.left, wr.top, ovTotalW, ovTotalH,
                     SWP_NOACTIVATE);
    }
}

static void DrawMiniBar(HDC hdc, int x, int y, int w, double pct,
                        COLORREF corFill, COLORREF corBg)
{
    RECT bar = {x, y, x + w, y + OV_BAR_H};
    HBRUSH bg = CreateSolidBrush(corBg);
    FillRect(hdc, &bar, bg);
    DeleteObject(bg);

    if (pct > 0.0)
    {
        int fill = (int)(w * (pct / 100.0));
        if (fill > w)
            fill = w;
        if (fill > 0)
        {
            RECT filled = {x, y, x + fill, y + OV_BAR_H};
            HBRUSH fg = CreateSolidBrush(corFill);
            FillRect(hdc, &filled, fg);
            DeleteObject(fg);
        }
    }
}

typedef struct
{
    const char *label;
    COLORREF corLabel;
    const char *valorStr;
    const char *subStr;
    double barPct;
    COLORREF corBar;
} OvColuna;

static void DrawOvColuna(HDC hdc, int x, int y,
                         HFONT fLabel, HFONT fValor, HFONT fSub,
                         const OvColuna *c)
{
    int lh = ovRowH;
    int cw = ovColW;
    RECT tr;

    SelectObject(hdc, fLabel);
    SetTextColor(hdc, c->corLabel);
    tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
    DrawTextA(hdc, c->label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    SelectObject(hdc, fValor);
    SetTextColor(hdc, ovPerfis[ovPerfilActivo].corTextoValor);
    tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
    DrawTextA(hdc, c->valorStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += lh;

    if (c->barPct >= 0.0 && ovPerfis[ovPerfilActivo].mostrarBarras)
        DrawMiniBar(hdc, x + OV_PAD, y, cw - OV_PAD * 2,
                    c->barPct, c->corBar, RGB(40, 42, 46));
    y += OV_BAR_H + 2;

    if (c->subStr && c->subStr[0])
    {
        SelectObject(hdc, fSub);
        SetTextColor(hdc, RGB(150, 153, 158));
        tr = (RECT){x + OV_PAD, y, x + cw - OV_PAD, y + lh};
        DrawTextA(hdc, c->subStr, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT uMsg,
                                    WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bgBrush = CreateSolidBrush(ovPerfis[ovPerfilActivo].corBg);
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        COLORREF corBordaAtual = (overlayAlertaVisivel ? RGB(220, 45, 45) : ovPerfis[ovPerfilActivo].corBorda);
        int espBordaAtual = overlayAlertaVisivel
                                ? max(2, ovPerfis[ovPerfilActivo].espessuraBorda + 1)
                                : ovPerfis[ovPerfilActivo].espessuraBorda;
        HPEN penBorda = CreatePen(PS_SOLID, espBordaAtual, corBordaAtual);
        HPEN penVelho = (HPEN)SelectObject(hdc, penBorda);
        MoveToEx(hdc, 0, 0, NULL);
        LineTo(hdc, rc.right - 1, 0);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        LineTo(hdc, 0, rc.bottom - 1);
        LineTo(hdc, 0, 0);
        SelectObject(hdc, penVelho);
        DeleteObject(penBorda);

        if (OV_CLICKTHRU)
        {
            HPEN penCT = CreatePen(PS_SOLID, 2, RGB(80, 160, 80));
            HPEN pv = (HPEN)SelectObject(hdc, penCT);
            MoveToEx(hdc, 1, 1, NULL);
            LineTo(hdc, rc.right - 2, 1);
            LineTo(hdc, rc.right - 2, rc.bottom - 2);
            LineTo(hdc, 1, rc.bottom - 2);
            LineTo(hdc, 1, 1);
            SelectObject(hdc, pv);
            DeleteObject(penCT);
        }

        SetBkMode(hdc, TRANSPARENT);

        HFONT fLabel = CreateFontA(
            OV_FONTE_PT - 1, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);
        HFONT fValor = CreateFontA(
            OV_FONTE_PT + 1, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);
        HFONT fSub = CreateFontA(
            max(OV_FONTE_PT - 3, OV_FONT_MIN), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, OV_FONTE_NOME);

        HFONT fOrig = (HFONT)SelectObject(hdc, fLabel);

        int cx = OV_PAD;
        int cy = OV_PAD;
        OvColuna col;
        char vBuf[64], sBuf[64], dBuf[32], uBuf[32];
        HPEN penSep = CreatePen(PS_SOLID, 1, ovPerfis[ovPerfilActivo].corSeparador);

        snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoCpuPercent);
        if (numZonasTemp > 0)
        {
            double t = 0.0;
            int i;
            for (i = 0; i < numZonasTemp; i++)
                t += tempAtual[i];
            snprintf(sBuf, sizeof(sBuf), "%.0f\xB0"
                                         "C",
                     t / numZonasTemp);
        }
        else
        {
            snprintf(sBuf, sizeof(sBuf), "%d cores", numNucleosMonitorizados);
        }
        col = (OvColuna){"CPU", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                         ultimoCpuPercent, ovPerfis[ovPerfilActivo].corBarraCpu};
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        if (OV_FREQ)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            if (ultimoCpuFreqMHz > 0.0)
            {
                snprintf(vBuf, sizeof(vBuf), "%.2f GHz", ultimoCpuFreqMHz / 1000.0);
                snprintf(sBuf, sizeof(sBuf), "%.0f%% base", ultimoCpuPerfPct);
            }
            else
            {
                snprintf(vBuf, sizeof(vBuf), "N/A");
                snprintf(sBuf, sizeof(sBuf), "sem contador");
            }
            col = (OvColuna){"FREQ", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                             -1.0, ovPerfis[ovPerfilActivo].corBarraCpu};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        SelectObject(hdc, penSep);
        MoveToEx(hdc, cx, cy, NULL);
        LineTo(hdc, cx, rc.bottom - OV_PAD);

        {
            MEMORYSTATUSEX ms;
            ms.dwLength = sizeof(ms);
            DWORDLONG usedMB = 0, totalMB = 0;
            if (GlobalMemoryStatusEx(&ms))
            {
                totalMB = ms.ullTotalPhys / (1024 * 1024);
                usedMB = totalMB - ms.ullAvailPhys / (1024 * 1024);
            }
            snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoRamPercent);
            snprintf(sBuf, sizeof(sBuf), "%llu/%llu MB",
                     (unsigned long long)usedMB, (unsigned long long)totalMB);
        }
        col = (OvColuna){"RAM", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                         ultimoRamPercent, ovPerfis[ovPerfilActivo].corBarraRam};
        DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        cx += ovColW;

        if (OV_GPU)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            if (numGpuCounters[GPU_ENG_3D] > 0)
            {
                /* Overlay mostra apenas carga 3D (igual ao Gestor de Tarefas) */
                snprintf(vBuf, sizeof(vBuf), "%.1f%%", ultimoGpuPercent);
                snprintf(sBuf, sizeof(sBuf), "GPU 3D");
                col = (OvColuna){"GPU", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                                 ultimoGpuPercent, RGB(120, 80, 190)};
            }
            else if (numGpuCounters[GPU_ENG_COPY] > 0 ||
                     numGpuCounters[GPU_ENG_ENCODE] > 0 ||
                     numGpuCounters[GPU_ENG_DECODE] > 0)
            {
                /* GPU sem engine 3D (ex: iGPU sem DX) — mostrar N/A para 3D */
                snprintf(vBuf, sizeof(vBuf), "N/A");
                snprintf(sBuf, sizeof(sBuf), "Sem engine 3D");
                col = (OvColuna){"GPU", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf,
                                 -1.0, 0};
            }
            else
            {
                col = (OvColuna){"GPU", ovPerfis[ovPerfilActivo].corTextoLabel, "N/A",
                                 "WDDM indisponivel", -1.0, 0};
            }
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_VRAM)
        {
            double pct = -1.0;
            char usada[24];

            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            if (ultimaVramUsadaMB >= 0.0)
            {
                FormatarMemMB(ultimaVramUsadaMB, usada, sizeof(usada));
                snprintf(vBuf, sizeof(vBuf), "%s", usada);
                if (g_vramTotalMB > 0.0)
                {
                    char tot[24];
                    FormatarMemMB(g_vramTotalMB, tot, sizeof(tot));
                    snprintf(sBuf, sizeof(sBuf), "de %s", tot);
                    pct = ultimaVramUsadaMB / g_vramTotalMB * 100.0;
                }
                else
                    snprintf(sBuf, sizeof(sBuf), "em uso");
            }
            else
            {
                snprintf(vBuf, sizeof(vBuf), "N/A");
                snprintf(sBuf, sizeof(sBuf), "sem contador");
            }
            col = (OvColuna){g_vramIntegrada ? "GMEM" : "VRAM", ovPerfis[ovPerfilActivo].corTextoLabel,
                             vBuf, sBuf, pct, RGB(170, 120, 220)};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_NET)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            FormatarBytes(ultimoNetDown, dBuf, sizeof(dBuf));
            FormatarBytes(ultimoNetUp, uBuf, sizeof(uBuf));
            snprintf(vBuf, sizeof(vBuf), "\x19%s/s", dBuf);
            snprintf(sBuf, sizeof(sBuf), "\x18%s/s", uBuf);
            col = (OvColuna){"NET", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, -1.0, ovPerfis[ovPerfilActivo].corBarraNet};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_PING)
        {
            PingEstado pe;
            WifiEstado we;
            COLORREF cPing = RGB(46, 160, 110);
            double bPct = -1.0;

            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            PingWifiObter(&pe, &we);
            if (!pe.ativo)
            {
                snprintf(vBuf, sizeof(vBuf), "--");
                snprintf(sBuf, sizeof(sBuf), "a iniciar");
            }
            else if (pe.semGateway)
            {
                snprintf(vBuf, sizeof(vBuf), "N/A");
                snprintf(sBuf, sizeof(sBuf), "sem gateway");
            }
            else if (isnan(pe.ultimoMs))
            {
                snprintf(vBuf, sizeof(vBuf), "--");
                snprintf(sBuf, sizeof(sBuf), "a medir");
            }
            else if (pe.ultimoMs < 0.0)
            {
                /* pacote perdido */
                snprintf(vBuf, sizeof(vBuf), "perdido");
                snprintf(sBuf, sizeof(sBuf), "perda %.0f%%", pe.perdaJanela);
                bPct = 100.0;
                cPing = RGB(220, 60, 60);
            }
            else
            {
                snprintf(vBuf, sizeof(vBuf), "%.0f ms", pe.ultimoMs);
                if (pe.perdaJanela > 0.0)
                    snprintf(sBuf, sizeof(sBuf), "perda %.0f%%", pe.perdaJanela);
                else
                    snprintf(sBuf, sizeof(sBuf), "jit %.1f ms", pe.jitterMs);
                bPct = (pe.ultimoMs > 200.0 ? 200.0 : pe.ultimoMs) / 2.0;
                if (pe.ultimoMs >= 100.0)
                    cPing = RGB(220, 60, 60);
                else if (pe.ultimoMs >= 50.0)
                    cPing = RGB(235, 170, 40);
            }
            col = (OvColuna){"PING", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, bPct, cPing};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_TEMP)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            if (numZonasTemp > 0)
            {
                double tMax = 0.0, tMedia = 0.0;
                int i;
                for (i = 0; i < numZonasTemp; i++)
                {
                    tMedia += tempAtual[i];
                    if (tempAtual[i] > tMax)
                        tMax = tempAtual[i];
                }
                tMedia /= numZonasTemp;
                snprintf(vBuf, sizeof(vBuf), "%.0f\xB0"
                                             "C",
                         tMedia);
                snprintf(sBuf, sizeof(sBuf), "max %.0f\xB0"
                                             "C",
                         tMax);
                double bPct = (tMax > 0.0) ? (tMedia / 110.0) * 100.0 : 0.0;
                COLORREF cT = ovPerfis[ovPerfilActivo].corBarraTemp;
                col = (OvColuna){"TEMP", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, bPct, cT};
            }
            else
            {
                col = (OvColuna){"TEMP", ovPerfis[ovPerfilActivo].corTextoLabel, "N/A", "WMI off", -1.0, 0};
            }
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
            cx += ovColW;
        }

        if (OV_DISCO)
        {
            if (ovPerfis[ovPerfilActivo].mostrarSeparadores)
            {
                SelectObject(hdc, penSep);
                MoveToEx(hdc, cx, cy, NULL);
                LineTo(hdc, cx, rc.bottom - OV_PAD);
            }

            FormatarBytes(ultimoDiskRead, dBuf, sizeof(dBuf));
            FormatarBytes(ultimoDiskWrite, uBuf, sizeof(uBuf));
            snprintf(vBuf, sizeof(vBuf), "R %s/s", dBuf);
            snprintf(sBuf, sizeof(sBuf), "W %s/s", uBuf);
            col = (OvColuna){"DISCO", ovPerfis[ovPerfilActivo].corTextoLabel, vBuf, sBuf, -1.0, ovPerfis[ovPerfilActivo].corBarraDisco};
            DrawOvColuna(hdc, cx, cy, fLabel, fValor, fSub, &col);
        }

        SelectObject(hdc, penVelho);
        DeleteObject(penSep);
        SelectObject(hdc, fOrig);
        DeleteObject(fLabel);
        DeleteObject(fValor);
        DeleteObject(fSub);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == OVERLAY_ALERT_TIMER)
        {
            ULONGLONG agora = GetTickCount64();
            if (agora >= overlayAlertaAteTick)
            {
                overlayAlertaVisivel = 0;
                KillTimer(hwnd, OVERLAY_ALERT_TIMER);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            else
            {
                overlayAlertaVisivel = !overlayAlertaVisivel;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        if (!OV_CLICKTHRU)
        {
            ovArrastar = TRUE;
            ovPtArrastar.x = LOWORD(lParam);
            ovPtArrastar.y = HIWORD(lParam);
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (ovArrastar)
        {
            POINT pt;
            RECT wr;
            GetCursorPos(&pt);
            GetWindowRect(hwnd, &wr);
            SetWindowPos(hwnd, NULL,
                         wr.left + (pt.x - wr.left - ovPtArrastar.x),
                         wr.top + (pt.y - wr.top - ovPtArrastar.y),
                         0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (ovArrastar)
        {
            ovArrastar = FALSE;
            ReleaseCapture();
            SnapOverlayAoCanto();
        }
        return 0;

    case WM_RBUTTONUP:
        MostrarDialogoConfigOverlay(hwnd);
        return 0;

    case WM_LBUTTONDBLCLK:
        ToggleOverlay();
        return 0;

    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        OV_FONTE_PT += (delta > 0) ? 1 : -1;
        if (OV_FONTE_PT < OV_FONT_MIN)
            OV_FONTE_PT = OV_FONT_MIN;
        if (OV_FONTE_PT > OV_FONT_MAX)
            OV_FONTE_PT = OV_FONT_MAX;
        RecalcOverlaySize();
        InvalidateRect(hwnd, NULL, FALSE);
        GravarPerfisOverlay();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, OVERLAY_ALERT_TIMER);
        overlayAlertaVisivel = 0;
        hOverlay = NULL;
        overlayAtivo = 0;
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define IDC_OV_PERFIL_CB 4001
#define IDC_OV_NOME 4002
#define IDC_OV_FONTSIZE 4003
#define IDC_OV_FONTNAME 4004
#define IDC_OV_OPACITY 4005
#define IDC_OV_TEMP 4006
#define IDC_OV_DISCO 4007
#define IDC_OV_NET 4008
#define IDC_OV_GPU 4026
#define IDC_OV_PING 4027
#define IDC_OV_VRAM 4028
#define IDC_OV_FREQ 4029
#define IDC_OV_CLICKTHRU 4009
#define IDC_OV_BTN_NOVO 4010
#define IDC_OV_BTN_APAGAR 4011
#define IDC_OV_BTN_COR_BG 4012
#define IDC_OV_BTN_COR_BORDA 4013
#define IDC_OV_BTN_COR_LABEL 4014
#define IDC_OV_BTN_COR_VALOR 4015
#define IDC_OV_BTN_COR_CPU 4016
#define IDC_OV_BTN_COR_RAM 4017
#define IDC_OV_BTN_COR_TEMP 4018
#define IDC_OV_BTN_COR_DISCO 4019
#define IDC_OV_BTN_COR_NET 4020
#define IDC_OV_BTN_COR_SEP 4021
#define IDC_OV_BARRAS 4022
#define IDC_OV_SEPARADORES 4023
#define IDC_OV_BORDERW 4024
#define IDC_OV_MONITOR 4025

static INT_PTR CALLBACK DialogoConfigOverlayProc(HWND hDlg, UINT uMsg,
                                                 WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        char buf[32];
        int i;
        HWND hCb = GetDlgItem(hDlg, IDC_OV_PERFIL_CB);
        HWND hMon = GetDlgItem(hDlg, IDC_OV_MONITOR);
        int nMon = ContarMonitores();

        SendMessageA(hCb, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < ovNumPerfis; i++)
            SendMessageA(hCb, CB_ADDSTRING, 0, (LPARAM)ovPerfis[i].nome);
        SendMessageA(hCb, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);

        SendMessageA(hMon, CB_RESETCONTENT, 0, 0);
        {
            int mi;
            for (mi = 0; mi < nMon; mi++)
            {
                char nome[64];
                snprintf(nome, sizeof(nome), "Monitor %d%s", mi + 1,
                         mi == 0 ? " (principal)" : "");
                SendMessageA(hMon, CB_ADDSTRING, 0, (LPARAM)nome);
            }
        }
        if (ovMonitorIndex >= nMon)
            ovMonitorIndex = 0;
        SendMessageA(hMon, CB_SETCURSEL, (WPARAM)ovMonitorIndex, 0);

        SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[ovPerfilActivo].nome);
        snprintf(buf, sizeof(buf), "%d", OV_FONTE_PT);
        SetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf);
        SetDlgItemTextA(hDlg, IDC_OV_FONTNAME, OV_FONTE_NOME);
        snprintf(buf, sizeof(buf), "%d", (int)OV_OPACIDADE);
        SetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf);
        CheckDlgButton(hDlg, IDC_OV_TEMP, OV_TEMP ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_DISCO, OV_DISCO ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_NET, OV_NET ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_GPU, OV_GPU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_PING, OV_PING ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_VRAM, OV_VRAM ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_FREQ, OV_FREQ ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_CLICKTHRU, OV_CLICKTHRU ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_BARRAS, ovPerfis[ovPerfilActivo].mostrarBarras ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_OV_SEPARADORES, ovPerfis[ovPerfilActivo].mostrarSeparadores ? BST_CHECKED : BST_UNCHECKED);
        snprintf(buf, sizeof(buf), "%d", ovPerfis[ovPerfilActivo].espessuraBorda);
        SetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf);

        EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), ovPerfilActivo >= 3 ? TRUE : FALSE);
        return TRUE;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        WORD notif = HIWORD(wParam);

        if (id == IDC_OV_PERFIL_CB && notif == CBN_SELCHANGE)
        {
            int sel = (int)SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < ovNumPerfis)
            {
                char buf[32];
                ovPerfilActivo = sel;
                SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[sel].nome);
                snprintf(buf, sizeof(buf), "%d", ovPerfis[sel].fontePt);
                SetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf);
                SetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovPerfis[sel].fonteNome);
                snprintf(buf, sizeof(buf), "%d", (int)ovPerfis[sel].opacidade);
                SetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf);
                CheckDlgButton(hDlg, IDC_OV_TEMP, ovPerfis[sel].mostrarTemp ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_DISCO, ovPerfis[sel].mostrarDisco ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_NET, ovPerfis[sel].mostrarNet ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_GPU, ovPerfis[sel].mostrarGpu ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_PING, ovPerfis[sel].mostrarPing ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_VRAM, ovPerfis[sel].mostrarVram ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_FREQ, ovPerfis[sel].mostrarFreq ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_CLICKTHRU, ovPerfis[sel].clickThrough ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_BARRAS, ovPerfis[sel].mostrarBarras ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(hDlg, IDC_OV_SEPARADORES, ovPerfis[sel].mostrarSeparadores ? BST_CHECKED : BST_UNCHECKED);
                snprintf(buf, sizeof(buf), "%d", ovPerfis[sel].espessuraBorda);
                SetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf);
                EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), sel >= 3 ? TRUE : FALSE);
            }
            return TRUE;
        }

        if (id == IDC_OV_BTN_COR_BG)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBg))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_BORDA)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBorda))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_LABEL)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corTextoLabel))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_VALOR)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corTextoValor))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_CPU)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraCpu))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_RAM)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraRam))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }

        if (id == IDC_OV_BTN_COR_TEMP)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraTemp))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_DISCO)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraDisco))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_NET)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corBarraNet))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }
        if (id == IDC_OV_BTN_COR_SEP)
        {
            if (SelecionarCor(hDlg, &ovPerfis[ovPerfilActivo].corSeparador))
                AtivarPerfil(ovPerfilActivo);
            return TRUE;
        }

        if (id == IDC_OV_BTN_NOVO)
        {
            if (ovNumPerfis >= OV_MAX_PERFIS)
            {
                MessageBoxA(hDlg, "Limite de perfis atingido.", "WinMon", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            ovPerfis[ovNumPerfis] = ovPerfis[ovPerfilActivo];
            snprintf(ovPerfis[ovNumPerfis].nome, OV_NOME_MAX, "Perfil %d", ovNumPerfis + 1);
            ovNumPerfis++;
            ovPerfilActivo = ovNumPerfis - 1;

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_ADDSTRING, 0, (LPARAM)ovPerfis[ovPerfilActivo].nome);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);
            SetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[ovPerfilActivo].nome);
            EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), TRUE);
            return TRUE;
        }

        if (id == IDC_OV_BTN_APAGAR && ovPerfilActivo >= 3)
        {
            int i, idx = ovPerfilActivo;
            for (i = idx; i < ovNumPerfis - 1; i++)
                ovPerfis[i] = ovPerfis[i + 1];
            ovNumPerfis--;
            if (ovPerfilActivo >= ovNumPerfis)
                ovPerfilActivo = ovNumPerfis - 1;

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_RESETCONTENT, 0, 0);
            for (i = 0; i < ovNumPerfis; i++)
                SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_ADDSTRING, 0, (LPARAM)ovPerfis[i].nome);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_SETCURSEL, (WPARAM)ovPerfilActivo, 0);
            EnableWindow(GetDlgItem(hDlg, IDC_OV_BTN_APAGAR), ovPerfilActivo >= 3 ? TRUE : FALSE);
            return TRUE;
        }

        if (id == IDOK)
        {
            char buf[64];
            int sel = ovPerfilActivo;

            GetDlgItemTextA(hDlg, IDC_OV_NOME, ovPerfis[sel].nome, OV_NOME_MAX);
            if (ovPerfis[sel].nome[0] == '\0')
                snprintf(ovPerfis[sel].nome, OV_NOME_MAX, "Perfil %d", sel + 1);

            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_DELETESTRING, (WPARAM)sel, 0);
            SendDlgItemMessageA(hDlg, IDC_OV_PERFIL_CB, CB_INSERTSTRING, (WPARAM)sel, (LPARAM)ovPerfis[sel].nome);

            GetDlgItemTextA(hDlg, IDC_OV_FONTSIZE, buf, sizeof(buf));
            {
                int fs = atoi(buf);
                if (fs >= OV_FONT_MIN && fs <= OV_FONT_MAX)
                    ovPerfis[sel].fontePt = fs;
            }

            GetDlgItemTextA(hDlg, IDC_OV_FONTNAME, ovPerfis[sel].fonteNome, sizeof(ovPerfis[sel].fonteNome));
            if (ovPerfis[sel].fonteNome[0] == '\0')
                strncpy_s(ovPerfis[sel].fonteNome, sizeof(ovPerfis[sel].fonteNome), "Consolas", _TRUNCATE);

            GetDlgItemTextA(hDlg, IDC_OV_OPACITY, buf, sizeof(buf));
            {
                int op = atoi(buf);
                if (op < 30)
                    op = 30;
                if (op > 255)
                    op = 255;
                ovPerfis[sel].opacidade = (BYTE)op;
            }

            ovPerfis[sel].mostrarTemp = (IsDlgButtonChecked(hDlg, IDC_OV_TEMP) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarDisco = (IsDlgButtonChecked(hDlg, IDC_OV_DISCO) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarNet = (IsDlgButtonChecked(hDlg, IDC_OV_NET) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarGpu = (IsDlgButtonChecked(hDlg, IDC_OV_GPU) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarPing = (IsDlgButtonChecked(hDlg, IDC_OV_PING) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarVram = (IsDlgButtonChecked(hDlg, IDC_OV_VRAM) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarFreq = (IsDlgButtonChecked(hDlg, IDC_OV_FREQ) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].clickThrough = (IsDlgButtonChecked(hDlg, IDC_OV_CLICKTHRU) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarBarras = (IsDlgButtonChecked(hDlg, IDC_OV_BARRAS) == BST_CHECKED) ? 1 : 0;
            ovPerfis[sel].mostrarSeparadores = (IsDlgButtonChecked(hDlg, IDC_OV_SEPARADORES) == BST_CHECKED) ? 1 : 0;
            GetDlgItemTextA(hDlg, IDC_OV_BORDERW, buf, sizeof(buf));
            {
                int bw = atoi(buf);
                if (bw < 1)
                    bw = 1;
                if (bw > 4)
                    bw = 4;
                ovPerfis[sel].espessuraBorda = bw;
            }

            {
                int mi = (int)SendDlgItemMessageA(hDlg, IDC_OV_MONITOR, CB_GETCURSEL, 0, 0);
                int nMon = ContarMonitores();
                if (mi < 0 || mi >= nMon)
                    mi = 0;
                ovMonitorIndex = mi;
            }

            AtivarPerfil(sel);
            GravarPerfisOverlay();
            MoverOverlayParaMonitorSelecionado();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        if (id == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void MostrarDialogoConfigOverlay(HWND hwndPai)
{
    static WORD dlgBuf[1024];
    WORD *p = dlgBuf;

#define WRITE_STR_W(s)             \
    do                             \
    {                              \
        const wchar_t *_ws = (s);  \
        while (*_ws)               \
            *p++ = (WORD) * _ws++; \
        *p++ = 0;                  \
    } while (0)
#define ALIGN_DW()          \
    if (((ULONG_PTR)p) & 2) \
    p++
#define ADD_CTRL(sty, ex, xx, yy, ww, hh, iid, cls, txt) \
    do                                                   \
    {                                                    \
        ALIGN_DW();                                      \
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

    DLGTEMPLATE *dt = (DLGTEMPLATE *)p;
    dt->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
    dt->dwExtendedStyle = 0;
    dt->cdit = 39;
    dt->x = 0;
    dt->y = 0;
    dt->cx = 280;
    dt->cy = 259;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);
    *p++ = 0;
    *p++ = 0;
    WRITE_STR_W(L"Configurar Overlay");
    *p++ = 9;
    WRITE_STR_W(L"Segoe UI");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 8, 40, 10, -1, 0x0082, L"Perfil:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 48, 6, 120, 80, IDC_OV_PERFIL_CB, 0x0085, L"");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 172, 6, 30, 13, IDC_OV_BTN_NOVO, 0x0080, L"Novo");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 205, 6, 30, 13, IDC_OV_BTN_APAGAR, 0x0080, L"Apagar");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 25, 45, 10, -1, 0x0082, L"Monitor:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 55, 23, 180, 80, IDC_OV_MONITOR, 0x0085, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 44, 40, 10, -1, 0x0082, L"Nome:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 48, 42, 187, 12, IDC_OV_NOME, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 61, 40, 10, -1, 0x0082, L"Fonte pt:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 48, 59, 28, 12, IDC_OV_FONTSIZE, 0x0081, L"");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 82, 61, 40, 10, -1, 0x0082, L"Nome:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER, 0, 120, 59, 115, 12, IDC_OV_FONTNAME, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 78, 75, 10, -1, 0x0082, L"Opac. (30-255):");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 90, 76, 35, 12, IDC_OV_OPACITY, 0x0081, L"");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 7, 96, 38, 12, IDC_OV_NET, 0x0080, L"Rede");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 47, 96, 60, 12, IDC_OV_TEMP, 0x0080, L"Temperatura");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 108, 96, 52, 12, IDC_OV_DISCO, 0x0080, L"Disco I/O");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 161, 96, 34, 12, IDC_OV_GPU, 0x0080, L"GPU");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 197, 96, 55, 12, IDC_OV_PING, 0x0080, L"Lat\u00EAncia");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 7, 113, 40, 12, IDC_OV_VRAM, 0x0080, L"VRAM");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 47, 113, 85, 12, IDC_OV_FREQ, 0x0080, L"Frequ\u00EAncia CPU");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 7, 128, 226, 12, IDC_OV_CLICKTHRU, 0x0080, L"Click-through (cliques passam para janelas por baixo)");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 147, 70, 13, IDC_OV_BTN_COR_BG, 0x0080, L"Cor Fundo");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 82, 147, 70, 13, IDC_OV_BTN_COR_BORDA, 0x0080, L"Cor Borda");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 157, 147, 75, 13, IDC_OV_BTN_COR_LABEL, 0x0080, L"Cor Labels");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 163, 70, 13, IDC_OV_BTN_COR_VALOR, 0x0080, L"Cor Valores");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 82, 163, 70, 13, IDC_OV_BTN_COR_CPU, 0x0080, L"Cor Barra CPU");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 157, 163, 75, 13, IDC_OV_BTN_COR_RAM, 0x0080, L"Cor Barra RAM");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 179, 85, 13, IDC_OV_BTN_COR_TEMP, 0x0080, L"Cor Temp");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 97, 179, 85, 13, IDC_OV_BTN_COR_DISCO, 0x0080, L"Cor Disco");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 187, 179, 45, 13, IDC_OV_BTN_COR_NET, 0x0080, L"Cor Net");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 7, 196, 85, 13, IDC_OV_BTN_COR_SEP, 0x0080, L"Cor Separador");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 97, 196, 75, 13, IDC_OV_BARRAS, 0x0080, L"Mostrar barras");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 174, 196, 80, 13, IDC_OV_SEPARADORES, 0x0080, L"Separadores");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 214, 95, 10, -1, 0x0082, L"Espessura borda:");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 105, 212, 28, 12, IDC_OV_BORDERW, 0x0081, L"1");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 230, 260, 10, -1, 0x0082, L"Scroll no overlay altera o tamanho da fonte.");

    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 70, 243, 60, 14, IDOK, 0x0080, L"OK");
    ADD_CTRL(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 145, 243, 60, 14, IDCANCEL, 0x0080, L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL), (LPDLGTEMPLATE)dlgBuf, hwndPai, DialogoConfigOverlayProc);

#undef WRITE_STR_W
#undef ALIGN_DW
#undef ADD_CTRL
}

static void CriarOverlay(void)
{
    HINSTANCE hInst = GetModuleHandle(NULL);

    CarregarPerfisOverlay();
    RecalcOverlaySize();

    {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = hInst;
        wc.lpszClassName = OVERLAY_CLASS_NAME;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.style = CS_DBLCLKS;
        RegisterClassA(&wc);
    }

    RECT wa = {0};
    if (!ObterAreaMonitor(ovMonitorIndex, &wa))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - ovTotalW - OV_MARGIN_SCR;
    int y = wa.top + OV_MARGIN_SCR;

    hOverlay = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME, "WinMon Overlay",
        WS_POPUP,
        x, y, ovTotalW, ovTotalH,
        NULL, NULL, hInst, NULL);

    if (!hOverlay)
        return;

    SetLayeredWindowAttributes(hOverlay, 0, OV_OPACIDADE, LWA_ALPHA);
    AplicarClickThrough();
    ShowWindow(hOverlay, SW_SHOWNOACTIVATE);
    UpdateWindow(hOverlay);
    overlayAtivo = 1;
}

static void FecharOverlay(void)
{
    if (hOverlay)
    {
        DestroyWindow(hOverlay);
        hOverlay = NULL;
    }
    overlayAtivo = 0;
}

static void ToggleOverlay(void)
{
    if (overlayAtivo)
        FecharOverlay();
    else
        CriarOverlay();
}

static void AtivarAlertaOverlay(void)
{
    if (!hOverlay || !overlayAtivo)
        return;

    overlayAlertaAteTick = GetTickCount64() + OVERLAY_ALERT_MS;
    overlayAlertaVisivel = 1;
    SetTimer(hOverlay, OVERLAY_ALERT_TIMER, 250, NULL);
    InvalidateRect(hOverlay, NULL, FALSE);
}

static void AtualizarOverlay(void)
{
    if (hOverlay && overlayAtivo)
        InvalidateRect(hOverlay, NULL, FALSE);
}

void AtualizarMonitor()
{
    static char buffer[BUFFER_SIZE];
    size_t offset = 0;

    InicializarBuffer(buffer, sizeof(buffer), &offset);

    totalIntervalosAlerta = 0;
    alertaGlobalAtivo = 0;

    MonitorarSistema(buffer, BUFFER_SIZE, &offset);
    MonitorarCPU(buffer, BUFFER_SIZE, &offset);
    LerTemperaturaCPU(buffer, BUFFER_SIZE, &offset);
    MonitorarRAM(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscos(buffer, BUFFER_SIZE, &offset);
    MonitorarDiscoIO(buffer, BUFFER_SIZE, &offset);
    MonitorarRede(buffer, BUFFER_SIZE, &offset);
    MonitorarLatenciaWifi(buffer, BUFFER_SIZE, &offset);
    MonitorarGPU(buffer, BUFFER_SIZE, &offset);
    MonitorarProcessos(buffer, BUFFER_SIZE, &offset);

    {
        double tempSessao=ObterTemperaturaMaxima();
        unsigned long alertasAntes=g_alertHistoryCount;
        AdicionarHistorico(
        ultimoCpuPercent,
        ultimoRamPercent,
        tempSessao,
        ultimoDiskRead,
        ultimoDiskWrite,
        ultimoNetDown,
        ultimoNetUp,
        ultimoGpuPercent,
        ultimoGpuCopy,
        ultimoGpuEncode,
        ultimoGpuDecode);
        SessaoAdicionarAmostra(ultimoCpuPercent,ultimoRamPercent,tempSessao);
        if(g_alertHistoryCount>(int)alertasAntes)g_session.alertas+=(unsigned long)(g_alertHistoryCount-alertasAntes);
    }

    {
        int i;
        for (i = 0; i < numNucleosMonitorizados; i++)
            historico.core[i][historico.pos] = coresCpuAtuais[i];
    }

    processoCpuTop[historico.pos] = processoCpuAtual;
    processoRamTop[historico.pos] = processoRamAtual;

    {
        PingEstado pe;
        WifiEstado we;
        PingWifiObter(&pe, &we);
        historico.ping[historico.pos] = (pe.ativo && !pe.semGateway) ? pe.ultimoMs : NAN;
        historico.wifiSinal[historico.pos] = we.ligado ? (double)we.qualidade : NAN;
    }

    strncpy_s(ultimoSnapshot, BUFFER_SIZE, buffer, _TRUNCATE);

    SetWindowTextA(hEdit, buffer);
    AplicarCoresDeAlerta();
    AtualizarHistoricoAlertas();
    AtualizarTituloJanela();
    if (hAppStatus)
        SetWindowTextA(hAppStatus,
                       alertaGlobalAtivo ? "[!] Alerta ativo" : "[OK] Sistema normal");
    AtualizarTooltipTray();
    VerificarAlertasBalloon();
    if (alertaGlobalAtivo && !alertaGlobalAnterior)
        AtivarAlertaOverlay();
    alertaGlobalAnterior = alertaGlobalAtivo;
    EscreverLinhaLog();

    InvalidateRect(hGraphCPU, NULL, FALSE);
    InvalidateRect(hGraphRAM, NULL, FALSE);
    InvalidateRect(hGraphTemp, NULL, FALSE);
    InvalidateRect(hGraphDisk, NULL, FALSE);
    InvalidateRect(hGraphNet, NULL, FALSE);
    InvalidateRect(hGraphGPU, NULL, FALSE);
    InvalidateRect(hGraphProcesses, NULL, FALSE);
    InvalidateRect(hDashboard, NULL, FALSE);
    if (hProcessDetail)
        InvalidateRect(hProcessDetail, NULL, FALSE);

    /* Actualiza a lista completa de processos (aba Processos) */
    g_totalProcessosBruto = totalListaProcessos;
    AtualizarHistoricoProcessos();
    if (abaAtual == 7)
        AtualizarListViewProcessos();
    else
    {
        /* mesmo fora da aba, mantemos o filtro actualizado
           para que ao mudar de aba a lista já esteja pronta */
        FiltrarListaProcessos();
        AtualizarLabelContagemProc();
    }

    if (abaAtual == 8 && !g_compactMode)
        AtualizarLigacoes();
    else { /* deteccao de novas ligacoes continua ativa fora da aba */
        ConexoesRecolher();
        CxVerificarNovasLigacoes();
    }

    AtualizarOverlay();
    AtualizarCompacto();
}

