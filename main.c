#include <windows.h>
#include <pdh.h>
#include <tlhelp32.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <stdio.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

#define TIMER_ID 1

HWND hEdit;
PDH_HQUERY hQuery;
PDH_HCOUNTER hCounterCPU;

DWORDLONG lastIn = 0;
DWORDLONG lastOut = 0;
int firstNetworkRead = 1;

void FormatarBytes(double bytes, char *buffer, size_t size)
{
    if (bytes >= 1073741824.0)
        snprintf(buffer, size, "%.2f GB", bytes / 1073741824.0);
    else if (bytes >= 1048576.0)
        snprintf(buffer, size, "%.2f MB", bytes / 1048576.0);
    else if (bytes >= 1024.0)
        snprintf(buffer, size, "%.2f KB", bytes / 1024.0);
    else
        snprintf(buffer, size, "%.0f B", bytes);
}

// 1. Monitorização de Discos
void MonitorarDiscos(char *buffer, size_t bufferSize)
{
    char temp[256];
    strcat_s(buffer, bufferSize, "=== [ DISCOS DE ARMAZENAMENTO ] ===\r\n");

    DWORD drives = GetLogicalDrives();
    char driveLetter[] = "A:\\";

    for (int i = 0; i < 26; i++)
    {
        if (drives & (1 << i))
        {
            driveLetter[0] = 'A' + i;
            UINT driveType = GetDriveTypeA(driveLetter);

            // Filtra apenas discos fixos (HDDs/SSDs)
            if (driveType == DRIVE_FIXED)
            {
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
                if (GetDiskFreeSpaceExA(driveLetter, &freeBytesAvailable, &totalBytes, &totalFreeBytes))
                {
                    double totalGB = (double)totalBytes.QuadPart / (1024 * 1024 * 1024);
                    double livreGB = (double)totalFreeBytes.QuadPart / (1024 * 1024 * 1024);
                    double usadaGB = totalGB - livreGB;
                    double percentUsado = (usadaGB / totalGB) * 100.0;

                    snprintf(temp, sizeof(temp), "Drive %s  Uso: %.1f%%  (%.1f GB usad. de %.1f GB)\r\n",
                             driveLetter, percentUsado, usadaGB, totalGB);
                    strcat_s(buffer, bufferSize, temp);
                }
            }
        }
    }
    strcat_s(buffer, bufferSize, "\r\n");
}

// 2. Processos + Consumo individual de RAM (via PSAPI)
void MonitorarProcessos(char *buffer, size_t bufferSize)
{
    char temp[256];
    strcat_s(buffer, bufferSize, "=== [ TOP PROCESSOS (USO DE MEMORIA) ] ===\r\n");

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe32 = {.dwSize = sizeof(PROCESSENTRY32)};
        if (Process32First(hProcessSnap, &pe32))
        {
            int count = 0;
            do
            {
                if (pe32.th32ProcessID != 0)
                {
                    // Abre o processo para consultar a memória do Working Set
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                    SIZE_T memUsageMB = 0;

                    if (hProcess)
                    {
                        PROCESS_MEMORY_COUNTERS pmc;
                        if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc)))
                        {
                            memUsageMB = pmc.WorkingSetSize / (1024 * 1024);
                        }
                        CloseHandle(hProcess);
                    }

                    snprintf(temp, sizeof(temp), "PID: %-6u | RAM: %4lu MB | %s\r\n",
                             pe32.th32ProcessID, (unsigned long)memUsageMB, pe32.szExeFile);
                    strcat_s(buffer, bufferSize, temp);
                    count++;
                }
            } while (Process32Next(hProcessSnap, &pe32) && count < 12);
        }
        CloseHandle(hProcessSnap);
    }
}

void AtualizarMonitor()
{
    char buffer[8192] = {0};
    char temp[256];

    // CPU
    PDH_FMT_COUNTERVALUE counterVal;
    PdhCollectQueryData(hQuery);
    PdhGetFormattedCounterValue(hCounterCPU, PDH_FMT_DOUBLE, NULL, &counterVal);
    snprintf(temp, sizeof(temp), "=== [ PROCESSADOR ] ===\r\nUso Atual da CPU: %.1f%%\r\n\r\n", counterVal.doubleValue);
    strcat_s(buffer, sizeof(buffer), temp);

    // RAM Geral
    MEMORYSTATUSEX memInfo = {.dwLength = sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memInfo))
    {
        DWORDLONG totalRAM = memInfo.ullTotalPhys / (1024 * 1024);
        DWORDLONG livreRAM = memInfo.ullAvailPhys / (1024 * 1024);
        snprintf(temp, sizeof(temp), "=== [ MEMORIA RAM GERAL ] ===\r\nUso: %ld%% | Usada: %llu MB | Livre: %llu MB\r\n\r\n",
                 memInfo.dwMemoryLoad, totalRAM - livreRAM, livreRAM);
        strcat_s(buffer, sizeof(buffer), temp);
    }

    // Discos
    MonitorarDiscos(buffer, sizeof(buffer));

    // Rede
    ULONG outBufLen = 0;
    GetIfTable(NULL, &outBufLen, FALSE);
    PMIB_IFTABLE pIfTable = (PMIB_IFTABLE)malloc(outBufLen);

    if (pIfTable && GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR)
    {
        DWORDLONG currentIn = 0, currentOut = 0;
        for (DWORD i = 0; i < pIfTable->dwNumEntries; i++)
        {
            currentIn += pIfTable->table[i].dwInOctets;
            currentOut += pIfTable->table[i].dwOutOctets;
        }

        if (!firstNetworkRead)
        {
            char downStr[32], upStr[32];
            FormatarBytes((double)(currentIn - lastIn), downStr, sizeof(downStr));
            FormatarBytes((double)(currentOut - lastOut), upStr, sizeof(upStr));
            snprintf(temp, sizeof(temp), "=== [ REDE EM TEMPO REAL ] ===\r\nDownload: %s/s | Upload: %s/s\r\n\r\n", downStr, upStr);
            strcat_s(buffer, sizeof(buffer), temp);
        }

        lastIn = currentIn;
        lastOut = currentOut;
        firstNetworkRead = 0;
        free(pIfTable);
    }

    // Processos e RAM Individual
    MonitorarProcessos(buffer, sizeof(buffer));

    SetWindowTextA(hEdit, buffer);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        // Aumentado a largura e altura da janela
        hEdit = CreateWindowEx(0, "EDIT", "A carregar dados do sistema...",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                               10, 10, 560, 680, hwnd, NULL, NULL, NULL);

        HFONT hFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                 FIXED_PITCH | FF_MODERN, "Consolas");
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        PdhOpenQuery(NULL, 0, &hQuery);
        PdhAddEnglishCounter(hQuery, "\\Processor(_Total)\\% Processor Time", 0, &hCounterCPU);
        PdhCollectQueryData(hQuery);

        SetTimer(hwnd, TIMER_ID, 1000, NULL);
        return 0;

    case WM_TIMER:
        AtualizarMonitor();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        PdhCloseQuery(hQuery);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const char CLASS_NAME[] = "HardwareMonitorClassV2";

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Monitor Completo de Hardware & Sistema (Win32)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 740,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
        return 0;

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}