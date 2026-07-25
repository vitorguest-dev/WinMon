#include "globals.h"
#include "window_proc.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // DPI awareness: evita que a janela fique desfocada em ecrãs de alto DPI.
    // SetProcessDpiAwarenessContext é a API mais recente (Windows 10 1703+);
    // se não existir (versões antigas), cai-se em SetProcessDPIAware como fallback.
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiCtxFunc)(DPI_AWARENESS_CONTEXT);
        SetDpiCtxFunc pSetDpiCtx = (SetDpiCtxFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpiCtx) {
            pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }

    const char CLASS_NAME[] = "HardwareMonitorClassV6";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);

    RegisterClass(&wc);

    // WS_THICKFRAME e WS_MAXIMIZEBOX permitem redimensionar a janela
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, "Monitor de Hardware & Sistema (Win32) v6",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 616, 780,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    // Tabela de aceleradores: Ctrl+S para exportar o snapshot atual
    ACCEL accels[] = {
        { FVIRTKEY | FCONTROL, 'S', ID_EXPORT_SNAPSHOT }
    };
    HACCEL hAccel = CreateAcceleratorTable(accels, 1);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateAccelerator(hwnd, hAccel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}
