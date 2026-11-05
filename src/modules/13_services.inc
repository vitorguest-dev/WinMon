/*
 * WinMon — Windows Services Monitor
 *
 * Monitorização de serviços Windows através do SCM.
 * Compatível com a estrutura monolítica do WinMon.
 *
 * Utiliza as estruturas, IDs e handles já declarados
 * no ficheiro principal.
 */

#include <winsvc.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   ESTADO INTERNO
   ============================================================ */

static int g_servicesLoaded = 0;

static HWND hServicesSearchLabel = NULL;

/* ============================================================
   GESTÃO DA MEMÓRIA
   ============================================================ */

static void WM_ServicesFree(void)
{
    free(g_services);

    g_services = NULL;
    g_servicesCount = 0;
    g_servicesCapacity = 0;
}

/* ============================================================
   ADICIONAR UM SERVIÇO À COLEÇÃO
   ============================================================ */

static BOOL WM_ServiceAppend(
    const ENUM_SERVICE_STATUS_PROCESSA *entry)
{
    if (entry == NULL || g_servicesCount == MAXDWORD)
        return FALSE;

    if (g_servicesCount >= g_servicesCapacity)
    {
        SIZE_T newCapacity =
            (g_servicesCapacity == 0)
                ? 128
                : (SIZE_T)g_servicesCapacity * 2;

        if (newCapacity <= g_servicesCapacity ||
            newCapacity > MAXDWORD ||
            newCapacity > ((SIZE_T)-1) / sizeof(WinServiceInfo))
        {
            return FALSE;
        }

        WinServiceInfo *tmp = (WinServiceInfo *)realloc(
            g_services,
            newCapacity * sizeof(WinServiceInfo));

        if (tmp == NULL)
            return FALSE;

        g_services = tmp;
        g_servicesCapacity = (DWORD)newCapacity;
    }

    WinServiceInfo *dst = &g_services[g_servicesCount];

    memset(dst, 0, sizeof(*dst));

    if (entry->lpServiceName != NULL)
    {
        lstrcpynA(
            dst->serviceName,
            entry->lpServiceName,
            MAX_SERVICE_NAME);
    }

    if (entry->lpDisplayName != NULL)
    {
        lstrcpynA(
            dst->displayName,
            entry->lpDisplayName,
            MAX_SERVICE_DISPLAY);
    }

    dst->state = entry->ServiceStatusProcess.dwCurrentState;
    dst->processId = entry->ServiceStatusProcess.dwProcessId;
    dst->win32ExitCode = entry->ServiceStatusProcess.dwWin32ExitCode;
    dst->startType = MAXDWORD;

    g_servicesCount++;

    return TRUE;
}

/* ============================================================
   ENUMERAÇÃO DE SERVIÇOS
   ============================================================ */

static BOOL WM_EnumerateServices(void)
{
    SC_HANDLE scm = OpenSCManagerA(
        NULL,
        NULL,
        SC_MANAGER_ENUMERATE_SERVICE);

    if (scm == NULL)
        return FALSE;

    WM_ServicesFree();

    BYTE *buffer = NULL;
    DWORD bufferSize = 64 * 1024;
    DWORD resumeHandle = 0;
    BOOL success = FALSE;

    for (;;)
    {
        BYTE *tmp = (BYTE *)realloc(buffer, bufferSize);

        if (tmp == NULL)
            break;

        buffer = tmp;

        DWORD bytesNeeded = 0;
        DWORD servicesReturned = 0;

        BOOL result = EnumServicesStatusExA(
            scm,
            SC_ENUM_PROCESS_INFO,
            SERVICE_WIN32,
            SERVICE_STATE_ALL,
            buffer,
            bufferSize,
            &bytesNeeded,
            &servicesReturned,
            &resumeHandle,
            NULL);

        DWORD errorCode =
            result ? ERROR_SUCCESS : GetLastError();

        ENUM_SERVICE_STATUS_PROCESSA *entries =
            (ENUM_SERVICE_STATUS_PROCESSA *)buffer;

        for (DWORD i = 0; i < servicesReturned; i++)
        {
            if (!WM_ServiceAppend(&entries[i]))
                goto cleanup;
        }

        if (result)
        {
            success = TRUE;
            break;
        }

        if (errorCode != ERROR_MORE_DATA)
            break;

        if (bytesNeeded > bufferSize)
        {
            bufferSize = bytesNeeded;
        }
        else
        {
            if (bufferSize > MAXDWORD / 2)
                break;

            bufferSize *= 2;
        }
    }

cleanup:

    free(buffer);
    CloseServiceHandle(scm);

    if (!success)
        WM_ServicesFree();

    return success;
}

/* ============================================================
   TEXTO DO ESTADO DO SERVIÇO
   ============================================================ */

static const char *WM_ServiceStateText(DWORD state)
{
    switch (state)
    {
    case SERVICE_STOPPED:
        return "Parado";

    case SERVICE_START_PENDING:
        return "A iniciar";

    case SERVICE_STOP_PENDING:
        return "A parar";

    case SERVICE_RUNNING:
        return "Em execucao";

    case SERVICE_CONTINUE_PENDING:
        return "A retomar";

    case SERVICE_PAUSE_PENDING:
        return "A pausar";

    case SERVICE_PAUSED:
        return "Em pausa";

    default:
        return "Desconhecido";
    }
}

/* ============================================================
   TEXTO DO TIPO DE ARRANQUE
   ============================================================ */

static const char *WM_ServiceStartTypeText(DWORD type)
{
    switch (type)
    {
    case SERVICE_AUTO_START:
        return "Automatico";

    case SERVICE_DEMAND_START:
        return "Manual";

    case SERVICE_DISABLED:
        return "Desativado";

    case SERVICE_BOOT_START:
        return "Boot";

    case SERVICE_SYSTEM_START:
        return "Sistema";

    case MAXDWORD:
        return "Indisponivel";

    default:
        return "Desconhecido";
    }
}

/* ============================================================
   CONSULTAR CONFIGURAÇÃO DO SERVIÇO
   ============================================================ */

static BOOL WM_ServiceLoadDetails(WinServiceInfo *service)
{
    if (service == NULL || service->serviceName[0] == '\0')
        return FALSE;

    SC_HANDLE scm = OpenSCManagerA(
        NULL,
        NULL,
        SC_MANAGER_CONNECT);

    if (scm == NULL)
        return FALSE;

    SC_HANDLE hService = OpenServiceA(
        scm,
        service->serviceName,
        SERVICE_QUERY_CONFIG);

    if (hService == NULL)
    {
        CloseServiceHandle(scm);
        return FALSE;
    }

    BOOL success = FALSE;
    DWORD required = 0;

    BOOL result = QueryServiceConfigA(
        hService,
        NULL,
        0,
        &required);

    DWORD errorCode =
        result ? ERROR_SUCCESS : GetLastError();

    if (!result &&
        errorCode == ERROR_INSUFFICIENT_BUFFER &&
        required >= sizeof(QUERY_SERVICE_CONFIGA))
    {
        QUERY_SERVICE_CONFIGA *config =
            (QUERY_SERVICE_CONFIGA *)malloc(required);

        if (config != NULL)
        {
            if (QueryServiceConfigA(
                    hService,
                    config,
                    required,
                    &required))
            {
                service->startType = config->dwStartType;

                if (config->lpBinaryPathName != NULL)
                {
                    lstrcpynA(
                        service->binaryPath,
                        config->lpBinaryPathName,
                        MAX_SERVICE_PATH);
                }

                success = TRUE;
            }

            free(config);
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(scm);

    return success;
}

/* ============================================================
   CONSULTAR DESCRIÇÃO DO SERVIÇO
   ============================================================ */

static BOOL WM_ServiceLoadDescription(WinServiceInfo *service)
{
    if (service == NULL || service->serviceName[0] == '\0')
        return FALSE;

    SC_HANDLE scm = OpenSCManagerA(
        NULL,
        NULL,
        SC_MANAGER_CONNECT);

    if (scm == NULL)
        return FALSE;

    SC_HANDLE hService = OpenServiceA(
        scm,
        service->serviceName,
        SERVICE_QUERY_CONFIG);

    if (hService == NULL)
    {
        CloseServiceHandle(scm);
        return FALSE;
    }

    BOOL success = FALSE;
    DWORD required = 0;

    BOOL result = QueryServiceConfig2A(
        hService,
        SERVICE_CONFIG_DESCRIPTION,
        NULL,
        0,
        &required);

    DWORD errorCode =
        result ? ERROR_SUCCESS : GetLastError();

    if (!result &&
        errorCode == ERROR_INSUFFICIENT_BUFFER &&
        required >= sizeof(SERVICE_DESCRIPTIONA))
    {
        SERVICE_DESCRIPTIONA *desc =
            (SERVICE_DESCRIPTIONA *)malloc(required);

        if (desc != NULL)
        {
            if (QueryServiceConfig2A(
                    hService,
                    SERVICE_CONFIG_DESCRIPTION,
                    (LPBYTE)desc,
                    required,
                    &required))
            {
                if (desc->lpDescription != NULL)
                {
                    lstrcpynA(
                        service->description,
                        desc->lpDescription,
                        MAX_SERVICE_DESC);
                }
                else
                {
                    service->description[0] = '\0';
                }

                success = TRUE;
            }

            free(desc);
        }
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(scm);

    return success;
}

/* ============================================================
   PESQUISA PARCIAL SEM DEPENDÊNCIAS EXTERNAS
   ============================================================ */

static BOOL WM_ServiceContainsText(
    const char *text,
    const char *search)
{
    if (text == NULL || search == NULL)
        return FALSE;

    if (search[0] == '\0')
        return TRUE;

    size_t searchLength = strlen(search);
    size_t textLength = strlen(text);

    if (searchLength > textLength)
        return FALSE;

    for (size_t i = 0; i <= textLength - searchLength; i++)
    {
        if (_strnicmp(
                text + i,
                search,
                searchLength) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static LRESULT CALLBACK WM_ServicesListSubclass(
       HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

/* ============================================================
   CRIAR LISTVIEW
   ============================================================ */

static HWND WM_ServicesCreateListView(HWND parent)
{
    HWND list = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD |
        LVS_REPORT |
        LVS_SINGLESEL |
        LVS_SHOWSELALWAYS,
        0,
        0,
        900,
        500,
        parent,
        (HMENU)(INT_PTR)IDC_SERVICES_LIST,
        GetModuleHandleA(NULL),
        NULL);

    if (list == NULL)
        return NULL;

    ListView_SetExtendedListViewStyle(
        list,
        LVS_EX_FULLROWSELECT |
        LVS_EX_GRIDLINES |
        LVS_EX_DOUBLEBUFFER);

    SetWindowSubclass(list, WM_ServicesListSubclass, 1, 0);    

    LVCOLUMNA column = {0};

    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.cx = 190;
    column.pszText = "Nome interno";
    ListView_InsertColumn(list, 0, &column);

    column.cx = 250;
    column.pszText = "Servico";
    ListView_InsertColumn(list, 1, &column);

    column.cx = 130;
    column.pszText = "Estado";
    ListView_InsertColumn(list, 2, &column);

    column.cx = 90;
    column.pszText = "PID";
    ListView_InsertColumn(list, 3, &column);

    column.cx = 130;
    column.pszText = "Arranque";
    ListView_InsertColumn(list, 4, &column);

    return list;
}

/* ============================================================
   ATUALIZAR CONTADOR
   ============================================================ */

static void WM_ServicesUpdateCount(int visibleCount)
{
    if (hLabelServicosCount == NULL)
        return;

    char text[128];

    snprintf(
        text,
        sizeof(text),
        "%d de %lu servicos",
        visibleCount,
        (unsigned long)g_servicesCount);

    SetWindowTextA(hLabelServicosCount, text);
}

/* ============================================================
   APLICAR FILTRO E PREENCHER LISTVIEW
   ============================================================ */

static void WM_ServicesApplyFilter(void)
{
    if (hListViewServicos == NULL)
        return;

    char search[256] = {0};

    if (hEditPesquisaServicos != NULL)
    {
        GetWindowTextA(
            hEditPesquisaServicos,
            search,
            (int)sizeof(search));
    }

    ListView_DeleteAllItems(hListViewServicos);

    int visibleCount = 0;

    for (DWORD i = 0; i < g_servicesCount; i++)
    {
        WinServiceInfo *service = &g_services[i];

        if (!WM_ServiceContainsText(service->serviceName, search) &&
            !WM_ServiceContainsText(service->displayName, search))
        {
            continue;
        }

        char pidText[32];

        snprintf(
            pidText,
            sizeof(pidText),
            "%lu",
            (unsigned long)service->processId);

        LVITEMA item = {0};

        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = visibleCount;
        item.pszText = service->serviceName;

        /*
         * Guardamos o índice original da coleção.
         * Assim, a seleção continua correta mesmo com filtro.
         */
        item.lParam = (LPARAM)i;

        int index = ListView_InsertItem(
            hListViewServicos,
            &item);

        if (index < 0)
            continue;

        ListView_SetItemText(
            hListViewServicos,
            index,
            1,
            service->displayName);

        ListView_SetItemText(
            hListViewServicos,
            index,
            2,
            (LPSTR)WM_ServiceStateText(service->state));

        ListView_SetItemText(
            hListViewServicos,
            index,
            3,
            pidText);

        ListView_SetItemText(
            hListViewServicos,
            index,
            4,
            (LPSTR)WM_ServiceStartTypeText(service->startType));

        visibleCount++;
    }

    WM_ServicesUpdateCount(visibleCount);
}

/* ============================================================
   CRIAR PAINEL DE SERVIÇOS
   ============================================================ */

static void WM_ServicesCreatePanel(HWND hwndPai)
{
    if (hwndPai == NULL)
        return;

    hLabelServicosDetails = CreateWindowExA(
        0,
        "STATIC",
        "Servicos do Windows",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 400, 28,
        hwndPai,
        (HMENU)(INT_PTR)IDC_SERVICES_DETAILS,
        GetModuleHandleA(NULL),
        NULL);

    hServicesSearchLabel = CreateWindowExA(
        0,
        "STATIC",
        "Pesquisar:",
        WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 75, 24,
        hwndPai,
        NULL,
        GetModuleHandleA(NULL),
        NULL);

    hEditPesquisaServicos = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 200, 25,
        hwndPai,
        (HMENU)(INT_PTR)IDC_SERVICES_SEARCH,
        GetModuleHandleA(NULL),
        NULL);

    hBtnRefreshServicos = CreateWindowExA(
        0,
        "BUTTON",
        "Atualizar",
        WS_CHILD | BS_PUSHBUTTON,
        0, 0, 100, 27,
        hwndPai,
        (HMENU)(INT_PTR)IDC_SERVICES_REFRESH,
        GetModuleHandleA(NULL),
        NULL);

    hLabelServicosCount = CreateWindowExA(
        0,
        "STATIC",
        "0 servicos",
        WS_CHILD | SS_RIGHT | SS_CENTERIMAGE,
        0, 0, 160, 27,
        hwndPai,
        (HMENU)(INT_PTR)IDC_SERVICES_COUNT,
        GetModuleHandleA(NULL),
        NULL);

    hListViewServicos = WM_ServicesCreateListView(hwndPai);

    if (!hLabelServicosDetails ||
        !hServicesSearchLabel ||
        !hEditPesquisaServicos ||
        !hBtnRefreshServicos ||
        !hLabelServicosCount ||
        !hListViewServicos)
    {
        MessageBoxA(
            hwndPai,
            "Não foi possível criar todos os controlos da aba Serviços.",
            "WinMon",
            MB_OK | MB_ICONERROR);
    }
}

/* ============================================================
   MOSTRAR / ESCONDER ABA
   ============================================================ */

static void WM_ServicesShow(int visible)
{
    int show = visible ? SW_SHOW : SW_HIDE;

    if (hLabelServicosDetails)
        ShowWindow(hLabelServicosDetails, show);

    if (hServicesSearchLabel)
        ShowWindow(hServicesSearchLabel, show);

    if (hEditPesquisaServicos)
        ShowWindow(hEditPesquisaServicos, show);

    if (hBtnRefreshServicos)
        ShowWindow(hBtnRefreshServicos, show);

    if (hLabelServicosCount)
        ShowWindow(hLabelServicosCount, show);

    if (hListViewServicos)
        ShowWindow(hListViewServicos, show);

    if (visible && !g_servicesLoaded)
        WM_ServicesRefreshPanel();
}

/* ============================================================
   CRIAR PAINEL DE SERVIÇOS
   ============================================================ */

static void WM_ServicesLayout(const RECT *rc)
{
    if (rc == NULL)
        return;

    int x = rc->left + 8;
    int y = rc->top + 8;

    int width = rc->right - rc->left - 16;
    int height = rc->bottom - rc->top - 16;

    if (width < 100 || height < 100)
        return;

    const int topHeight = 34;
    const int detailsHeight = 28;

    /* Título e informação */
    if (hLabelServicosDetails)
    {
        MoveWindow(
            hLabelServicosDetails,
            x,
            y,
            width,
            detailsHeight,
            TRUE);
    }

    y += detailsHeight + 5;

    const int searchLabelWidth = 75;
    const int refreshWidth = 100;
    const int countWidth = 160;
    const int spacing = 8;

    int editWidth =
        width -
        searchLabelWidth -
        refreshWidth -
        countWidth -
        (spacing * 4);

    if (editWidth < 80)
        editWidth = 80;

    /* Etiqueta "Pesquisar:" */
    if (hServicesSearchLabel)
    {
        MoveWindow(
            hServicesSearchLabel,
            x,
            y,
            searchLabelWidth,
            topHeight,
            TRUE);
    }

    /* Caixa de pesquisa */
    if (hEditPesquisaServicos)
    {
        MoveWindow(
            hEditPesquisaServicos,
            x + searchLabelWidth + spacing,
            y + 2,
            editWidth,
            25,
            TRUE);
    }

    /* Botão Atualizar */
    if (hBtnRefreshServicos)
    {
        MoveWindow(
            hBtnRefreshServicos,
            x + searchLabelWidth + spacing + editWidth + spacing,
            y + 1,
            refreshWidth,
            27,
            TRUE);
    }

    /* Contador */
    if (hLabelServicosCount)
    {
        MoveWindow(
            hLabelServicosCount,
            x + width - countWidth,
            y + 1,
            countWidth,
            27,
            TRUE);
    }

    /* ListView */
    y += topHeight + 8;

    int listHeight = height - (y - rc->top);

    if (listHeight < 50)
        listHeight = 50;

    if (hListViewServicos)
    {
        MoveWindow(
            hListViewServicos,
            x,
            y,
            width,
            listHeight,
            TRUE);
    }
}

/* ============================================================
   ATUALIZAR PAINEL
   ============================================================ */

static BOOL WM_ServicesRefreshPanel(void)
{
    if (hListViewServicos == NULL)
        return FALSE;

    if (hLabelServicosDetails)
        SetWindowTextA(
            hLabelServicosDetails,
            "A consultar servicos do Windows...");

    if (!WM_EnumerateServices())
    {
        if (hLabelServicosDetails)
            SetWindowTextA(
                hLabelServicosDetails,
                "Não foi possível enumerar os servicos.");

        g_servicesLoaded = 0;
        WM_ServicesApplyFilter();

        return FALSE;
    }

    /*
     * Consultar configuração individual.
     * Uma falha num serviço não impede a apresentação dos outros.
     */
    for (DWORD i = 0; i < g_servicesCount; i++)
        WM_ServiceLoadDetails(&g_services[i]);

    g_servicesLoaded = 1;

    WM_ServicesApplyFilter();

    if (hLabelServicosDetails)
    {
        SetWindowTextA(
            hLabelServicosDetails,
            "Servicos do Windows, estado e configuracao de arranque");
    }

    return TRUE;
}

/* ============================================================
   OBTER SERVIÇO SELECIONADO
   ============================================================ */

static WinServiceInfo *WM_ServicesGetSelected(HWND list)
{
    if (list == NULL)
        return NULL;

    int selected = ListView_GetNextItem(
        list,
        -1,
        LVNI_SELECTED);

    if (selected < 0)
        return NULL;

    LVITEMA item = {0};

    item.mask = LVIF_PARAM;
    item.iItem = selected;

    if (!ListView_GetItem(list, &item))
        return NULL;

    DWORD index = (DWORD)item.lParam;

    if (index >= g_servicesCount)
        return NULL;

    return &g_services[index];
}

/* ============================================================
   OBTER SERVIÇO SELECIONADO NA ABA
   ============================================================ */

static WinServiceInfo *WM_ServicesGetSelectedVisible(void)
{
    return WM_ServicesGetSelected(hListViewServicos);
}


#define IDM_SVC_START 6200
#define IDM_SVC_STOP 6201
#define IDM_SVC_RESTART 6202
#define IDM_SVC_PAUSE 6203
#define IDM_SVC_RESUME 6204
#define IDM_SVC_BOOT_AUTO 6205
#define IDM_SVC_BOOT_MANUAL 6206
#define IDM_SVC_BOOT_OFF 6207
#define IDM_SVC_DETAILS 6208
#define IDM_SVC_COPY_NAME 6209
#define IDM_SVC_COPY_DISPLAY 6210
#define IDM_SVC_COPY_PATH 6211
#define IDM_SVC_OPEN_FOLDER 6212
#define IDM_SVC_KILL_PROC 6213
#define IDM_SVC_OPEN_MSC 6214
#define IDM_SVC_REFRESH 6215

/* ---------- utilitarios ---------- */

static void WM_SvcShowError(HWND owner, DWORD code)
{
    char sys[512] = {0};
    char msg[700];

    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, sys, (DWORD)sizeof(sys), NULL);

    snprintf(
        msg, sizeof(msg),
        "A operacao falhou (erro %lu).\n\n%s%s",
        (unsigned long)code,
        sys,
        (code == ERROR_ACCESS_DENIED)
            ? "\nExecute o WinMon como administrador."
            : "");

    MessageBoxA(owner, msg, "WinMon - Servicos", MB_OK | MB_ICONWARNING);
}

/* Acrescenta texto formatado ao fim de buf sem estourar o tamanho. */
static void WM_SvcAppend(char *buf, size_t size, const char *fmt, ...)
{
    size_t len = strlen(buf);
    va_list ap;

    if (len + 1 >= size)
        return;

    va_start(ap, fmt);
    vsnprintf(buf + len, size - len, fmt, ap);
    va_end(ap);
}

static void WM_SvcCopyText(HWND owner, const char *text)
{
    if (text == NULL || text[0] == '\0')
        return;

    size_t len = strlen(text) + 1;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);

    if (mem == NULL)
        return;

    char *dst = (char *)GlobalLock(mem);

    if (dst == NULL)
    {
        GlobalFree(mem);
        return;
    }

    memcpy(dst, text, len);
    GlobalUnlock(mem);

    if (!OpenClipboard(owner))
    {
        GlobalFree(mem);
        return;
    }

    EmptyClipboard();

    if (SetClipboardData(CF_TEXT, mem) == NULL)
        GlobalFree(mem);

    CloseClipboard();
}

/* Extrai o .exe de uma linha de comando de servico
 * ("C:\x\a.exe" -k grupo   ou   C:\x\a.exe -k grupo). */
static void WM_SvcExtractExePath(const char *cmd, char *out, size_t outSize)
{
    char tmp[MAX_SERVICE_PATH];

    if (out == NULL || outSize < 2)
        return;

    out[0] = '\0';

    if (cmd == NULL || cmd[0] == '\0')
        return;

    if (cmd[0] == '"')
    {
        const char *end = strchr(cmd + 1, '"');
        size_t len = end ? (size_t)(end - (cmd + 1)) : strlen(cmd + 1);

        if (len >= sizeof(tmp))
            len = sizeof(tmp) - 1;

        memcpy(tmp, cmd + 1, len);
        tmp[len] = '\0';
    }
    else
    {
        lstrcpynA(tmp, cmd, (int)sizeof(tmp));

        for (char *p = tmp; *p; p++)
        {
            if (_strnicmp(p, ".exe", 4) == 0)
            {
                p[4] = '\0';
                break;
            }
        }
    }

    DWORD n = ExpandEnvironmentStringsA(tmp, out, (DWORD)outSize);

    if (n == 0 || n > (DWORD)outSize)
        lstrcpynA(out, tmp, (int)outSize);
}

/* ---------- estado ao vivo ---------- */

/* Le o estado atual no SCM e atualiza a estrutura. */
static BOOL WM_SvcQueryLive(WinServiceInfo *svc, DWORD *accepted)
{
    BOOL ok = FALSE;

    if (accepted)
        *accepted = 0;

    if (svc == NULL)
        return FALSE;

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);

    if (scm == NULL)
        return FALSE;

    SC_HANDLE h = OpenServiceA(scm, svc->serviceName, SERVICE_QUERY_STATUS);

    if (h != NULL)
    {
        SERVICE_STATUS_PROCESS ssp;
        DWORD needed = 0;

        if (QueryServiceStatusEx(
                h, SC_STATUS_PROCESS_INFO,
                (LPBYTE)&ssp, sizeof(ssp), &needed))
        {
            svc->state = ssp.dwCurrentState;
            svc->processId = ssp.dwProcessId;
            svc->win32ExitCode = ssp.dwWin32ExitCode;

            if (accepted)
                *accepted = ssp.dwControlsAccepted;

            ok = TRUE;
        }

        CloseServiceHandle(h);
    }

    CloseServiceHandle(scm);

    return ok;
}

static BOOL WM_SvcWaitState(SC_HANDLE h, DWORD wanted, DWORD timeoutMs)
{
    SERVICE_STATUS_PROCESS ssp;
    DWORD needed = 0;
    DWORD start = GetTickCount();

    for (;;)
    {
        if (!QueryServiceStatusEx(
                h, SC_STATUS_PROCESS_INFO,
                (LPBYTE)&ssp, sizeof(ssp), &needed))
            return FALSE;

        if (ssp.dwCurrentState == wanted)
            return TRUE;

        if (GetTickCount() - start >= timeoutMs)
            return FALSE;

        Sleep(200);
    }
}

/* Repovoa a lista (respeitando o filtro) e volta a selecionar o servico. */
static void WM_SvcRepopulate(DWORD index)
{
    if (hListViewServicos == NULL)
        return;

    WM_ServicesApplyFilter();

    int n = ListView_GetItemCount(hListViewServicos);

    for (int i = 0; i < n; i++)
    {
        LVITEMA it = {0};

        it.mask = LVIF_PARAM;
        it.iItem = i;

        if (ListView_GetItem(hListViewServicos, &it) &&
            (DWORD)it.lParam == index)
        {
            ListView_SetItemState(
                hListViewServicos, i,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(hListViewServicos, i, FALSE);
            break;
        }
    }
}

/* ---------- acoes ---------- */

static void WM_SvcRunAction(HWND owner, WinServiceInfo *svc, UINT cmd)
{
    DWORD access = SERVICE_QUERY_STATUS;
    DWORD err = ERROR_SUCCESS;
    SERVICE_STATUS ss;
    DWORD index = (DWORD)(svc - g_services);

    switch (cmd)
    {
    case IDM_SVC_START:
        access |= SERVICE_START;
        break;
    case IDM_SVC_STOP:
        access |= SERVICE_STOP;
        break;
    case IDM_SVC_RESTART:
        access |= SERVICE_START | SERVICE_STOP;
        break;
    default:
        access |= SERVICE_PAUSE_CONTINUE;
        break;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);

    if (scm == NULL)
    {
        WM_SvcShowError(owner, GetLastError());
        return;
    }

    SC_HANDLE h = OpenServiceA(scm, svc->serviceName, access);

    if (h == NULL)
    {
        DWORD e = GetLastError();

        CloseServiceHandle(scm);
        WM_SvcShowError(owner, e);
        return;
    }

    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));

    switch (cmd)
    {
    case IDM_SVC_START:
        if (!StartServiceA(h, 0, NULL) &&
            GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
            err = GetLastError();
        else
            WM_SvcWaitState(h, SERVICE_RUNNING, 10000);
        break;

    case IDM_SVC_STOP:
        if (!ControlService(h, SERVICE_CONTROL_STOP, &ss))
            err = GetLastError();
        else
            WM_SvcWaitState(h, SERVICE_STOPPED, 10000);
        break;

    case IDM_SVC_RESTART:
        if (!ControlService(h, SERVICE_CONTROL_STOP, &ss))
            err = GetLastError();
        else if (!WM_SvcWaitState(h, SERVICE_STOPPED, 15000))
            err = ERROR_SERVICE_REQUEST_TIMEOUT;
        else if (!StartServiceA(h, 0, NULL))
            err = GetLastError();
        else
            WM_SvcWaitState(h, SERVICE_RUNNING, 10000);
        break;

    case IDM_SVC_PAUSE:
        if (!ControlService(h, SERVICE_CONTROL_PAUSE, &ss))
            err = GetLastError();
        else
            WM_SvcWaitState(h, SERVICE_PAUSED, 5000);
        break;

    case IDM_SVC_RESUME:
        if (!ControlService(h, SERVICE_CONTROL_CONTINUE, &ss))
            err = GetLastError();
        else
            WM_SvcWaitState(h, SERVICE_RUNNING, 5000);
        break;
    }

    SetCursor(oldCursor);

    CloseServiceHandle(h);
    CloseServiceHandle(scm);

    WM_SvcQueryLive(svc, NULL);
    WM_SvcRepopulate(index);

    if (err != ERROR_SUCCESS)
        WM_SvcShowError(owner, err);
}

static void WM_SvcSetStartType(HWND owner, WinServiceInfo *svc, DWORD type)
{
    DWORD index = (DWORD)(svc - g_services);

    if (type == SERVICE_DISABLED)
    {
        if (MessageBoxA(
                owner,
                "Desativar este servico impede-o de arrancar.\n\nContinuar?",
                "WinMon - Servicos",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);

    if (scm == NULL)
    {
        WM_SvcShowError(owner, GetLastError());
        return;
    }

    SC_HANDLE h = OpenServiceA(scm, svc->serviceName, SERVICE_CHANGE_CONFIG);

    if (h == NULL)
    {
        DWORD e = GetLastError();

        CloseServiceHandle(scm);
        WM_SvcShowError(owner, e);
        return;
    }

    BOOL ok = ChangeServiceConfigA(
        h,
        SERVICE_NO_CHANGE,
        type,
        SERVICE_NO_CHANGE,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL);

    DWORD e = ok ? ERROR_SUCCESS : GetLastError();

    CloseServiceHandle(h);
    CloseServiceHandle(scm);

    if (ok)
    {
        svc->startType = type;
        WM_SvcRepopulate(index);
    }
    else
    {
        WM_SvcShowError(owner, e);
    }
}

static void WM_SvcKillProcess(HWND owner, const WinServiceInfo *svc)
{
    if (svc->processId == 0)
        return;

    char msg[512];

    snprintf(
        msg, sizeof(msg),
        "Terminar o processo com PID %lu?\n\n"
        "Se for um svchost.exe partilhado, todos os servicos "
        "que correm nele serao terminados.",
        (unsigned long)svc->processId);

    if (MessageBoxA(
            owner, msg, "WinMon - Servicos",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;

    HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, svc->processId);

    if (hp == NULL)
    {
        WM_SvcShowError(owner, GetLastError());
        return;
    }

    if (!TerminateProcess(hp, 1))
        WM_SvcShowError(owner, GetLastError());

    CloseHandle(hp);
}

static void WM_SvcOpenFolder(HWND owner, const WinServiceInfo *svc)
{
    char exe[MAX_SERVICE_PATH];
    char args[MAX_SERVICE_PATH + 32];

    WM_SvcExtractExePath(svc->binaryPath, exe, sizeof(exe));

    if (exe[0] == '\0' || GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxA(
            owner, "Nao foi possivel localizar o ficheiro do servico.",
            "WinMon - Servicos", MB_OK | MB_ICONINFORMATION);
        return;
    }

    snprintf(args, sizeof(args), "/select,\"%s\"", exe);
    ShellExecuteA(owner, "open", "explorer.exe", args, NULL, SW_SHOWNORMAL);
}

/* ---------- janela de detalhes ---------- */

static void WM_SvcShowDetails(HWND owner, WinServiceInfo *svc)
{
    char text[4096];
    DWORD accepted = 0;

    text[0] = '\0';

    BOOL live = WM_SvcQueryLive(svc, &accepted);

    WM_ServiceLoadDescription(svc);

    WM_SvcAppend(text, sizeof(text),
                 "Nome interno: %s\nNome: %s\n",
                 svc->serviceName, svc->displayName);

    if (svc->description[0] != '\0')
        WM_SvcAppend(text, sizeof(text), "Descricao: %s\n", svc->description);

    WM_SvcAppend(text, sizeof(text),
                 "\nEstado: %s\n", WM_ServiceStateText(svc->state));

    if (svc->processId != 0)
        WM_SvcAppend(text, sizeof(text),
                     "PID: %lu\n", (unsigned long)svc->processId);

    if (svc->win32ExitCode != 0)
        WM_SvcAppend(text, sizeof(text),
                     "Ultimo codigo de saida: %lu\n",
                     (unsigned long)svc->win32ExitCode);

    if (live)
        WM_SvcAppend(text, sizeof(text), "Aceita: %s%s\n",
                     (accepted & SERVICE_ACCEPT_STOP) ? "parar " : "",
                     (accepted & SERVICE_ACCEPT_PAUSE_CONTINUE) ? "pausar/retomar" : "");

    /* configuracao */
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE h = scm ? OpenServiceA(scm, svc->serviceName,
                                     SERVICE_QUERY_CONFIG)
                      : NULL;

    if (h != NULL)
    {
        DWORD required = 0;

        QueryServiceConfigA(h, NULL, 0, &required);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            required >= sizeof(QUERY_SERVICE_CONFIGA))
        {
            QUERY_SERVICE_CONFIGA *cfg =
                (QUERY_SERVICE_CONFIGA *)malloc(required);

            if (cfg != NULL)
            {
                if (QueryServiceConfigA(h, cfg, required, &required))
                {
                    DWORD kind = cfg->dwServiceType &
                                 (SERVICE_WIN32_OWN_PROCESS |
                                  SERVICE_WIN32_SHARE_PROCESS);
                    const char *kindText =
                        (kind == SERVICE_WIN32_OWN_PROCESS)     ? "Processo proprio"
                        : (kind == SERVICE_WIN32_SHARE_PROCESS) ? "Processo partilhado"
                                                                : "Outro";

                    WM_SvcAppend(text, sizeof(text), "\nTipo: %s\n", kindText);

                    WM_SvcAppend(text, sizeof(text), "Arranque: %s",
                                 WM_ServiceStartTypeText(cfg->dwStartType));

                    if (cfg->dwStartType == SERVICE_AUTO_START)
                    {
                        DWORD need2 = 0;
                        SERVICE_DELAYED_AUTO_START_INFO di = {0};

                        if (QueryServiceConfig2A(
                                h, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                (LPBYTE)&di, sizeof(di), &need2) &&
                            di.fDelayedAutostart)
                        {
                            WM_SvcAppend(text, sizeof(text), " (atrasado)");
                        }
                    }

                    WM_SvcAppend(text, sizeof(text), "\n");

                    if (cfg->lpServiceStartName != NULL)
                        WM_SvcAppend(text, sizeof(text),
                                     "Conta: %s\n", cfg->lpServiceStartName);

                    if (cfg->lpBinaryPathName != NULL)
                        WM_SvcAppend(text, sizeof(text),
                                     "Executavel: %s\n", cfg->lpBinaryPathName);

                    if (cfg->lpLoadOrderGroup != NULL &&
                        cfg->lpLoadOrderGroup[0] != '\0')
                        WM_SvcAppend(text, sizeof(text),
                                     "Grupo: %s\n", cfg->lpLoadOrderGroup);

                    if (cfg->lpDependencies != NULL &&
                        cfg->lpDependencies[0] != '\0')
                    {
                        const char *p = cfg->lpDependencies;
                        int first = 1;

                        WM_SvcAppend(text, sizeof(text), "Dependencias: ");

                        while (*p)
                        {
                            WM_SvcAppend(text, sizeof(text), "%s%s",
                                         first ? "" : ", ",
                                         (*p == '+') ? p + 1 : p);
                            first = 0;
                            p += strlen(p) + 1;
                        }

                        WM_SvcAppend(text, sizeof(text), "\n");
                    }
                }

                free(cfg);
            }
        }

        CloseServiceHandle(h);
    }

    if (scm != NULL)
        CloseServiceHandle(scm);

    MessageBoxA(owner, text, "WinMon - Detalhes do servico",
                MB_OK | MB_ICONINFORMATION);
}

/* ---------- o menu ---------- */

static void WM_ServicesShowContextMenu(HWND list, POINT pt)
{
    WinServiceInfo *svc = WM_ServicesGetSelected(list);

    if (svc == NULL)
        return;

    HWND owner = GetParent(list);
    DWORD index = (DWORD)(svc - g_services);
    DWORD accepted = 0;
    DWORD oldState = svc->state;
    DWORD oldPid = svc->processId;

    /* estado atual (a lista pode estar desatualizada) */
    BOOL live = WM_SvcQueryLive(svc, &accepted);

    if (svc->state != oldState || svc->processId != oldPid)
        WM_SvcRepopulate(index);

    DWORD st = svc->state;
    BOOL canStart = live && st == SERVICE_STOPPED &&
                    svc->startType != SERVICE_DISABLED;
    BOOL canStop = live &&
                   (st == SERVICE_RUNNING || st == SERVICE_PAUSED) &&
                   (accepted & SERVICE_ACCEPT_STOP);
    BOOL canRestart = canStop && st == SERVICE_RUNNING;
    BOOL canPause = live && st == SERVICE_RUNNING &&
                    (accepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
    BOOL canResume = live && st == SERVICE_PAUSED &&
                     (accepted & SERVICE_ACCEPT_PAUSE_CONTINUE);

    HMENU menu = CreatePopupMenu();
    HMENU boot = CreatePopupMenu();

    if (menu == NULL || boot == NULL)
    {
        if (menu)
            DestroyMenu(menu);
        if (boot)
            DestroyMenu(boot);
        return;
    }

    AppendMenuA(boot, MF_STRING, IDM_SVC_BOOT_AUTO, "Automatico");
    AppendMenuA(boot, MF_STRING, IDM_SVC_BOOT_MANUAL, "Manual");
    AppendMenuA(boot, MF_STRING, IDM_SVC_BOOT_OFF, "Desativado");

    if (svc->startType == SERVICE_AUTO_START)
        CheckMenuRadioItem(boot, IDM_SVC_BOOT_AUTO, IDM_SVC_BOOT_OFF,
                           IDM_SVC_BOOT_AUTO, MF_BYCOMMAND);
    else if (svc->startType == SERVICE_DEMAND_START)
        CheckMenuRadioItem(boot, IDM_SVC_BOOT_AUTO, IDM_SVC_BOOT_OFF,
                           IDM_SVC_BOOT_MANUAL, MF_BYCOMMAND);
    else if (svc->startType == SERVICE_DISABLED)
        CheckMenuRadioItem(boot, IDM_SVC_BOOT_AUTO, IDM_SVC_BOOT_OFF,
                           IDM_SVC_BOOT_OFF, MF_BYCOMMAND);

    AppendMenuA(menu, MF_STRING | (canStart ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_START, "Iniciar");
    AppendMenuA(menu, MF_STRING | (canStop ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_STOP, "Parar");
    AppendMenuA(menu, MF_STRING | (canRestart ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_RESTART, "Reiniciar");
    AppendMenuA(menu, MF_STRING | (canPause ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_PAUSE, "Pausar");
    AppendMenuA(menu, MF_STRING | (canResume ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_RESUME, "Retomar");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)boot, "Tipo de arranque");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_SVC_DETAILS, "Detalhes...");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_SVC_COPY_NAME, "Copiar nome interno");
    AppendMenuA(menu, MF_STRING, IDM_SVC_COPY_DISPLAY, "Copiar nome do servico");
    AppendMenuA(menu, MF_STRING | (svc->binaryPath[0] ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_COPY_PATH, "Copiar caminho do executavel");
    AppendMenuA(menu, MF_STRING | (svc->binaryPath[0] ? MF_ENABLED : MF_GRAYED),
                IDM_SVC_OPEN_FOLDER, "Abrir localizacao do ficheiro");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);

    {
        char killText[64];

        snprintf(killText, sizeof(killText),
                 "Terminar processo (PID %lu)...",
                 (unsigned long)svc->processId);

        AppendMenuA(menu,
                    MF_STRING | (svc->processId != 0 ? MF_ENABLED : MF_GRAYED),
                    IDM_SVC_KILL_PROC, killText);
    }

    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_SVC_OPEN_MSC, "Abrir services.msc");
    AppendMenuA(menu, MF_STRING, IDM_SVC_REFRESH, "Atualizar lista");

    SetForegroundWindow(owner);

    UINT cmd = (UINT)TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, owner, NULL);

    PostMessageA(owner, WM_NULL, 0, 0);

    DestroyMenu(menu); /* destroi tambem o submenu */

    switch (cmd)
    {
    case IDM_SVC_START:
    case IDM_SVC_STOP:
    case IDM_SVC_RESTART:
    case IDM_SVC_PAUSE:
    case IDM_SVC_RESUME:
        WM_SvcRunAction(owner, svc, cmd);
        break;

    case IDM_SVC_BOOT_AUTO:
        WM_SvcSetStartType(owner, svc, SERVICE_AUTO_START);
        break;
    case IDM_SVC_BOOT_MANUAL:
        WM_SvcSetStartType(owner, svc, SERVICE_DEMAND_START);
        break;
    case IDM_SVC_BOOT_OFF:
        WM_SvcSetStartType(owner, svc, SERVICE_DISABLED);
        break;

    case IDM_SVC_DETAILS:
        WM_SvcShowDetails(owner, svc);
        break;

    case IDM_SVC_COPY_NAME:
        WM_SvcCopyText(owner, svc->serviceName);
        break;
    case IDM_SVC_COPY_DISPLAY:
        WM_SvcCopyText(owner, svc->displayName);
        break;
    case IDM_SVC_COPY_PATH:
        WM_SvcCopyText(owner, svc->binaryPath);
        break;

    case IDM_SVC_OPEN_FOLDER:
        WM_SvcOpenFolder(owner, svc);
        break;

    case IDM_SVC_KILL_PROC:
        WM_SvcKillProcess(owner, svc);
        WM_SvcQueryLive(svc, NULL);
        WM_SvcRepopulate(index);
        break;

    case IDM_SVC_OPEN_MSC:
        ShellExecuteA(owner, "open", "services.msc", NULL, NULL, SW_SHOWNORMAL);
        break;

    case IDM_SVC_REFRESH:
        WM_ServicesRefreshPanel(); /* svc fica invalido: nada a fazer depois */
        break;
    }
}

/* ---------- subclass do ListView (apanha o botao direito) ---------- */

static LRESULT CALLBACK WM_ServicesListSubclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR subId, DWORD_PTR refData)
{
    (void)refData;

    switch (msg)
    {
    case WM_CONTEXTMENU:
    {
        POINT pt;
        int item;

        if (GET_X_LPARAM(lParam) == -1 && GET_Y_LPARAM(lParam) == -1)
        {
            /* teclado (Shift+F10 / tecla de menu): usa o item selecionado */
            RECT r;

            item = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);

            if (item < 0 || !ListView_GetItemRect(hwnd, item, &r, LVIR_LABEL))
                return 0;

            pt.x = r.left + 8;
            pt.y = r.bottom;
            ClientToScreen(hwnd, &pt);
        }
        else
        {
            LVHITTESTINFO hit = {0};

            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);

            hit.pt = pt;
            ScreenToClient(hwnd, &hit.pt);

            item = ListView_HitTest(hwnd, &hit);

            if (item < 0)
                return 0;

            ListView_SetItemState(
                hwnd, item,
                LVIS_SELECTED | LVIS_FOCUSED,
                LVIS_SELECTED | LVIS_FOCUSED);
        }

        WM_ServicesShowContextMenu(hwnd, pt);
        return 0;
    }

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, WM_ServicesListSubclass, subId);
        break;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* ============================================================
   EVENTOS DOS CONTROLos
   ============================================================ */

static int WM_ServicesCommand(
    HWND hwnd,
    UINT id,
    UINT codigo)
{
    (void)hwnd;

    switch (id)
    {
    case IDC_SERVICES_REFRESH:

        if (codigo == BN_CLICKED)
        {
            WM_ServicesRefreshPanel();
            return 1;
        }

        break;

    case IDC_SERVICES_SEARCH:

        if (codigo == EN_CHANGE)
        {
            WM_ServicesApplyFilter();
            return 1;
        }

        break;
    }

    return 0;
}

/* ============================================================
   LIMPEZA
   ============================================================ */

static void WM_ServicesCleanup(void)
{
    WM_ServicesFree();

    g_servicesLoaded = 0;

    hPainelServicos = NULL;
    hListViewServicos = NULL;
    hEditPesquisaServicos = NULL;
    hLabelServicosCount = NULL;
    hLabelServicosDetails = NULL;
    hBtnRefreshServicos = NULL;
    hComboFiltroServicos = NULL;
    hServicesSearchLabel = NULL;
}