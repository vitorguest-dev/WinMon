/* 02 utilities and collection */
/* Balloon & WMI                                                             */
/* ------------------------------------------------------------------------- */

static void EnviarBalloon(const char *titulo, const char *msg)
{
    ULONGLONG agora = GetTickCount64();

    if (agora - ultimoBalloonTick <
        (ULONGLONG)BALLOON_COOLDOWN_SEGUNDOS * 1000ULL)
        return;

    if (!trayIconAtivo)
    {
        Shell_NotifyIconA(NIM_ADD, &nid);
        trayIconAtivo = 1;
    }

    nid.uFlags |= NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    strncpy_s(nid.szInfoTitle, sizeof(nid.szInfoTitle), titulo, _TRUNCATE);
    strncpy_s(nid.szInfo, sizeof(nid.szInfo), msg, _TRUNCATE);
    nid.uTimeout = 5000;

    Shell_NotifyIconA(NIM_MODIFY, &nid);

    nid.uFlags &= ~NIF_INFO;
    ultimoBalloonTick = agora;
}

static void AlertaLimparFicheiro(void)
{
    char caminho[MAX_PATH];
    CaminhoDados(ALERTAS_FICHEIRO, caminho, sizeof(caminho));
    DeleteFileA(caminho);
}

static void CarregarHistoricoAlertas(void)
{
    char caminho[MAX_PATH];
    char linha[512];
    FILE *f = NULL;

    CaminhoDados(ALERTAS_FICHEIRO, caminho, sizeof(caminho));
    if (fopen_s(&f, caminho, "r") != 0 || !f)
        return;

    while (fgets(linha, sizeof(linha), f))
    {
        AlertaHistoricoItem it;
        unsigned a, mo, d, h, mi, se;
        char rec[32];
        char culpado[128] = "Desconhecido";
        double v, l;

        int parsed = sscanf_s(linha, "%u-%u-%u %u:%u:%u,%31[^,],%lf,%lf,%127[^\n]",
                              &a, &mo, &d, &h, &mi, &se, rec, (unsigned)sizeof(rec), &v, &l, culpado, (unsigned)sizeof(culpado));

        if (parsed < 10)
            continue;

        ZeroMemory(&it, sizeof(it));
        it.timestamp.wYear = (WORD)a;
        it.timestamp.wMonth = (WORD)mo;
        it.timestamp.wDay = (WORD)d;
        it.timestamp.wHour = (WORD)h;
        it.timestamp.wMinute = (WORD)mi;
        it.timestamp.wSecond = (WORD)se;
        strncpy_s(it.recurso, sizeof(it.recurso), rec, _TRUNCATE);
        it.valor = v;
        it.limite = l;
        strncpy_s(it.culpado, sizeof(it.culpado), culpado, _TRUNCATE);
        AlertaAdicionarMemoria(&it);
    }
    fclose(f);
}

static void RegistarEventoAlerta(const char *recurso, double valor, double limite, const char *culpado)
{
    AlertaHistoricoItem item;
    ZeroMemory(&item, sizeof(item));
    GetLocalTime(&item.timestamp);
    strncpy_s(item.recurso, sizeof(item.recurso), recurso, _TRUNCATE);
    item.valor = valor;
    item.limite = limite;
    strncpy_s(item.culpado, sizeof(item.culpado), culpado ? culpado : "Desconhecido", _TRUNCATE);

    AlertaAdicionarMemoria(&item);
    AlertaGuardarNoFicheiro(&item);

    /* Mostrar Balloon Tooltip no Tray imediatamente com o Culpado */
    char msg[256];
    snprintf(msg, sizeof(msg), "%s %.1f (limite %.1f)\nCulpado: %s", recurso, valor, limite, item.culpado);
    EnviarBalloon("Alerta de Sistema", msg);
}

static void AtualizarHistoricoAlertas(void)
{
    int cpu = ultimoCpuPercent > limiteCpuPercent;
    int ram = ultimoRamPercent > limiteRamPercent;
    int disco = ultimoDiscoUsoPercent > limiteDiscoPercent;

    if (cpu && !g_alertCpuAnterior)
    {
        char culpado[128] = "Desconhecido";
        if (totalListaProcessos > 0)
            snprintf(culpado, sizeof(culpado), "%s (%.1f%%)", listaProcessos[0].exeFile, listaProcessos[0].cpuPercent);
        RegistarEventoAlerta("CPU", ultimoCpuPercent, limiteCpuPercent, culpado);
    }
    if (ram && !g_alertRamAnterior)
    {
        char culpado[128] = "Desconhecido";
        /* Como o array listaProcessos tem o qsort de RAM feito perto do final da rotina de monitorização, a posição 0 será o topo da RAM */
        if (totalListaProcessos > 0)
            snprintf(culpado, sizeof(culpado), "%s (%lu MB)", listaProcessos[0].exeFile, (unsigned long)listaProcessos[0].memUsageMB);
        RegistarEventoAlerta("RAM", ultimoRamPercent, limiteRamPercent, culpado);
    }
    if (disco && !g_alertDiscoAnterior)
    {
        RegistarEventoAlerta("Disco", ultimoDiscoUsoPercent, limiteDiscoPercent, "I/O Intensivo");
    }

    g_alertCpuAnterior = cpu;
    g_alertRamAnterior = ram;
    g_alertDiscoAnterior = disco;
}

static void MostrarDialogoHistoricoAlertas(HWND hwndPai);

static void AdicionarHistorico(double cpu, double ram, double temp,
                               double diskRead, double diskWrite,
                               double netDown, double netUp, double gpu,
                               double gpuCopy, double gpuEncode, double gpuDecode)
{
    int i;

    historico.pos = (historico.pos + 1) % HISTORICO_PONTOS;
    historico.cpu[historico.pos] = cpu;
    historico.ram[historico.pos] = ram;
    historico.temp[historico.pos] = temp;
    historico.diskRead[historico.pos] = diskRead;
    historico.diskWrite[historico.pos] = diskWrite;
    historico.netDown[historico.pos] = netDown;
    historico.netUp[historico.pos] = netUp;
    historico.gpu[historico.pos] = gpu;
    historico.gpuCopy[historico.pos] = gpuCopy;
    historico.gpuEncode[historico.pos] = gpuEncode;
    historico.gpuDecode[historico.pos] = gpuDecode;
    GetLocalTime(&historico.timestamp[historico.pos]);
    historico.tickMs[historico.pos] = GetTickCount64();

    for (i = 0; i < numNucleosMonitorizados; i++)
    {
        historico.core[i][historico.pos] = 0.0;
    }

    processoCpuTop[historico.pos] = 0.0;
    processoRamTop[historico.pos] = 0.0;
    historico.ping[historico.pos] = NAN;
    historico.wifiSinal[historico.pos] = NAN;

    if (historico.count < HISTORICO_PONTOS)
        historico.count++;
}

/* ------------------------------------------------------------------------- */
/* Recolha de Dados                                                          */
/* ------------------------------------------------------------------------- */

void MonitorarSistema(char *buffer, size_t size, size_t *offset)
{
    ULONGLONG uptimeMs = GetTickCount64();
    int dias = (int)(uptimeMs / (1000ULL * 60 * 60 * 24));
    int horas = (int)((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int minutos = (int)((uptimeMs / (1000ULL * 60)) % 60);

    AppendFormat(buffer, size, offset, "=== [ SISTEMA & UPTIME ] ===\r\n"
                                       "Tempo de Atividade: %d dias, %d horas, %d minutos\r\n",
                 dias, horas, minutos);

    SYSTEM_POWER_STATUS statusEnergia;
    if (GetSystemPowerStatus(&statusEnergia))
    {
        if (statusEnergia.BatteryLifePercent != 255)
        {
            const char *fonte = (statusEnergia.ACLineStatus == 1)
                                    ? "Carregador Conectado"
                                    : "Em Bateria";
            AppendFormat(buffer, size, offset, "Energia: %d%% (%s)\r\n",
                         statusEnergia.BatteryLifePercent, fonte);
        }
        else
        {
            AppendFormat(buffer, size, offset, "Energia: Desktop (Sem Bateria)\r\n");
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

/* =========================================================================
 * Frequencia do CPU e memoria da GPU
 * ========================================================================= */

/* Frequencia atual = frequencia nominal x "% Processor Performance" / 100
 * (e assim que o Gestor de Tarefas a calcula; passa de 100% em turbo). */
static void CpuFreqInicializar(void)
{
    HKEY hk;

    PdhAddEnglishCounterA(hQuery,
                          "\\Processor Information(_Total)\\Processor Frequency",
                          0, &hCounterCpuFreq);
    PdhAddEnglishCounterA(hQuery,
                          "\\Processor Information(_Total)\\% Processor Performance",
                          0, &hCounterCpuPerf);

    /* fallback para a frequencia base: valor ~MHz do registry */
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        DWORD v = 0, sz = sizeof(v);
        if (RegQueryValueExA(hk, "~MHz", NULL, NULL, (BYTE *)&v, &sz) == ERROR_SUCCESS)
            g_cpuBaseMhzReg = (double)v;
        RegCloseKey(hk);
    }
}

static void CpuFreqAtualizar(void)
{
    PDH_FMT_COUNTERVALUE v;
    double base = 0.0, perf = 0.0;
    int temPerf = 0;

    if (hCounterCpuFreq &&
        PdhGetFormattedCounterValue(hCounterCpuFreq, PDH_FMT_DOUBLE, NULL, &v) == ERROR_SUCCESS &&
        v.CStatus == ERROR_SUCCESS && v.doubleValue > 0.0)
        base = v.doubleValue;
    if (base <= 0.0)
        base = g_cpuBaseMhzReg;

    if (hCounterCpuPerf &&
        PdhGetFormattedCounterValue(hCounterCpuPerf, PDH_FMT_DOUBLE, NULL, &v) == ERROR_SUCCESS &&
        v.CStatus == ERROR_SUCCESS && v.doubleValue > 0.0)
    {
        perf = v.doubleValue;
        temPerf = 1;
    }

    ultimoCpuFreqBaseMHz = base;
    ultimoCpuPerfPct = temPerf ? perf : 0.0;
    ultimoCpuFreqMHz = (base > 0.0 && temPerf) ? base * perf / 100.0 : 0.0;
}

/* Total de memoria da GPU: DXGI carregado dinamicamente (sem novas libs).
 * Escolhe o adaptador fisico com mais memoria dedicada; em GPUs integradas
 * (< 1 GB dedicado) o total passa a ser dedicada + partilhada. */
static const GUID LOCAL_IID_IDXGIFactory1 =
    {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
typedef HRESULT(WINAPI *PFN_CreateDXGIFactory1)(REFIID, void **);

static LUID g_vramLuid;
static int g_vramLuidValido = 0;

static void GpuMemDetetarAdaptador(void)
{
    HMODULE hDxgi;
    PFN_CreateDXGIFactory1 fCreate;
    IDXGIFactory1 *fac = NULL;
    SIZE_T melhor = 0;
    UINT i;

    hDxgi = LoadLibraryExW(L"dxgi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hDxgi)
        return;

    fCreate = (PFN_CreateDXGIFactory1)(void *)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (fCreate && SUCCEEDED(fCreate(&LOCAL_IID_IDXGIFactory1, (void **)&fac)) && fac)
    {
        for (i = 0;; i++)
        {
            IDXGIAdapter1 *ad = NULL;
            DXGI_ADAPTER_DESC1 d;

            if (fac->lpVtbl->EnumAdapters1(fac, i, &ad) != S_OK || !ad)
                break;

            if (SUCCEEDED(ad->lpVtbl->GetDesc1(ad, &d)) &&
                !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                SIZE_T ded = d.DedicatedVideoMemory;
                if (!g_vramLuidValido || ded > melhor)
                {
                    melhor = ded;
                    g_vramLuid = d.AdapterLuid;
                    g_vramLuidValido = 1;
                    if (ded < (SIZE_T)1024 * 1024 * 1024)
                    {
                        g_vramIntegrada = 1;
                        g_vramTotalMB = ((double)ded + (double)d.SharedSystemMemory) / 1048576.0;
                    }
                    else
                    {
                        g_vramIntegrada = 0;
                        g_vramTotalMB = (double)ded / 1048576.0;
                    }
                }
            }
            ad->lpVtbl->Release(ad);
        }
        fac->lpVtbl->Release(fac);
    }
    /* dxgi.dll fica carregada ate ao fim do processo (como no Gestor de Tarefas) */
}

/* Adiciona ao query os contadores que correspondem ao wildcard;
 * se luidTag != NULL so os da GPU escolhida (comparacao sem maiusculas). */
static void GpuMemAdicionarContadores(const char *wildcard, const char *luidTag)
{
    DWORD chars = 0;
    PDH_STATUS st;
    char *paths;

    st = PdhExpandWildCardPathA(NULL, wildcard, NULL, &chars, 0);
    if (!((st == ERROR_SUCCESS || st == PDH_MORE_DATA) &&
          chars > 1 && chars <= (1024UL * 1024UL)))
        return;

    paths = (char *)malloc((size_t)chars);
    if (!paths)
        return;

    st = PdhExpandWildCardPathA(NULL, wildcard, paths, &chars, 0);
    if (st == ERROR_SUCCESS)
    {
        const char *p = paths;
        while (*p && numVramCounters < MAX_VRAM_COUNTERS)
        {
            int ok = 1;

            if (luidTag && *luidTag)
            {
                char low[512];
                size_t k, n = strlen(p);
                if (n >= sizeof(low))
                    n = sizeof(low) - 1;
                for (k = 0; k < n; k++)
                    low[k] = (char)((p[k] >= 'A' && p[k] <= 'Z') ? p[k] + 32 : p[k]);
                low[n] = '\0';
                ok = strstr(low, luidTag) != NULL;
            }

            if (ok)
            {
                PDH_HCOUNTER c = NULL;
                if (PdhAddEnglishCounterA(hQuery, p, 0, &c) == ERROR_SUCCESS && c)
                    hCounterVram[numVramCounters++] = c;
            }
            p += strlen(p) + 1;
        }
    }
    free(paths);
}

static void GpuMemInicializar(void)
{
    char tag[64];
    const char *tagUsada = NULL;

    tag[0] = '\0';
    GpuMemDetetarAdaptador();

    if (g_vramLuidValido)
    {
        snprintf(tag, sizeof(tag), "luid_0x%08lx_0x%08lx",
                 (unsigned long)g_vramLuid.HighPart, (unsigned long)g_vramLuid.LowPart);
        tagUsada = tag;
    }

    GpuMemAdicionarContadores("\\GPU Adapter Memory(*)\\Dedicated Usage", tagUsada);
    if (numVramCounters == 0 && tagUsada)
    {
        /* nome da instancia diferente do esperado: usa todos os adaptadores */
        tagUsada = NULL;
        GpuMemAdicionarContadores("\\GPU Adapter Memory(*)\\Dedicated Usage", NULL);
    }
    if (g_vramIntegrada && numVramCounters > 0)
        GpuMemAdicionarContadores("\\GPU Adapter Memory(*)\\Shared Usage", tagUsada);
}

static void GpuMemAtualizar(void)
{
    double soma = 0.0;
    int ok = 0, i;

    for (i = 0; i < numVramCounters; i++)
    {
        PDH_FMT_COUNTERVALUE v;
        if (hCounterVram[i] &&
            PdhGetFormattedCounterValue(hCounterVram[i], PDH_FMT_DOUBLE, NULL, &v) == ERROR_SUCCESS &&
            v.CStatus == ERROR_SUCCESS && v.doubleValue >= 0.0)
        {
            soma += v.doubleValue;
            ok = 1;
        }
    }

    ultimaVramUsadaMB = ok ? soma / 1048576.0 : -1.0;
    if (ok && g_vramTotalMB > 0.0 && ultimaVramUsadaMB > g_vramTotalMB)
        ultimaVramUsadaMB = g_vramTotalMB;
}

static void FormatarMemMB(double mb, char *out, size_t n)
{
    if (mb >= 1024.0)
        snprintf(out, n, "%.1f GB", mb / 1024.0);
    else
        snprintf(out, n, "%.0f MB", mb);
}

/* "5.2/8.0 GB", "5.2 GB" (total desconhecido) ou "N/A" */
static void TextoVramCurto(char *out, size_t n)
{
    if (ultimaVramUsadaMB < 0.0)
    {
        snprintf(out, n, "N/A");
        return;
    }
    if (g_vramTotalMB >= 1024.0)
        snprintf(out, n, "%.1f/%.1f GB", ultimaVramUsadaMB / 1024.0, g_vramTotalMB / 1024.0);
    else if (g_vramTotalMB > 0.0)
        snprintf(out, n, "%.0f/%.0f MB", ultimaVramUsadaMB, g_vramTotalMB);
    else
        FormatarMemMB(ultimaVramUsadaMB, out, n);
}

void MonitorarCPU(char *buffer, size_t size, size_t *offset)
{
    PDH_FMT_COUNTERVALUE counterVal;
    double cpuAtual = 0.0;
    int i;

    PdhCollectQueryData(hQuery);

    if (hCounterCPU &&
        PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal) == ERROR_SUCCESS &&
        counterVal.CStatus == ERROR_SUCCESS)
    {
        cpuAtual = counterVal.doubleValue;
    }

    ultimoCpuPercent = cpuAtual;
    CpuFreqAtualizar();

    {
        long inicioLinha = (long)*offset;
        AppendFormat(buffer, size, offset, "=== [ PROCESSADOR ] ===\r\n"
                                           "Uso Atual da CPU (Total): %.1f%%\r\n",
                     cpuAtual);
        if (cpuAtual > limiteCpuPercent)
            RegistarAlerta(inicioLinha, (long)*offset);
    }

    if (ultimoCpuFreqMHz > 0.0)
        AppendFormat(buffer, size, offset,
                     "Frequencia atual: %.2f GHz (base %.2f GHz, %.0f%%)\r\n",
                     ultimoCpuFreqMHz / 1000.0, ultimoCpuFreqBaseMHz / 1000.0,
                     ultimoCpuPerfPct);

    if (numNucleosMonitorizados > 0)
    {
        AppendFormat(buffer, size, offset, "Por Nucleo: ");

        for (i = 0; i < numNucleosMonitorizados; i++)
        {
            PDH_FMT_COUNTERVALUE coreVal;
            double corePercent = 0.0;

            if (hCounterCoresCPU[i] &&
                PdhGetFormattedCounterValue(hCounterCoresCPU[i], PDH_FMT_DOUBLE,
                                            NULL, &coreVal) == ERROR_SUCCESS &&
                coreVal.CStatus == ERROR_SUCCESS)
            {
                corePercent = coreVal.doubleValue;
            }

            coresCpuAtuais[i] = corePercent;

            AppendFormat(buffer, size, offset, "C%d:%5.1f%%  ", i, corePercent);

            if ((i + 1) % 4 == 0 && (i + 1) < numNucleosMonitorizados)
                AppendFormat(buffer, size, offset, "\r\n            ");
        }

        AppendFormat(buffer, size, offset, "\r\n");
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarDiscoIO(char *buffer, size_t size, size_t *offset)
{
    PDH_FMT_COUNTERVALUE readVal, writeVal;
    double bytesLeitura = 0.0, bytesEscrita = 0.0;

    if (hCounterDiskRead &&
        PdhGetFormattedCounterValue(hCounterDiskRead, PDH_FMT_DOUBLE, NULL, &readVal) == ERROR_SUCCESS &&
        readVal.CStatus == ERROR_SUCCESS)
    {
        bytesLeitura = readVal.doubleValue;
    }

    if (hCounterDiskWrite &&
        PdhGetFormattedCounterValue(hCounterDiskWrite, PDH_FMT_DOUBLE, NULL, &writeVal) == ERROR_SUCCESS &&
        writeVal.CStatus == ERROR_SUCCESS)
    {
        bytesEscrita = writeVal.doubleValue;
    }

    ultimoDiskRead = bytesLeitura;
    ultimoDiskWrite = bytesEscrita;

    {
        char leituraStr[32], escritaStr[32];
        FormatarBytes(bytesLeitura, leituraStr, sizeof(leituraStr));
        FormatarBytes(bytesEscrita, escritaStr, sizeof(escritaStr));

        AppendFormat(buffer, size, offset, "=== [ DISCO - VELOCIDADE I/O (TOTAL) ] ===\r\n"
                                           "Leitura: %-10s/s | Escrita: %-10s/s\r\n\r\n",
                     leituraStr, escritaStr);
    }
}

void MonitorarRAM(char *buffer, size_t size, size_t *offset)
{
    MEMORYSTATUSEX memInfo;
    ZeroMemory(&memInfo, sizeof(memInfo));
    memInfo.dwLength = sizeof(memInfo);

    if (GlobalMemoryStatusEx(&memInfo))
    {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        DWORDLONG usadaRAM = totalRAM - livreRAM;

        ultimoRamPercent = (double)memInfo.dwMemoryLoad;

        {
            long inicioLinha = (long)*offset;

            AppendFormat(buffer, size, offset, "=== [ MEMORIA RAM GERAL ] ===\r\n"
                                               "Uso: %ld%% | Usada: %llu MB | Livre: %llu MB (Total: %llu MB)\r\n\r\n",
                         memInfo.dwMemoryLoad, usadaRAM, livreRAM, totalRAM);

            if ((double)memInfo.dwMemoryLoad > limiteRamPercent)
                RegistarAlerta(inicioLinha, (long)*offset);
        }
    }
}

void MonitorarDiscos(char *buffer, size_t size, size_t *offset)
{
    DWORD drives = GetLogicalDrives();
    ultimoDiscoUsoPercent = 0.0;
    char driveLetter[] = "A:\\";
    int i;

    AppendFormat(buffer, size, offset, "=== [ DISCOS DE ARMAZENAMENTO ] ===\r\n");

    for (i = 0; i < 26; i++)
    {
        if (drives & (1UL << i))
        {
            driveLetter[0] = (char)('A' + i);

            if (GetDriveTypeA(driveLetter) == DRIVE_FIXED)
            {
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;

                if (GetDiskFreeSpaceExA(driveLetter, &freeBytesAvailable,
                                        &totalBytes, &totalFreeBytes))
                {
                    double totalGB = (double)totalBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double livreGB = (double)totalFreeBytes.QuadPart /
                                     (1024.0 * 1024.0 * 1024.0);
                    double usadaGB = totalGB - livreGB;
                    double percentUsado = totalGB > 0.0
                                              ? (usadaGB / totalGB) * 100.0
                                              : 0.0;

                    long inicioLinha = (long)*offset;

                    AppendFormat(buffer, size, offset, "Drive %s  Uso: %5.1f%%  (%.1f GB usad. de %.1f GB)\r\n",
                                 driveLetter, percentUsado, usadaGB, totalGB);

                    if (percentUsado > limiteDiscoPercent)
                        RegistarAlerta(inicioLinha, (long)*offset);
                }
            }
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");
}

void MonitorarRede(char *buffer, size_t size, size_t *offset)
{
    ULONG outBufLen = 0;
    PMIB_IFTABLE pIfTable;
    DWORDLONG currentIn = 0, currentOut = 0;
    DWORD i;

    if (GetIfTable(NULL, &outBufLen, FALSE) != ERROR_INSUFFICIENT_BUFFER ||
        outBufLen == 0 ||
        outBufLen > (1024UL * 1024UL * 8UL))
    {
        return;
    }

    pIfTable = (PMIB_IFTABLE)malloc(outBufLen);
    if (!pIfTable)
        return;

    if (GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR)
    {
        for (i = 0; i < pIfTable->dwNumEntries; i++)
        {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }

        if (!firstNetworkRead)
        {
            double down = (double)(currentIn >= lastIn ? currentIn - lastIn : 0);
            double up = (double)(currentOut >= lastOut ? currentOut - lastOut : 0);

            ultimoNetDown = down;
            ultimoNetUp = up;

            {
                char downStr[32], upStr[32];
                FormatarBytes(down, downStr, sizeof(downStr));
                FormatarBytes(up, upStr, sizeof(upStr));

                AppendFormat(buffer, size, offset, "=== [ REDE EM TEMPO REAL ] ===\r\n"
                                                   "Download: %-10s/s | Upload: %-10s/s\r\n\r\n",
                             downStr, upStr);
            }
        }
        else
        {
            ultimoNetDown = 0.0;
            ultimoNetUp = 0.0;
        }

        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
    }

    free(pIfTable);
}

void MonitorarLatenciaWifi(char *buffer, size_t size, size_t *offset)
{
    PingEstado p;
    WifiEstado w;

    PingWifiObter(&p, &w);

    AppendFormat(buffer, size, offset, "=== [ LATENCIA & WI-FI ] ===\r\n");

    if (!p.ativo)
        AppendFormat(buffer, size, offset, "Ping: a iniciar...\r\n");
    else if (p.semGateway)
        AppendFormat(buffer, size, offset, "Ping (%s): sem gateway (clica no grafico de Rede para mudar de alvo)\r\n", p.alvoNome);
    else if (isnan(p.ultimoMs))
        AppendFormat(buffer, size, offset, "Ping (%s): a medir...\r\n", p.alvoNome);
    else
    {
        char ult[32];

        if (p.ultimoMs < 0.0)
            snprintf(ult, sizeof(ult), "perdido");
        else if (p.ultimoMs < 1.0)
            snprintf(ult, sizeof(ult), "<1 ms");
        else
            snprintf(ult, sizeof(ult), "%.0f ms", p.ultimoMs);

        AppendFormat(buffer, size, offset,
                     "Ping (%s): %s | Media %.1f ms | Jitter %.1f ms | Perda %.1f%%\r\n",
                     p.alvoNome, ult, p.mediaMs, p.jitterMs, p.perdaJanela);
    }

    if (!w.adaptador)
        AppendFormat(buffer, size, offset, "Wi-Fi: sem adaptador Wi-Fi\r\n\r\n");
    else if (!w.ligado)
        AppendFormat(buffer, size, offset, "Wi-Fi: desligado (%s)\r\n\r\n", w.adaptadorNome);
    else
        AppendFormat(buffer, size, offset,
                     "Wi-Fi: %s | Sinal %d%% (~%.0f dBm) | %lu/%lu Mbps | %s | %s\r\n\r\n",
                     w.ssid, w.qualidade, w.dbm,
                     (unsigned long)(w.rxKbps / 1000), (unsigned long)(w.txKbps / 1000),
                     WifiPhyNome(w.phy), WifiAuthNome(w.auth));
}

void MonitorarGPU(char *buffer, size_t size, size_t *offset)
{
    /* Nomes internos dos tipos de engine (mesmos que o Gestor de Tarefas) */
    static const char *nomeEng[GPU_ENG_COUNT] = {"3D", "Copy", "Encode", "Decode"};

    double valores[GPU_ENG_COUNT] = {0.0, 0.0, 0.0, 0.0};
    int valido[GPU_ENG_COUNT] = {0, 0, 0, 0};
    int e, i;
    int algumValido = 0;

    GpuMemAtualizar();

    /*
     * O Gestor de Tarefas soma a utilizacao de todos os engines
     * do mesmo tipo (3D, Copy, Encode, Decode) e limita a 100%.
     * Fazemos o mesmo: para cada tipo, somamos os valores PDH de
     * todos os contadores desse tipo e clampamos em [0, 100].
     */
    for (e = 0; e < GPU_ENG_COUNT; e++)
    {
        double soma = 0.0;
        for (i = 0; i < numGpuCounters[e]; i++)
        {
            PDH_FMT_COUNTERVALUE value;
            if (hCounterGpu[e][i] &&
                PdhGetFormattedCounterValue(hCounterGpu[e][i], PDH_FMT_DOUBLE,
                                            NULL, &value) == ERROR_SUCCESS &&
                value.CStatus == ERROR_SUCCESS &&
                value.doubleValue >= 0.0)
            {
                soma += value.doubleValue;
                valido[e] = 1;
            }
        }
        if (valido[e])
        {
            if (soma > 100.0)
                soma = 100.0;
            valores[e] = soma;
            algumValido = 1;
        }
    }

    ultimoGpuPercent = valido[GPU_ENG_3D] ? valores[GPU_ENG_3D] : 0.0;
    ultimoGpuCopy = valido[GPU_ENG_COPY] ? valores[GPU_ENG_COPY] : 0.0;
    ultimoGpuEncode = valido[GPU_ENG_ENCODE] ? valores[GPU_ENG_ENCODE] : 0.0;
    ultimoGpuDecode = valido[GPU_ENG_DECODE] ? valores[GPU_ENG_DECODE] : 0.0;

    if (algumValido)
    {
        AppendFormat(buffer, size, offset, "=== [ GPU ] ===\r\n");
        for (e = 0; e < GPU_ENG_COUNT; e++)
        {
            if (valido[e])
                AppendFormat(buffer, size, offset,
                             "GPU %s: %.1f%%\r\n", nomeEng[e], valores[e]);
            else
                AppendFormat(buffer, size, offset,
                             "GPU %s: N/A\r\n", nomeEng[e]);
        }
        if (ultimaVramUsadaMB >= 0.0)
        {
            char mem[40];
            TextoVramCurto(mem, sizeof(mem));
            AppendFormat(buffer, size, offset, "%s: %s\r\n",
                         g_vramIntegrada ? "Memoria da GPU (dedicada+partilhada)" : "VRAM", mem);
        }
        AppendFormat(buffer, size, offset, "\r\n");
    }
    else
        AppendFormat(buffer, size, offset,
                     "=== [ GPU ] ===\r\nUso Atual da GPU: N/A (contador GPU Engine indisponivel)\r\n\r\n");
}

/* Trafego por processo: TCP ESTATS (contadores cumulativos do Windows).
 * EStats e obtido dinamicamente para manter compatibilidade com sistemas/SDKs
 * onde as APIs nao estejam presentes. UDP nao e contabilizado por este metodo. */
typedef struct { BOOLEAN EnableCollection; } WM_TCP_ESTATS_DATA_RW_V0;
typedef struct {
    ULONGLONG DataBytesOut, DataSegsOut, DataBytesIn, DataSegsIn, SegsOut, SegsIn;
    ULONG SoftErrors, SoftErrorReason, SndUna, SndNxt, SndMax;
    ULONGLONG ThruBytesAcked;
    ULONG RcvNxt;
    ULONGLONG ThruBytesReceived;
} WM_TCP_ESTATS_DATA_ROD_V0;
typedef DWORD (WINAPI *WM_SET_TCP_ESTATS)(PMIB_TCPROW, int, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG);
typedef DWORD (WINAPI *WM_GET_TCP_ESTATS)(PMIB_TCPROW, int, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG, PUCHAR, ULONG, ULONG);
typedef struct { DWORD pid; ULONGLONG inBytes, outBytes; } WM_PID_TRAFFIC;
static WM_PID_TRAFFIC g_trafficPrev[MAX_PROCESSES];
static int g_trafficPrevCount = 0;
static ULONGLONG g_trafficPrevTick = 0;
static int g_estatsTried = 0;
static WM_SET_TCP_ESTATS g_setEstats = NULL;
static WM_GET_TCP_ESTATS g_getEstats = NULL;

static void AtualizarTrafegoProcessos(void)
{
    HMODULE iphlp = NULL;
    DWORD sz = 0, rc;
    MIB_TCPTABLE_OWNER_PID *tb = NULL;
    WM_PID_TRAFFIC now[MAX_PROCESSES];
    int n = 0, i, j;
    ULONGLONG tick = GetTickCount64(), elapsed = g_trafficPrevTick ? tick-g_trafficPrevTick : 0;
    if (!g_estatsTried) {
        g_estatsTried = 1;
        iphlp = GetModuleHandleA("iphlpapi.dll");
        if (!iphlp) iphlp = LoadLibraryA("iphlpapi.dll");
        if (iphlp) {
            g_setEstats = (WM_SET_TCP_ESTATS)GetProcAddress(iphlp, "SetPerTcpConnectionEStats");
            g_getEstats = (WM_GET_TCP_ESTATS)GetProcAddress(iphlp, "GetPerTcpConnectionEStats");
        }
    }
    if (!g_setEstats || !g_getEstats || elapsed == 0) { g_trafficPrevTick=tick; return; }
    rc = GetExtendedTcpTable(NULL, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc != ERROR_INSUFFICIENT_BUFFER || !sz) { g_trafficPrevTick=tick; return; }
    tb = (MIB_TCPTABLE_OWNER_PID*)malloc(sz);
    if (!tb) { g_trafficPrevTick=tick; return; }
    rc = GetExtendedTcpTable(tb, &sz, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc == NO_ERROR) {
        DWORD k;
        for (k=0; k<tb->dwNumEntries; k++) {
            MIB_TCPROW row;
            WM_TCP_ESTATS_DATA_RW_V0 rw;
            WM_TCP_ESTATS_DATA_ROD_V0 rod;
            ULONGLONG inB, outB;
            DWORD pid=tb->table[k].dwOwningPid;
            if (tb->table[k].dwState != MIB_TCP_STATE_ESTAB || pid==0) continue;
            row.dwState=tb->table[k].dwState; row.dwLocalAddr=tb->table[k].dwLocalAddr;
            row.dwLocalPort=tb->table[k].dwLocalPort; row.dwRemoteAddr=tb->table[k].dwRemoteAddr;
            row.dwRemotePort=tb->table[k].dwRemotePort;
            ZeroMemory(&rw,sizeof(rw)); ZeroMemory(&rod,sizeof(rod));
            /* Primeiro consulta o estado; só tenta ativar uma vez por ligacao
               enquanto o Windows mantiver a recolha desativada. */
            if (g_getEstats(&row, 1, (PUCHAR)&rw, 0, sizeof(rw), NULL, 0, 0,
                            (PUCHAR)&rod, 0, sizeof(rod)) != NO_ERROR || !rw.EnableCollection) {
                rw.EnableCollection=TRUE;
                if (g_setEstats(&row, 1, (PUCHAR)&rw, 0, sizeof(rw), NULL, 0, 0, NULL, 0, 0) != NO_ERROR) continue;
                ZeroMemory(&rw,sizeof(rw)); ZeroMemory(&rod,sizeof(rod));
                if (g_getEstats(&row, 1, (PUCHAR)&rw, 0, sizeof(rw), NULL, 0, 0,
                                (PUCHAR)&rod, 0, sizeof(rod)) != NO_ERROR || !rw.EnableCollection) continue;
            }
            inB=rod.DataBytesIn; outB=rod.DataBytesOut;
            for (j=0; j<n; j++) if (now[j].pid==pid) break;
            if (j==n && n<MAX_PROCESSES) { now[n].pid=pid; now[n].inBytes=0; now[n].outBytes=0; n++; }
            if (j<n) { now[j].inBytes += inB; now[j].outBytes += outB; }
        }
    }
    free(tb);
    for (i=0; i<totalListaProcessos; i++) {
        DWORD pid=listaProcessos[i].pid; ULONGLONG inB=0,outB=0; int a,b;
        for (a=0;a<n;a++) if(now[a].pid==pid){inB=now[a].inBytes;outB=now[a].outBytes;break;}
        for (b=0;b<g_trafficPrevCount;b++) if(g_trafficPrev[b].pid==pid) break;
        listaProcessos[i].netDownBps=(b<g_trafficPrevCount && inB>=g_trafficPrev[b].inBytes)?(double)(inB-g_trafficPrev[b].inBytes)*1000.0/(double)elapsed:0.0;
        listaProcessos[i].netUpBps=(b<g_trafficPrevCount && outB>=g_trafficPrev[b].outBytes)?(double)(outB-g_trafficPrev[b].outBytes)*1000.0/(double)elapsed:0.0;
    }
    memcpy(g_trafficPrev,now,(size_t)n*sizeof(now[0])); g_trafficPrevCount=n; g_trafficPrevTick=tick;
}

void MonitorarProcessos(char *buffer, size_t size, size_t *offset)
{
    FILETIME ftIdle, ftKernelSys, ftUserSys;
    ULONGLONG currentSystemTime, deltaSystemTime;
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;
    int totalProcessos = 0;
    ProcessoCpuHistorico novoHistorico[MAX_HISTORICO];
    int totalNovoHistorico = 0;
    int i;

    AppendFormat(buffer, size, offset, "=== [ TOP 12 PROCESSOS (MAIOR CONSUMO CPU) ] ===\r\n");

    ZeroMemory(&ftIdle, sizeof(ftIdle));
    ZeroMemory(&ftKernelSys, sizeof(ftKernelSys));
    ZeroMemory(&ftUserSys, sizeof(ftUserSys));
    if (!GetSystemTimes(&ftIdle, &ftKernelSys, &ftUserSys))
    {
        AppendFormat(buffer, size, offset,
                     "Nao foi possivel obter os tempos do sistema.\r\n\r\n");
        return;
    }
    currentSystemTime = FileTimeToU64(ftKernelSys) +
                        FileTimeToU64(ftUserSys);
    deltaSystemTime = (lastSystemTime != 0)
                          ? (currentSystemTime - lastSystemTime)
                          : 0;

    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hProcessSnap == INVALID_HANDLE_VALUE)
    {
        AppendFormat(buffer, size, offset, "Nao foi possivel enumerar processos.\r\n\r\n");
        return;
    }

    ZeroMemory(&pe32, sizeof(pe32));
    pe32.dwSize = sizeof(pe32);

    if (Process32First(hProcessSnap, &pe32))
    {
        do
        {
            if (pe32.th32ProcessID != 0 && totalProcessos < MAX_PROCESSES)
            {
                HANDLE hProcess = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, pe32.th32ProcessID);

                SIZE_T memUsageMB = 0;
                double cpuPercent = 0.0;

                if (hProcess)
                {
                    PROCESS_MEMORY_COUNTERS pmc;

                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
                        memUsageMB = pmc.WorkingSetSize / (1024 * 1024);

                    {
                        FILETIME ftCreate, ftExit, ftKernel, ftUser;

                        if (GetProcessTimes(hProcess, &ftCreate, &ftExit,
                                            &ftKernel, &ftUser))
                        {
                            ULONGLONG kernelU64 = FileTimeToU64(ftKernel);
                            ULONGLONG userU64 = FileTimeToU64(ftUser);
                            ULONGLONG totalProcTime = kernelU64 + userU64;
                            ProcessoCpuHistorico *hist =
                                EncontrarHistorico(pe32.th32ProcessID);

                            if (hist && deltaSystemTime > 0)
                            {
                                ULONGLONG anterior =
                                    hist->lastKernelTime + hist->lastUserTime;
                                ULONGLONG deltaProc =
                                    totalProcTime >= anterior
                                        ? totalProcTime - anterior
                                        : 0;

                                cpuPercent =
                                    ((double)deltaProc /
                                     (double)deltaSystemTime) *
                                    100.0 * numProcessadores;

                                if (cpuPercent < 0.0)
                                    cpuPercent = 0.0;
                                if (cpuPercent > 100.0 * numProcessadores)
                                    cpuPercent = 100.0 * numProcessadores;
                            }

                            if (totalNovoHistorico < MAX_HISTORICO)
                            {
                                novoHistorico[totalNovoHistorico].pid =
                                    pe32.th32ProcessID;
                                novoHistorico[totalNovoHistorico].lastKernelTime =
                                    kernelU64;
                                novoHistorico[totalNovoHistorico].lastUserTime =
                                    userU64;
                                novoHistorico[totalNovoHistorico].valido = 1;
                                totalNovoHistorico++;
                            }
                        }
                    }

                    CloseHandle(hProcess);
                }

                listaProcessos[totalProcessos].pid = pe32.th32ProcessID;
                listaProcessos[totalProcessos].memUsageMB = memUsageMB;
                listaProcessos[totalProcessos].cpuPercent = cpuPercent;
                listaProcessos[totalProcessos].netDownBps = 0.0;
                listaProcessos[totalProcessos].netUpBps = 0.0;

                strncpy_s(listaProcessos[totalProcessos].exeFile, MAX_PATH,
                          pe32.szExeFile, _TRUNCATE);

                totalProcessos++;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }

    CloseHandle(hProcessSnap);
    totalListaProcessos = totalProcessos;
    AtualizarTrafegoProcessos();

    memcpy(historicoCpu, novoHistorico,
           sizeof(ProcessoCpuHistorico) * totalNovoHistorico);
    totalHistorico = totalNovoHistorico;
    lastSystemTime = currentSystemTime;

    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo),
          CompararProcessosPorCPU);

    processoCpuAtual =
        (totalProcessos > 0) ? listaProcessos[0].cpuPercent : 0.0;

    {
        int limite = (totalProcessos < 12) ? totalProcessos : 12;

        for (i = 0; i < limite; i++)
        {
            AppendFormat(buffer, size, offset, "PID: %-6u | CPU: %5.1f%% | RAM: %5lu MB | %s\r\n",
                         listaProcessos[i].pid, listaProcessos[i].cpuPercent,
                         (unsigned long)listaProcessos[i].memUsageMB,
                         listaProcessos[i].exeFile);
        }
    }

    AppendFormat(buffer, size, offset, "\r\n");

    AppendFormat(buffer, size, offset, "=== [ TOP 5 PROCESSOS (MAIOR CONSUMO RAM) ] ===\r\n");

    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo),
          CompararProcessosPorRAM);

    processoRamAtual =
        (totalProcessos > 0) ? (double)listaProcessos[0].memUsageMB : 0.0;

    {
        int limiteRAM = (totalProcessos < 5) ? totalProcessos : 5;

        for (i = 0; i < limiteRAM; i++)
        {
            AppendFormat(buffer, size, offset, "PID: %-6u | RAM: %5lu MB | CPU: %5.1f%% | %s\r\n",
                         listaProcessos[i].pid,
                         (unsigned long)listaProcessos[i].memUsageMB,
                         listaProcessos[i].cpuPercent,
                         listaProcessos[i].exeFile);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Declaracoes antecipadas (forward declarations)                            */
/* ------------------------------------------------------------------------- */

typedef enum GraphType GraphType;

static void IniciarLogCSV(void);
static void FecharLogCSV(void);
static void EscreverLinhaLog(void);
static void MostrarDialogoAlertas(HWND hwndPai);
static void MostrarDialogoEstiloPrincipal(HWND hwndPai);
static void MostrarDialogoConfigTray(HWND hwndPai);
static void CarregarConfigVisual(void);
static void GravarConfigVisual(void);
static void CarregarConfigTray(void);
static void GravarConfigTray(void);
static void CarregarConfigIni(void);
static void GravarConfigIni(void);
static void InicializarTemperaturaCPU(void);
static void LerTemperaturaCPU(char *buffer, size_t size, size_t *offset);
static void TerminarWMI(void);
static void EnviarBalloon(const char *titulo, const char *msg);
static void VerificarAlertasBalloon(void);
static void CriarOverlay(void);
static void FecharOverlay(void);
static void ToggleOverlay(void);
static void AtualizarOverlay(void);
static void RecalcOverlaySize(void);
static void AplicarClickThrough(void);
static void SnapOverlayAoCanto(void);
static void InicializarPerfisOverlay(void);
static void GravarPerfisOverlay(void);
static void CarregarPerfisOverlay(void);
static void AtivarPerfil(int idx);
static void MostrarDialogoConfigOverlay(HWND hwndPai);
static void CriarPainelDefinicoes(HWND hwndPai);
static void AtualizarDefinicoesVisibilidade(void);
static void RedimensionarDefinicoes(const RECT *content);
static void AbrirDetalheProcesso(DWORD pid, const char *nome);
static LRESULT CALLBACK ProcessDetailProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void AtualizarHistoricoProcessos(void);
static ProcessoSerieHistorico *EncontrarSerieProcesso(DWORD pid, int criar);
static void AtivarAlertaOverlay(void);
static void AplicarEstiloJanelaPrincipal(void);
static void CriarPainelProcessos(HWND hwndPai);
static void CriarPainelLigacoes(HWND hwndPai);
static void CriarPainelRedeLocal(HWND hwndPai);
static void RedeLocalMostrar(int visivel);
static void RedeLocalRedimensionar(int x, int y, int w, int h);
static void AtualizarLigacoes(void);
static void MostrarLigacoesDoProcesso(const char *exe);
static void AtualizarListViewProcessos(void);
static void RedimensionarPainelProcessos(HWND hwndPai);
static void FiltrarListaProcessos(void);
static LRESULT CALLBACK DashboardProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam);
static void DesenharBotaoNavegacao(const DRAWITEMSTRUCT *dis)
{
    RECT rc;
    COLORREF bgColor;
    COLORREF textColor;
    COLORREF accentColor;
    HFONT font;
    HFONT oldFont;
    char texto[64];
    int ativo;
    int hover;

    if (!dis || !dis->hwndItem)
        return;

    rc = dis->rcItem;
    ativo = GetDlgCtrlID(dis->hwndItem) == TAB_RESUMO + abaAtual;
    hover = (dis->itemState & ODS_SELECTED) != 0;

    bgColor = ativo ? RGB(39, 55, 73) : (hover ? RGB(31, 45, 60) : RGB(20, 29, 40));
    textColor = ativo ? RGB(255, 255, 255) : RGB(197, 207, 218);
    accentColor = ativo ? RGB(74, 157, 230) : RGB(20, 29, 40);

    {
        HBRUSH bg = CreateSolidBrush(bgColor);
        FillRect(dis->hDC, &rc, bg);
        DeleteObject(bg);
    }

    if (ativo)
    {
        RECT accent = {rc.left, rc.top + 4, rc.left + 4, rc.bottom - 4};
        HBRUSH b = CreateSolidBrush(accentColor);
        FillRect(dis->hDC, &accent, b);
        DeleteObject(b);
    }

    GetWindowTextA(dis->hwndItem, texto, sizeof(texto));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    font = hFontUI ? hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    oldFont = (HFONT)SelectObject(dis->hDC, font);

    {
        RECT tr = rc;
        tr.left += 16;
        tr.right -= 10;
        DrawTextA(dis->hDC, texto, -1, &tr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(dis->hDC, oldFont);
}

static void ObterFaixaGrafico(GraphType type, int *start, int *count);
static void ResetGraphView(GraphType type);
static void ZoomGraph(GraphType type, int delta, int anchorX, int graphWidth);
static void ExportarGraficoPNG(HWND hwndGraph);
static BOOL ObterAreaMonitor(int monitorIndex, RECT *workArea);
static int ContarMonitores(void);
static void MoverOverlayParaMonitorSelecionado(void);
static void ToggleCompactMode(void);
static void AtualizarCompacto(void);
static LRESULT CALLBACK CompactProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void AplicarVisibilidadeModoNormal(void);
static void SessaoIniciar(void);
static void SessaoAdicionarAmostra(double cpu, double ram, double temp);
static void SessaoExportarRelatorio(void);
static void SessaoResetar(void);
static void SessaoAdicionarMarcador(const char *texto);
static void MostrarAjudaAtalhos(HWND hwndPai);

