/* 08 sessions logging alerts */
/* ------------------------------------------------------------------------- */
/* Sessao de monitorizacao                                                   */
/* ------------------------------------------------------------------------- */
static int CompararDoubleSessao(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void SessaoIniciar(void)
{
    if (!g_sessionLockReady)
    {
        InitializeCriticalSection(&g_sessionLock);
        g_sessionLockReady = 1;
    }
    EnterCriticalSection(&g_sessionLock);
    ZeroMemory(&g_session, sizeof(g_session));
    g_session.inicioTickMs = GetTickCount64();
    g_sessionSampleCount = 0;
    g_sessionMarkerCount = 0;
    free(g_sessionSamples);
    g_sessionSamples = (WinMonSample *)malloc(SESSION_MAX_SAMPLES * sizeof(WinMonSample));
    LeaveCriticalSection(&g_sessionLock);
}

static void SessaoAdicionarAmostra(double cpu, double ram, double temp)
{
    WinMonSample *novo;
    SYSTEMTIME st;
    if (!g_sessionLockReady) SessaoIniciar();
    EnterCriticalSection(&g_sessionLock);
    g_session.amostras++;
    g_session.somaCpu += cpu; g_session.somaRam += ram; g_session.somaTemp += temp;
    if (g_session.amostras == 1 || cpu > g_session.maxCpu) g_session.maxCpu = cpu;
    if (g_session.amostras == 1 || ram > g_session.maxRam) g_session.maxRam = ram;
    if (g_session.amostras == 1 || temp > g_session.maxTemp) g_session.maxTemp = temp;
    if (g_session.amostras == 1 || cpu < g_session.minCpu) g_session.minCpu = cpu;
    if (g_session.amostras == 1 || ram < g_session.minRam) g_session.minRam = ram;
    if (g_session.amostras == 1 || temp < g_session.minTemp) g_session.minTemp = temp;
    if (g_sessionSamples && g_sessionSampleCount < SESSION_MAX_SAMPLES)
    {
        novo = &g_sessionSamples[g_sessionSampleCount++];
        novo->tickMs = GetTickCount64(); GetLocalTime(&st); novo->timestamp = st;
        novo->cpu = cpu; novo->ram = ram; novo->temp = temp;
    }
    LeaveCriticalSection(&g_sessionLock);
}

static void SessaoAdicionarMarcador(const char *texto)
{
    WinMonMarker *m;
    if (!g_sessionLockReady) SessaoIniciar();
    EnterCriticalSection(&g_sessionLock);
    if (g_sessionMarkerCount < SESSION_MAX_MARKERS)
    {
        m = &g_sessionMarkers[g_sessionMarkerCount++];
        m->tickMs = GetTickCount64(); GetLocalTime(&m->timestamp);
        strncpy_s(m->texto, sizeof(m->texto), (texto && texto[0]) ? texto : "Marcador", _TRUNCATE);
    }
    LeaveCriticalSection(&g_sessionLock);
    if (logAtivo && hLogCSV)
    {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(hLogCSV, "#MARCADOR,%04u-%02u-%02u %02u:%02u:%02u,%s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                (texto && texto[0]) ? texto : "Marcador");
        fflush(hLogCSV);
    }
}

static void SessaoExportarRelatorio(void)
{
    char caminho[MAX_PATH], nome[128], msg[MAX_PATH+96], csvNome[128], csvPath[MAX_PATH];
    FILE *f = NULL; SYSTEMTIME st; size_t i; double dur;
    double *cpu = NULL, *ram = NULL, *temp = NULL;
    double p95cpu = 0, p95ram = 0, p95temp = 0;
    if (!g_sessionLockReady) SessaoIniciar();
    EnterCriticalSection(&g_sessionLock);
    dur = (GetTickCount64() - g_session.inicioTickMs) / 1000.0;
    GetLocalTime(&st);
    snprintf(nome, sizeof(nome), "winmon_sessao_%04d%02d%02d_%02d%02d%02d.html", st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    CaminhoDados(nome, caminho, sizeof(caminho));
    if (fopen_s(&f, caminho, "w") != 0 || !f) { LeaveCriticalSection(&g_sessionLock); MessageBoxA(hMainWindow,"Nao foi possivel exportar o relatorio HTML.","WinMon",MB_OK|MB_ICONERROR); return; }
    if (g_sessionSampleCount)
    {
        cpu=(double*)malloc(g_sessionSampleCount*sizeof(double)); ram=(double*)malloc(g_sessionSampleCount*sizeof(double)); temp=(double*)malloc(g_sessionSampleCount*sizeof(double));
        if (cpu && ram && temp)
        {
            for(i=0;i<g_sessionSampleCount;i++){cpu[i]=g_sessionSamples[i].cpu;ram[i]=g_sessionSamples[i].ram;temp[i]=g_sessionSamples[i].temp;}
            qsort(cpu,g_sessionSampleCount,sizeof(double),CompararDoubleSessao); qsort(ram,g_sessionSampleCount,sizeof(double),CompararDoubleSessao); qsort(temp,g_sessionSampleCount,sizeof(double),CompararDoubleSessao);
            { size_t ix=(size_t)ceil(0.95*(double)g_sessionSampleCount); if(ix>0)ix--; p95cpu=cpu[ix];p95ram=ram[ix];p95temp=temp[ix]; }
        }
    }
    fprintf(f,"<!doctype html><html lang=\"pt\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Relatorio WinMon</title><style>body{font:16px Segoe UI,Arial,sans-serif;max-width:1000px;margin:32px auto;padding:0 18px;color:#17212b;background:#f4f7fa}h1{color:#12304a}.card{background:white;border-radius:12px;padding:18px;margin:14px 0;box-shadow:0 2px 10px #0001}table{width:100%%;border-collapse:collapse}th,td{text-align:left;padding:10px;border-bottom:1px solid #e4e9ef}th{background:#edf2f7}.muted{color:#64748b}</style></head><body><h1>WinMon — Relatorio da sessao</h1><p class=\"muted\">Gerado em %04u-%02u-%02u %02u:%02u:%02u</p><div class=\"card\"><b>Duracao:</b> %.1f s &nbsp; <b>Amostras:</b> %lu &nbsp; <b>Alertas:</b> %lu</div><div class=\"card\"><h2>Estatisticas</h2><table><tr><th>Metrica</th><th>Minimo</th><th>Media</th><th>Maximo</th><th>P95</th></tr>",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,dur,g_session.amostras,g_session.alertas);
    fprintf(f,"<tr><td>CPU (%%)</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td></tr>",g_session.minCpu,g_session.amostras?g_session.somaCpu/g_session.amostras:0,g_session.maxCpu,p95cpu);
    fprintf(f,"<tr><td>RAM (%%)</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td></tr>",g_session.minRam,g_session.amostras?g_session.somaRam/g_session.amostras:0,g_session.maxRam,p95ram);
    fprintf(f,"<tr><td>Temperatura (C)</td><td>%.2f</td><td>%.2f</td><td>%.2f</td><td>%.2f</td></tr>",g_session.minTemp,g_session.amostras?g_session.somaTemp/g_session.amostras:0,g_session.maxTemp,p95temp);
    fprintf(f,"</table></div><div class=\"card\"><h2>Marcadores</h2><table><tr><th>Hora</th><th>Tempo desde o inicio (s)</th><th>Descricao</th></tr>");
    for(i=0;i<g_sessionMarkerCount;i++) fprintf(f,"<tr><td>%02u:%02u:%02u</td><td>%.1f</td><td>%s</td></tr>",g_sessionMarkers[i].timestamp.wHour,g_sessionMarkers[i].timestamp.wMinute,g_sessionMarkers[i].timestamp.wSecond,(g_sessionMarkers[i].tickMs-g_session.inicioTickMs)/1000.0,g_sessionMarkers[i].texto);
    fprintf(f,"</table><p class=\"muted\">P95 calculado sobre as amostras registadas em memoria. O limite de armazenamento e %u amostras; graficos PNG nao estao incorporados nesta versao.</p></div></body></html>",SESSION_MAX_SAMPLES);
    fclose(f); free(cpu);free(ram);free(temp);
    snprintf(csvNome,sizeof(csvNome),"winmon_marcadores_%04d%02d%02d_%02d%02d%02d.csv",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    CaminhoDados(csvNome,csvPath,sizeof(csvPath));
    { FILE *mf=NULL; if(fopen_s(&mf,csvPath,"w")==0 && mf){fprintf(mf,"hora,segundos_desde_inicio,marcador\n");for(i=0;i<g_sessionMarkerCount;i++)fprintf(mf,"%02u:%02u:%02u,%.1f,\"%s\"\n",g_sessionMarkers[i].timestamp.wHour,g_sessionMarkers[i].timestamp.wMinute,g_sessionMarkers[i].timestamp.wSecond,(g_sessionMarkers[i].tickMs-g_session.inicioTickMs)/1000.0,g_sessionMarkers[i].texto);fclose(mf);} }
    LeaveCriticalSection(&g_sessionLock);
    snprintf(msg,sizeof(msg),"Relatorio HTML exportado para:\n%s",caminho); MessageBoxA(hMainWindow,msg,"WinMon",MB_OK|MB_ICONINFORMATION);
}

static void SessaoResetar(void){SessaoIniciar();AtualizarDefinicoesVisibilidade();InvalidateRect(hDashboard,NULL,FALSE);}static void MostrarAjudaAtalhos(HWND hwndPai){MessageBoxA(hwndPai,"Ctrl+S  Exportar snapshot\nCtrl+L  Ligar/desligar log CSV\nCtrl+A  Alertas\nCtrl+E  Aparencia\nCtrl+O  Overlay\nCtrl+D  Definicoes\nCtrl+Shift+O  Overlay rapido\nCtrl+Shift+C  Modo compacto\n\nDashboard: clique num cartao para abrir o grafico.\nProcessos: duplo clique abre o historico do processo.\nLigacoes: clique direito numa linha para filtrar, copiar o endereco ou terminar o processo.","Atalhos e ajuda do WinMon",MB_OK|MB_ICONINFORMATION);}

/* ------------------------------------------------------------------------- */
/* Logging CSV Contínuo                                                      */
/* ------------------------------------------------------------------------- */

static void IniciarLogCSV(void)
{
    SYSTEMTIME st;
    errno_t err;

    if (hLogCSV)
        return;

    GetLocalTime(&st);
    {
        char nome[64];
        snprintf(nome, sizeof(nome),
                 "winmon_log_%04d%02d%02d_%02d%02d%02d.csv",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        CaminhoDados(nome, logNomeFicheiro, sizeof(logNomeFicheiro));
    }

    err = fopen_s(&hLogCSV, logNomeFicheiro, "a");
    if (err != 0 || !hLogCSV)
    {
        char msg[MAX_PATH + 96];
        snprintf(msg, sizeof(msg),
                 "Nao foi possivel criar o ficheiro de log:\n%s\n(erro %d)",
                 logNomeFicheiro, (int)err);
        MessageBoxA(hMainWindow, msg, "Erro de Log", MB_OK | MB_ICONERROR);
        hLogCSV = NULL;
        logAtivo = 0;
        return;
    }

    fprintf(hLogCSV,
            "timestamp,cpu_pct,ram_pct,disk_read_bps,disk_write_bps,"
            "net_down_bps,net_up_bps,gpu_pct");

    {
        int i;
        for (i = 0; i < numNucleosMonitorizados; i++)
            fprintf(hLogCSV, ",core%d_pct", i);
        for (i = 0; i < numZonasTemp; i++)
            fprintf(hLogCSV, ",temp_zona%d_c", i);
    }

    fprintf(hLogCSV, "\n");
    fflush(hLogCSV);

    logAtivo = 1;
    logUltimoTickMs = 0; /* forca gravar a 1.a linha logo no proximo ciclo */
}

static void FecharLogCSV(void)
{
    if (hLogCSV)
    {
        fclose(hLogCSV);
        hLogCSV = NULL;
    }
    logAtivo = 0;
}

static void EscreverLinhaLog(void)
{
    SYSTEMTIME st;
    int i;

    if (!logAtivo || !hLogCSV)
        return;

    {
        /* intervalo em tempo real (independente do intervalo de atualizacao) */
        ULONGLONG agora = GetTickCount64();
        if (logUltimoTickMs != 0 &&
            agora - logUltimoTickMs < (ULONGLONG)LOG_INTERVALO_SEGUNDOS * 1000ULL - 200ULL)
            return;
        logUltimoTickMs = agora;
    }

    GetLocalTime(&st);

    fprintf(hLogCSV,
            "%04d-%02d-%02d %02d:%02d:%02d,"
            "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            ultimoCpuPercent, ultimoRamPercent,
            ultimoDiskRead, ultimoDiskWrite,
            ultimoNetDown, ultimoNetUp, ultimoGpuPercent);

    for (i = 0; i < numNucleosMonitorizados; i++)
        fprintf(hLogCSV, ",%.2f", coresCpuAtuais[i]);

    for (i = 0; i < numZonasTemp; i++)
        fprintf(hLogCSV, ",%.1f", tempAtual[i]);

    fprintf(hLogCSV, "\n");
    fflush(hLogCSV);
}

/* ------------------------------------------------------------------------- */
/* Alertas e Configuração INI                                                */
/* ------------------------------------------------------------------------- */

#define INI_FICHEIRO "winmon.ini"
#define INI_SECAO "Alertas"
#define INI_SECAO_GERAL "Geral"

static void AtualizarTextoIntervalo(void)
{
    char texto[32];

    if (!hBtnInterval)
        return;

    if (intervaloAtualizacaoMs < 1000)
        snprintf(texto, sizeof(texto), "Atualizacao: %u ms", intervaloAtualizacaoMs);
    else
        snprintf(texto, sizeof(texto), "Atualizacao: %u s", intervaloAtualizacaoMs / 1000);

    SetWindowTextA(hBtnInterval, texto);
}

static void CiclarIntervaloAtualizacao(void)
{
    size_t i;

    for (i = 0; i < sizeof(intervalosAtualizacao) / sizeof(intervalosAtualizacao[0]); i++)
    {
        if (intervaloAtualizacaoMs == intervalosAtualizacao[i])
        {
            i = (i + 1) % (sizeof(intervalosAtualizacao) / sizeof(intervalosAtualizacao[0]));
            intervaloAtualizacaoMs = intervalosAtualizacao[i];
            SetTimer(hMainWindow, TIMER_ID, intervaloAtualizacaoMs, NULL);
            {
                char valor[16];
                snprintf(valor, sizeof(valor), "%u", intervaloAtualizacaoMs);
                WritePrivateProfileStringA(INI_SECAO_GERAL, "IntervaloMs",
                                           valor, INI_FICHEIRO);
            }
            AtualizarTextoIntervalo();
            return;
        }
    }
}

static void CarregarConfigVisual(void)
{
    char val[32];
    GetPrivateProfileStringA("Aparencia", "FundoGrafico", "16579578", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corFundoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Grelha", "15262945", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGrelhaGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Eixo", "12499380", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corEixoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "TextoGrafico", "3157032", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corTextoGrafico = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Cpu", "13792035", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoCpu = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Ram", "12143505", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoRam = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "DiscoRead", "13137960", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoDiscoRead = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "DiscoWrite", "2980055", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoDiscoWrite = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "NetDown", "6262307", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoNetDown = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "NetUp", "5263565", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoNetUp = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Processo", "4605645", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoProcesso = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Core", "6263125", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoCore = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Gpu", "12470440", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.corGraficoGpu = (COLORREF)strtoul(val, NULL, 10);
    GetPrivateProfileStringA("Aparencia", "Espessura", "2", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.espessuraLinhas = atoi(val);
    GetPrivateProfileStringA("Aparencia", "MostrarGrelha", "1", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.mostrarGrelha = atoi(val) != 0;
    GetPrivateProfileStringA("Aparencia", "MostrarEixos", "1", val, sizeof(val), INI_FICHEIRO);
    g_mainConfig.mostrarEixos = atoi(val) != 0;
    if (g_mainConfig.espessuraLinhas < 1 || g_mainConfig.espessuraLinhas > 5)
        g_mainConfig.espessuraLinhas = 2;
}

static void GravarConfigVisual(void)
{
    char val[32];
#define WV(k, v)                                                       \
    do                                                                 \
    {                                                                  \
        snprintf(val, sizeof(val), "%lu", (unsigned long)(v));         \
        WritePrivateProfileStringA("Aparencia", k, val, INI_FICHEIRO); \
    } while (0)
    WV("FundoGrafico", g_mainConfig.corFundoGrafico);
    WV("Grelha", g_mainConfig.corGrelhaGrafico);
    WV("Eixo", g_mainConfig.corEixoGrafico);
    WV("TextoGrafico", g_mainConfig.corTextoGrafico);
    WV("Cpu", g_mainConfig.corGraficoCpu);
    WV("Ram", g_mainConfig.corGraficoRam);
    WV("DiscoRead", g_mainConfig.corGraficoDiscoRead);
    WV("DiscoWrite", g_mainConfig.corGraficoDiscoWrite);
    WV("NetDown", g_mainConfig.corGraficoNetDown);
    WV("NetUp", g_mainConfig.corGraficoNetUp);
    WV("Processo", g_mainConfig.corGraficoProcesso);
    WV("Core", g_mainConfig.corGraficoCore);
    WV("Gpu", g_mainConfig.corGraficoGpu);
    WV("Espessura", g_mainConfig.espessuraLinhas);
    WV("MostrarGrelha", g_mainConfig.mostrarGrelha);
    WV("MostrarEixos", g_mainConfig.mostrarEixos);
#undef WV
}

static void CarregarConfigTray(void)
{
    char val[16];
#define RV(k, d) GetPrivateProfileStringA("Tray", k, d, val, sizeof(val), INI_FICHEIRO)
    RV("MostrarCPU", "1");
    g_trayConfig.mostrarCpu = atoi(val);
    RV("MostrarRAM", "1");
    g_trayConfig.mostrarRam = atoi(val);
    RV("MostrarTemp", "0");
    g_trayConfig.mostrarTemperatura = atoi(val);
    RV("MostrarRede", "0");
    g_trayConfig.mostrarRede = atoi(val);
    RV("MostrarDisco", "0");
    g_trayConfig.mostrarDisco = atoi(val);
    RV("MostrarUptime", "0");
    g_trayConfig.mostrarUptime = atoi(val);
    RV("Clique", "1");
    g_trayConfig.restaurarComClique = atoi(val);
    RV("DuploClique", "0");
    g_trayConfig.restaurarComDuploClique = atoi(val);
#undef RV
}

static void GravarConfigTray(void)
{
    char val[16];
#define WVTR(k, v)                                                \
    do                                                            \
    {                                                             \
        snprintf(val, sizeof(val), "%d", (v));                    \
        WritePrivateProfileStringA("Tray", k, val, INI_FICHEIRO); \
    } while (0)
    WVTR("MostrarCPU", g_trayConfig.mostrarCpu);
    WVTR("MostrarRAM", g_trayConfig.mostrarRam);
    WVTR("MostrarTemp", g_trayConfig.mostrarTemperatura);
    WVTR("MostrarRede", g_trayConfig.mostrarRede);
    WVTR("MostrarDisco", g_trayConfig.mostrarDisco);
    WVTR("MostrarUptime", g_trayConfig.mostrarUptime);
    WVTR("Clique", g_trayConfig.restaurarComClique);
    WVTR("DuploClique", g_trayConfig.restaurarComDuploClique);
#undef WVTR
}

static void CarregarConfigIni(void)
{
    char val[32];

    GetPrivateProfileStringA(INI_SECAO, "LimiteCPU", "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteCpuPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteRAM", "90.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteRamPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO, "LimiteDisco", "95.0",
                             val, sizeof(val), INI_FICHEIRO);
    limiteDiscoPercent = atof(val);

    GetPrivateProfileStringA(INI_SECAO_GERAL, "IntervaloMs", "1000",
                             val, sizeof(val), INI_FICHEIRO);
    intervaloAtualizacaoMs = (UINT)atoi(val);

    g_lvAgrupar = GetPrivateProfileIntA(INI_SECAO_GERAL, "AgruparProcessos",
                                        0, INI_FICHEIRO)
                      ? 1
                      : 0;

    if (!_finite(limiteCpuPercent) || limiteCpuPercent < 1.0 || limiteCpuPercent > 100.0)
        limiteCpuPercent = LIMITE_CPU_PERCENT_DEFAULT;
    if (!_finite(limiteRamPercent) || limiteRamPercent < 1.0 || limiteRamPercent > 100.0)
        limiteRamPercent = LIMITE_RAM_PERCENT_DEFAULT;
    if (!_finite(limiteDiscoPercent) || limiteDiscoPercent < 1.0 || limiteDiscoPercent > 100.0)
        limiteDiscoPercent = LIMITE_DISCO_PERCENT_DEFAULT;
    if (intervaloAtualizacaoMs != 500 && intervaloAtualizacaoMs != 1000 &&
        intervaloAtualizacaoMs != 2000 && intervaloAtualizacaoMs != 5000)
        intervaloAtualizacaoMs = 1000;
}

static void GravarConfigIni(void)
{
    char val[32];

    snprintf(val, sizeof(val), "%.1f", limiteCpuPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteCPU", val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteRamPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteRAM", val, INI_FICHEIRO);

    snprintf(val, sizeof(val), "%.1f", limiteDiscoPercent);
    WritePrivateProfileStringA(INI_SECAO, "LimiteDisco", val, INI_FICHEIRO);
}

/* Liga/desliga a vista agrupada. persistir=0 para mudancas temporarias
 * (ex.: "Ver instancias individuais"). */
static void DefinirAgrupamentoProcessos(int ligado, int persistir)
{
    g_lvAgrupar = ligado ? 1 : 0;

    /* ordenar por PID nao faz sentido em grupos: passa a RAM (desc) */
    if (g_lvAgrupar && g_lvSortCol == 1)
    {
        g_lvSortCol = 3;
        g_lvSortDesc = 1;
    }

    SincronizarUIAgrupamento();

    if (persistir)
        WritePrivateProfileStringA(INI_SECAO_GERAL, "AgruparProcessos",
                                   g_lvAgrupar ? "1" : "0", INI_FICHEIRO);

    AtualizarListViewProcessos();
}

#define IDC_EDIT_CPU 3001
#define IDC_EDIT_RAM 3002
#define IDC_EDIT_DISCO 3003

static INT_PTR CALLBACK DialogoAlertasProc(HWND hDlg, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", limiteCpuPercent);
        SetDlgItemTextA(hDlg, IDC_EDIT_CPU, buf);

        snprintf(buf, sizeof(buf), "%.1f", limiteRamPercent);
        SetDlgItemTextA(hDlg, IDC_EDIT_RAM, buf);

        snprintf(buf, sizeof(buf), "%.1f", limiteDiscoPercent);
        SetDlgItemTextA(hDlg, IDC_EDIT_DISCO, buf);
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            char buf[16];
            double v;

            GetDlgItemTextA(hDlg, IDC_EDIT_CPU, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteCpuPercent = v;

            GetDlgItemTextA(hDlg, IDC_EDIT_RAM, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteRamPercent = v;

            GetDlgItemTextA(hDlg, IDC_EDIT_DISCO, buf, sizeof(buf));
            v = atof(buf);
            if (v >= 1.0 && v <= 100.0)
                limiteDiscoPercent = v;

            GravarConfigIni();
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
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

static void MostrarDialogoAlertas(HWND hwndPai)
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
    dt->cdit = 8;
    dt->x = 0;
    dt->y = 0;
    dt->cx = 200;
    dt->cy = 130;
    p += sizeof(DLGTEMPLATE) / sizeof(WORD);

    *p++ = 0;
    *p++ = 0;
    WRITE_STR_W(L"Configurar Limites de Alerta (%)");
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

    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 14, 70, 10, -1, 0x0082, L"Limite CPU (%):");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 34, 70, 10, -1, 0x0082, L"Limite RAM (%):");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 7, 54, 70, 10, -1, 0x0082, L"Limite Disco (%):");

    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 12, 40, 12, IDC_EDIT_CPU, 0x0081, L"");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 32, 40, 12, IDC_EDIT_RAM, 0x0081, L"");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 0, 80, 52, 40, 12, IDC_EDIT_DISCO, 0x0081, L"");

    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 34, 108, 60, 14, IDOK, 0x0080, L"OK");
    ADD_ITEM(WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 106, 108, 60, 14, IDCANCEL, 0x0080, L"Cancelar");

    DialogBoxIndirectA(GetModuleHandle(NULL),
                       (LPDLGTEMPLATE)dlgBuf,
                       hwndPai,
                       DialogoAlertasProc);
#undef WRITE_STR_W
#undef ALIGN_DWORD
#undef ADD_ITEM
}

