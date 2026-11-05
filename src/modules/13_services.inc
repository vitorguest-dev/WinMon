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

    LVCOLUMNA column = {0};

    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    column.cx = 190;
    column.pszText = "Nome interno";
    ListView_InsertColumn(list, 0, &column);

    column.cx = 250;
    column.pszText = "Serviço";
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