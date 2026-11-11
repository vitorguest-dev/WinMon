/* 15 system info: aba "Sistema" (SO, BIOS, placa-mae, CPU, modulos de RAM e GPU).
 *
 * Os dados vem de WMI (ROOT\CIMV2). Como as consultas podem demorar, correm
 * numa thread propria (COM multithreaded) e o texto final chega a janela
 * principal por WM_SYSINFO_DONE: a interface nunca bloqueia.
 * A informacao e quase estatica, por isso so e relida quando a aba esta aberta
 * e a ultima leitura tem mais de 5 minutos. */

#define SI_BUF_TAMANHO 12288
#define SI_REFRESCO_MS 300000ULL

enum
{
    SI_TXT = 0, /* texto tal como vem do WMI */
    SI_DATA,    /* data CIM (yyyymmdd...) -> yyyy-mm-dd */
    SI_GB,      /* bytes -> GB */
    SI_GHZ,     /* MHz -> GHz */
    SI_MHZ,     /* MHz */
    SI_TIPO_RAM /* codigo SMBIOS do tipo de memoria -> nome */
};

typedef struct
{
    const wchar_t *prop;
    const char *rotulo;
    int tipo;
} SiCampo;

static volatile LONG g_siAExecutar = 0;
static ULONGLONG g_siUltimaLeitura = 0;

static void SiAnexar(char *out, size_t cap, const char *fmt, ...)
{
    size_t n = strlen(out);
    va_list ap;

    if (n + 1 >= cap)
        return;
    va_start(ap, fmt);
    vsnprintf(out + n, cap - n, fmt, ap);
    va_end(ap);
}

static void SiParaAnsi(const wchar_t *w, char *dst, size_t n)
{
    size_t len;

    dst[0] = '\0';
    if (!w || n == 0)
        return;
    if (WideCharToMultiByte(CP_ACP, 0, w, -1, dst, (int)n, NULL, NULL) == 0)
        dst[n - 1] = '\0';
    /* o WMI deixa espacos a direita (ex.: PartNumber) */
    len = strlen(dst);
    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t'))
        dst[--len] = '\0';
}

static const char *SiNomeTipoRam(int codigo)
{
    switch (codigo)
    {
    case 20: return "DDR";
    case 21: return "DDR2";
    case 24: return "DDR3";
    case 26: return "DDR4";
    case 27: return "LPDDR";
    case 28: return "LPDDR2";
    case 29: return "LPDDR3";
    case 30: return "LPDDR4";
    case 34: return "DDR5";
    case 35: return "LPDDR5";
    default: return NULL;
    }
}

/* Converte uma propriedade WMI em texto formatado. Devolve 0 se nao existir. */
static int SiValor(IWbemClassObject *obj, const SiCampo *c, char *dst, size_t n)
{
    VARIANT v, d;
    int ok = 0;

    dst[0] = '\0';
    VariantInit(&v);
    VariantInit(&d);

    if (FAILED(obj->lpVtbl->Get(obj, c->prop, 0, &v, NULL, NULL)) ||
        v.vt == VT_NULL || v.vt == VT_EMPTY)
    {
        VariantClear(&v);
        return 0;
    }

    switch (c->tipo)
    {
    case SI_DATA:
        if (v.vt == VT_BSTR)
        {
            char tmp[64];
            int i, digitos = 1;

            SiParaAnsi(v.bstrVal, tmp, sizeof(tmp));
            if (strlen(tmp) >= 8)
            {
                for (i = 0; i < 8; i++)
                    if (!isdigit((unsigned char)tmp[i]))
                        digitos = 0;
                if (digitos)
                {
                    snprintf(dst, n, "%.4s-%.2s-%.2s", tmp, tmp + 4, tmp + 6);
                    ok = 1;
                }
            }
        }
        break;

    case SI_GB:
    case SI_GHZ:
    case SI_MHZ:
    case SI_TIPO_RAM:
        if (SUCCEEDED(VariantChangeType(&d, &v, 0, VT_R8)))
        {
            double x = d.dblVal;

            if (c->tipo == SI_GB)
            {
                double gb = x / 1073741824.0;
                if (fabs(gb - floor(gb + 0.5)) < 0.05)
                    snprintf(dst, n, "%.0f GB", floor(gb + 0.5));
                else
                    snprintf(dst, n, "%.1f GB", gb);
                ok = 1;
            }
            else if (c->tipo == SI_GHZ)
            {
                snprintf(dst, n, "%.2f GHz", x / 1000.0);
                ok = 1;
            }
            else if (c->tipo == SI_MHZ)
            {
                snprintf(dst, n, "%.0f MHz", x);
                ok = 1;
            }
            else
            {
                const char *nome = SiNomeTipoRam((int)x);
                if (nome)
                {
                    snprintf(dst, n, "%s", nome);
                    ok = 1;
                }
            }
        }
        break;

    default: /* SI_TXT */
        if (v.vt == VT_BSTR)
            SiParaAnsi(v.bstrVal, dst, n);
        else if (SUCCEEDED(VariantChangeType(&d, &v, 0, VT_BSTR)) && d.vt == VT_BSTR)
            SiParaAnsi(d.bstrVal, dst, n);
        ok = dst[0] != '\0';
        break;
    }

    VariantClear(&d);
    VariantClear(&v);
    return ok;
}

/* Executa uma consulta WQL e acrescenta "Rotulo: valor" por cada campo.
 * enumerar = 1 numera os objetos (modulos de RAM, GPUs...). */
static void SiConsultar(IWbemServices *svc, const wchar_t *wql, const char *titulo,
                        const SiCampo *campos, int nc, int enumerar,
                        char *out, size_t cap)
{
    IEnumWbemClassObject *en = NULL;
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(wql);
    const char *ind = enumerar ? "    " : "  ";
    int idx = 0, i;

    SiAnexar(out, cap, "%s\r\n", titulo);

    if (lang && q &&
        SUCCEEDED(svc->lpVtbl->ExecQuery(svc, lang, q,
                                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                         NULL, &en)) &&
        en)
    {
        for (;;)
        {
            IWbemClassObject *obj = NULL;
            ULONG got = 0;

            if (FAILED(en->lpVtbl->Next(en, 5000, 1, &obj, &got)) || got == 0)
                break;
            idx++;
            if (enumerar)
                SiAnexar(out, cap, "  [%d]\r\n", idx);
            for (i = 0; i < nc; i++)
            {
                char v[256];
                if (SiValor(obj, &campos[i], v, sizeof(v)))
                    SiAnexar(out, cap, "%s%s: %s\r\n", ind, campos[i].rotulo, v);
            }
            obj->lpVtbl->Release(obj);
        }
    }

    if (idx == 0)
        SiAnexar(out, cap, "  (informacao indisponivel neste equipamento)\r\n");
    SiAnexar(out, cap, "\r\n");

    if (en)
        en->lpVtbl->Release(en);
    if (lang)
        SysFreeString(lang);
    if (q)
        SysFreeString(q);
}

static DWORD WINAPI SistemaInfoThread(LPVOID arg)
{
    static const SiCampo cSo[] = {
        {L"Caption", "Nome", SI_TXT},
        {L"Version", "Versao", SI_TXT},
        {L"BuildNumber", "Build", SI_TXT},
        {L"OSArchitecture", "Arquitetura", SI_TXT},
        {L"InstallDate", "Instalado em", SI_DATA}};
    static const SiCampo cPc[] = {
        {L"Manufacturer", "Fabricante", SI_TXT},
        {L"Model", "Modelo", SI_TXT},
        {L"TotalPhysicalMemory", "Memoria visivel ao Windows", SI_GB}};
    static const SiCampo cPlaca[] = {
        {L"Manufacturer", "Fabricante", SI_TXT},
        {L"Product", "Modelo", SI_TXT},
        {L"Version", "Revisao", SI_TXT}};
    static const SiCampo cBios[] = {
        {L"Manufacturer", "Fabricante", SI_TXT},
        {L"SMBIOSBIOSVersion", "Versao", SI_TXT},
        {L"ReleaseDate", "Data", SI_DATA}};
    static const SiCampo cCpu[] = {
        {L"Name", "Modelo", SI_TXT},
        {L"NumberOfCores", "Cores fisicos", SI_TXT},
        {L"NumberOfLogicalProcessors", "Threads", SI_TXT},
        {L"MaxClockSpeed", "Frequencia maxima", SI_GHZ}};
    static const SiCampo cRam[] = {
        {L"BankLabel", "Banco", SI_TXT},
        {L"Manufacturer", "Fabricante", SI_TXT},
        {L"PartNumber", "Referencia", SI_TXT},
        {L"Capacity", "Capacidade", SI_GB},
        {L"SMBIOSMemoryType", "Tipo", SI_TIPO_RAM},
        {L"Speed", "Velocidade nominal", SI_MHZ},
        {L"ConfiguredClockSpeed", "Velocidade configurada", SI_MHZ}};
    static const SiCampo cGpu[] = {
        {L"Name", "Modelo", SI_TXT},
        {L"DriverVersion", "Versao do driver", SI_TXT},
        {L"DriverDate", "Data do driver", SI_DATA}};

    HRESULT hrCo;
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    BSTR ns = NULL;
    SYSTEMTIME st;
    char *out;

    (void)arg;
    out = (char *)calloc(1, SI_BUF_TAMANHO);
    if (!out)
    {
        InterlockedExchange(&g_siAExecutar, 0);
        return 0;
    }

    GetLocalTime(&st);
    SiAnexar(out, SI_BUF_TAMANHO, "INFORMACAO DO SISTEMA  (lida as %02u:%02u)\r\n\r\n",
             (unsigned)st.wHour, (unsigned)st.wMinute);

    hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (SUCCEEDED(CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWbemLocator, (void **)&loc)) &&
        loc && (ns = SysAllocString(L"ROOT\\CIMV2")) != NULL &&
        SUCCEEDED(loc->lpVtbl->ConnectServer(loc, ns, NULL, NULL, NULL, 0, NULL, NULL, &svc)) &&
        svc &&
        SUCCEEDED(CoSetProxyBlanket((IUnknown *)svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                    RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                    NULL, EOAC_NONE)))
    {
        SiConsultar(svc, L"SELECT Caption, Version, BuildNumber, OSArchitecture, InstallDate FROM Win32_OperatingSystem",
                    "SISTEMA OPERATIVO", cSo, 5, 0, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT Manufacturer, Model, TotalPhysicalMemory FROM Win32_ComputerSystem",
                    "EQUIPAMENTO", cPc, 3, 0, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT Manufacturer, Product, Version FROM Win32_BaseBoard",
                    "PLACA-MAE", cPlaca, 3, 0, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT Manufacturer, SMBIOSBIOSVersion, ReleaseDate FROM Win32_BIOS",
                    "BIOS / UEFI", cBios, 3, 0, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed FROM Win32_Processor",
                    "PROCESSADOR", cCpu, 4, 1, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT BankLabel, Manufacturer, PartNumber, Capacity, SMBIOSMemoryType, Speed, ConfiguredClockSpeed FROM Win32_PhysicalMemory",
                    "MODULOS DE MEMORIA", cRam, 7, 1, out, SI_BUF_TAMANHO);
        SiConsultar(svc, L"SELECT Name, DriverVersion, DriverDate FROM Win32_VideoController",
                    "PLACA GRAFICA", cGpu, 3, 1, out, SI_BUF_TAMANHO);
    }
    else
    {
        SiAnexar(out, SI_BUF_TAMANHO,
                 "Nao foi possivel consultar o WMI neste equipamento.\r\n"
                 "Confirme que o servico \"Instrumentacao de Gestao do Windows\" esta ativo.\r\n");
    }

    if (svc)
        svc->lpVtbl->Release(svc);
    if (loc)
        loc->lpVtbl->Release(loc);
    if (ns)
        SysFreeString(ns);
    if (SUCCEEDED(hrCo))
        CoUninitialize();

    /* a janela principal liberta o texto em SistemaInfoMensagem */
    if (!hMainWindow || !PostMessageA(hMainWindow, WM_SYSINFO_DONE, 0, (LPARAM)out))
        free(out);
    InterlockedExchange(&g_siAExecutar, 0);
    return 0;
}

static void SistemaInfoCriar(HWND parent)
{
    hEditSistema = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "A consultar o WMI...",
                                   WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                   0, 0, 100, 100, parent, NULL, GetModuleHandleA(NULL), NULL);
    if (hEditSistema && hFontUI)
        SendMessageA(hEditSistema, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    if (hEditSistema)
        ShowWindow(hEditSistema, SW_HIDE);
}

static void SistemaInfoAtualizar(void)
{
    ULONGLONG agora;
    HANDLE th;

    if (abaAtual != 14 || !hEditSistema || g_siAExecutar)
        return;
    agora = GetTickCount64();
    if (g_siUltimaLeitura && agora - g_siUltimaLeitura < SI_REFRESCO_MS)
        return;
    if (InterlockedCompareExchange(&g_siAExecutar, 1, 0) != 0)
        return;

    g_siUltimaLeitura = agora; /* evita repetir de imediato se a leitura falhar */
    th = CreateThread(NULL, 0, SistemaInfoThread, NULL, 0, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&g_siAExecutar, 0);
}

static void SistemaInfoMostrar(int indice)
{
    if (hEditSistema)
        ShowWindow(hEditSistema, indice == 14 ? SW_SHOW : SW_HIDE);
    if (indice == 14)
        SistemaInfoAtualizar(); /* arranca ja, sem esperar pelo proximo tick */
}

static void SistemaInfoLayout(const RECT *rc)
{
    if (rc && hEditSistema)
        MoveWindow(hEditSistema, rc->left, rc->top,
                   rc->right - rc->left, rc->bottom - rc->top, TRUE);
}

static void SistemaInfoMensagem(LPARAM lParam)
{
    char *txt = (char *)lParam;

    if (!txt)
        return;
    if (hEditSistema)
        SetWindowTextA(hEditSistema, txt);
    g_siUltimaLeitura = GetTickCount64();
    free(txt);
}
