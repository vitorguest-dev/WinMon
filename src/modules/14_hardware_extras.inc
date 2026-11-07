/* 14 hardware extras: battery and fan telemetry. */
#include <wbemidl.h>

static ULONGLONG g_fansLastRead = 0;
static char g_fansCache[4096] = "A aguardar leitura dos sensores...";

static void HardwareExtrasCriar(HWND parent)
{
    HINSTANCE hi = GetModuleHandleA(NULL);
    hEditEnergia = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "A recolher dados da bateria...",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, parent, NULL, hi, NULL);
    hEditVentoinhas = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "A consultar sensores WMI...",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 100, 100, parent, NULL, hi, NULL);
    if (hEditEnergia && hFontUI) SendMessageA(hEditEnergia, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    if (hEditVentoinhas && hFontUI) SendMessageA(hEditVentoinhas, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    ShowWindow(hEditEnergia, SW_HIDE);
    ShowWindow(hEditVentoinhas, SW_HIDE);
}

static void HardwareExtrasMostrar(int indice)
{
    if (hEditEnergia) ShowWindow(hEditEnergia, indice == 12 ? SW_SHOW : SW_HIDE);
    if (hEditVentoinhas) ShowWindow(hEditVentoinhas, indice == 13 ? SW_SHOW : SW_HIDE);
}

static void HardwareExtrasLayout(const RECT *rc)
{
    int x, y, w, h;
    if (!rc) return;
    x = rc->left; y = rc->top; w = rc->right - rc->left; h = rc->bottom - rc->top;
    if (hEditEnergia) MoveWindow(hEditEnergia, x, y, w, h, TRUE);
    if (hEditVentoinhas) MoveWindow(hEditVentoinhas, x, y, w, h, TRUE);
}

static void HardwareExtrasAtualizarBateria(void)
{
    SYSTEM_POWER_STATUS s;
    char b[2048];
    const char *fonte, *estado;
    if (!hEditEnergia) return;
    if (!GetSystemPowerStatus(&s))
    {
        SetWindowTextA(hEditEnergia, "O Windows nao conseguiu obter o estado da bateria.");
        return;
    }
    fonte = s.ACLineStatus == 1 ? "Alimentacao AC / carregador" :
            s.ACLineStatus == 0 ? "Bateria" : "Estado da alimentacao desconhecido";
    if (s.BatteryFlag == 128) estado = "Sem bateria";
    else if (s.BatteryFlag == 255) estado = "Estado da bateria desconhecido";
    else if (s.BatteryFlag & 8) estado = "A carregar";
    else if (s.BatteryFlag & 4) estado = "Carga critica";
    else if (s.BatteryFlag & 2) estado = "Carga baixa";
    else if (s.ACLineStatus == 1) estado = "Ligado a corrente";
    else if (s.ACLineStatus == 0) estado = "A descarregar / em bateria";
    else estado = "Estado de carga desconhecido";
    if (s.BatteryLifePercent == 255)
        snprintf(b, sizeof b, "MONITORIZACAO DA BATERIA\r\n\r\nEstado: %s\r\nFonte de energia: %s\r\nCarga: indisponivel\r\n",
                 estado, fonte);
    else
        snprintf(b, sizeof b, "MONITORIZACAO DA BATERIA\r\n\r\nCarga atual: %u%%\r\nEstado: %s\r\nFonte de energia: %s\r\n",
                 (unsigned)s.BatteryLifePercent, estado, fonte);
    if (s.BatteryLifeTime != (DWORD)-1)
    {
        DWORD mins = s.BatteryLifeTime / 60;
        size_t n = strlen(b);
        snprintf(b + n, sizeof(b) - n, "Autonomia estimada: %lu h %02lu min\r\n", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
    }
    else
    {
        strncat_s(b, sizeof b, "Autonomia estimada: indisponivel\r\n", _TRUNCATE);
    }
    if (s.BatteryFlag != 128 && s.BatteryLifePercent != 255)
        strncat_s(b, sizeof b, "\r\nNota: a API nativa reporta carga e autonomia; nao fornece uma medida fiavel de saude/capacidade maxima em todos os equipamentos.\r\n", _TRUNCATE);
    SetWindowTextA(hEditEnergia, b);
}

static void HardwareExtrasAtualizarVentoinhas(void)
{
    HRESULT hr;
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    IEnumWbemClassObject *en = NULL;
    BSTR ns = NULL, lang = NULL, query = NULL;
    char out[4096];
    int count = 0;
    ULONGLONG now = GetTickCount64();
    if (!hEditVentoinhas || (g_fansLastRead && now - g_fansLastRead < 15000)) return;
    g_fansLastRead = now;
    strcpy_s(out, sizeof out, "MONITORIZACAO DE VENTOINHAS\r\n\r\nWin32_Fan pode fornecer DesiredSpeed (velocidade desejada), nao a rotacao real.\r\n\r\n");
    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (void **)&loc);
    if (FAILED(hr) || !loc) goto unavailable;
    ns = SysAllocString(L"ROOT\\CIMV2");
    if (!ns) goto unavailable;
    hr = loc->lpVtbl->ConnectServer(loc, ns, NULL, NULL, NULL, 0, NULL, NULL, &svc);
    if (FAILED(hr) || !svc) goto unavailable;
    hr = CoSetProxyBlanket((IUnknown *)svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    if (FAILED(hr)) goto unavailable;
    lang = SysAllocString(L"WQL");
    query = SysAllocString(L"SELECT Name, DesiredSpeed FROM Win32_Fan");
    if (!lang || !query) goto unavailable;
    hr = svc->lpVtbl->ExecQuery(svc, lang, query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &en);
    if (FAILED(hr) || !en) goto unavailable;
    for (;;)
    {
        IWbemClassObject *obj = NULL;
        ULONG got = 0;
        VARIANT vName, vSpeed;
        char name[256] = "Ventoinha";
        char line[512];
        LONG rpm = -1;
        VariantInit(&vName); VariantInit(&vSpeed);
        hr = en->lpVtbl->Next(en, 1000, 1, &obj, &got);
        if (FAILED(hr) || got == 0) break;
        if (SUCCEEDED(obj->lpVtbl->Get(obj, L"Name", 0, &vName, NULL, NULL)) && vName.vt == VT_BSTR)
            WideCharToMultiByte(CP_ACP, 0, vName.bstrVal, -1, name, sizeof name, NULL, NULL);
        if (SUCCEEDED(obj->lpVtbl->Get(obj, L"DesiredSpeed", 0, &vSpeed, NULL, NULL)))
        {
            if (vSpeed.vt == VT_I4) rpm = vSpeed.lVal;
            else if (vSpeed.vt == VT_UI4) rpm = (LONG)vSpeed.ulVal;
        }
        snprintf(line, sizeof line, "%d. %s\r\n   Velocidade desejada: %s\r\n", count + 1, name,
                 rpm >= 0 ? "valor apresentado abaixo" : "nao disponibilizada pelo driver");
        if (rpm >= 0) snprintf(line, sizeof line, "%d. %s\r\n   Velocidade desejada: %ld RPM\r\n", count + 1, name, rpm);
        strncat_s(out, sizeof out, line, _TRUNCATE);
        count++;
        VariantClear(&vName); VariantClear(&vSpeed);
        obj->lpVtbl->Release(obj);
    }
    if (count == 0)
        strncat_s(out, sizeof out, "O fornecedor WMI nao publicou objetos Win32_Fan.\r\n\r\nIsto e comum em PCs portateis e em placas-mae cujo controlador nao expoe RPM via WMI. Nao significa necessariamente que nao existam ventoinhas.\r\n", _TRUNCATE);
    goto cleanup;
unavailable:
    strncat_s(out, sizeof out, "Nao foi possivel consultar o fornecedor WMI Win32_Fan neste equipamento.\r\n", _TRUNCATE);
cleanup:
    if (en) en->lpVtbl->Release(en);
    if (svc) svc->lpVtbl->Release(svc);
    if (loc) loc->lpVtbl->Release(loc);
    if (ns) SysFreeString(ns); if (lang) SysFreeString(lang); if (query) SysFreeString(query);
    strcpy_s(g_fansCache, sizeof g_fansCache, out);
    SetWindowTextA(hEditVentoinhas, g_fansCache);
}

static void HardwareExtrasAtualizar(void)
{
    if (abaAtual == 12) HardwareExtrasAtualizarBateria();
    else if (abaAtual == 13) HardwareExtrasAtualizarVentoinhas();
}
