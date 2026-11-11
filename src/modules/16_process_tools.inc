/* 16 process tools: menu de contexto da aba Processos.
 *
 *  - submenu de prioridade (Baixa ... Alta) com a prioridade atual marcada;
 *  - dialogo de afinidade de CPU (uma caixa por CPU logico);
 *  - janela de detalhes: caminho, linha de comandos, processo pai,
 *    prioridade e afinidade;
 *  - copiar a linha de comandos.
 *
 * Privilegios minimos: so se pede PROCESS_QUERY_LIMITED_INFORMATION para ler
 * e PROCESS_SET_INFORMATION para alterar. A linha de comandos vem de
 * NtQueryInformationProcess(ProcessCommandLineInformation), que dispensa
 * PROCESS_VM_READ (Windows 8.1 ou superior). */

#define PT_PRIO_N 5
static const struct
{
    DWORD classe;
    const char *nome;
} k_ptPrio[PT_PRIO_N] = {
    {IDLE_PRIORITY_CLASS, "Baixa"},
    {BELOW_NORMAL_PRIORITY_CLASS, "Abaixo do normal"},
    {NORMAL_PRIORITY_CLASS, "Normal"},
    {ABOVE_NORMAL_PRIORITY_CLASS, "Acima do normal"},
    {HIGH_PRIORITY_CLASS, "Alta"}};

#define PT_BITS ((int)(sizeof(DWORD_PTR) * 8))
#define PT_BIT(i) (((DWORD_PTR)1) << (i))

static void PtAnexar(char *out, size_t cap, const char *fmt, ...)
{
    size_t n = strlen(out);
    va_list ap;

    if (n + 1 >= cap)
        return;
    va_start(ap, fmt);
    vsnprintf(out + n, cap - n, fmt, ap);
    va_end(ap);
}

static void PtFonte(HWND h)
{
    if (h && hFontUI)
        SendMessageA(h, WM_SETFONT, (WPARAM)hFontUI, TRUE);
}

static int PtRegistarClasse(const char *nome, WNDPROC proc)
{
    WNDCLASSEXA wc;
    HINSTANCE hi = GetModuleHandleA(NULL);

    if (GetClassInfoExA(hi, nome, &wc))
        return 1;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = nome;
    return RegisterClassExA(&wc) != 0;
}

/* Loop modal: desativa a janela principal enquanto o dialogo esta aberto.
 * O timer da janela principal continua a ser despachado aqui dentro. */
static void PtLoopModal(HWND dlg, HWND owner, volatile int *fim)
{
    MSG msg;

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    while (!*fim)
    {
        BOOL r = GetMessageA(&msg, NULL, 0, 0);
        if (r == 0)
        {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        if (r < 0)
            break;
        if (!IsDialogMessageA(dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(dlg);
    SetForegroundWindow(owner);
}

static void PtCentrar(HWND owner, int w, int h, int *x, int *y)
{
    RECT ro;

    if (owner && GetWindowRect(owner, &ro))
    {
        *x = ro.left + ((ro.right - ro.left) - w) / 2;
        *y = ro.top + ((ro.bottom - ro.top) - h) / 2;
    }
    else
    {
        *x = CW_USEDEFAULT;
        *y = CW_USEDEFAULT;
    }
}

/* ------------------------------------------------------------------------- */
/* Prioridade                                                                */
/* ------------------------------------------------------------------------- */

static int ProcDefinirPrioridade(DWORD pid, DWORD classe)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    int ok = 0;

    if (h)
    {
        ok = SetPriorityClass(h, classe) != 0;
        CloseHandle(h);
    }
    return ok;
}

/* Classe de prioridade do item i do submenu (0..4); usado pelo menu de grupos. */
static DWORD ProcClasseDoIndice(int i)
{
    return (i >= 0 && i < PT_PRIO_N) ? k_ptPrio[i].classe : NORMAL_PRIORITY_CLASS;
}

static DWORD ProcPrioridadeAtual(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD c = 0;

    if (h)
    {
        c = GetPriorityClass(h);
        CloseHandle(h);
    }
    return c;
}

/* Acrescenta as 5 classes ao submenu; os ids sao idBase .. idBase+4.
 * "Tempo real" fica de fora de proposito: pode bloquear o sistema. */
static void ProcAnexarMenuPrioridade(HMENU sub, DWORD atual, UINT idBase)
{
    int i, marcado = -1;

    for (i = 0; i < PT_PRIO_N; i++)
    {
        AppendMenuA(sub, MF_STRING, idBase + (UINT)i, k_ptPrio[i].nome);
        if (atual == k_ptPrio[i].classe)
            marcado = i;
    }
    if (marcado >= 0)
        CheckMenuRadioItem(sub, idBase, idBase + PT_PRIO_N - 1,
                           idBase + (UINT)marcado, MF_BYCOMMAND);
}

static const char *PtNomePrioridade(DWORD c)
{
    int i;

    for (i = 0; i < PT_PRIO_N; i++)
        if (k_ptPrio[i].classe == c)
            return k_ptPrio[i].nome;
    if (c == REALTIME_PRIORITY_CLASS)
        return "Tempo real";
    return "indisponivel";
}

/* ------------------------------------------------------------------------- */
/* Afinidade de CPU                                                          */
/* ------------------------------------------------------------------------- */

#define PT_AFF_CLASSE "WinMonAfinidadeDlg"
#define IDC_AFF_CHK_BASE 7100
#define IDC_AFF_TODOS 7170

typedef struct
{
    HWND chk[64];
    int cpu[64]; /* indice do CPU logico de cada caixa */
    int n;
    DWORD_PTR novaMask;
    int aplicar;
    volatile int fim;
} PtAfinidade;

static PtAfinidade g_ptAff;

static void PtFormatarMascara(DWORD_PTR mask, DWORD_PTR sys, char *out, size_t n)
{
    char tmp[256];
    size_t pos = 0;
    int i, total = 0, sel = 0, ini = -1;

    tmp[0] = '\0';
    for (i = 0; i < PT_BITS; i++)
        if (sys & PT_BIT(i))
            total++;

    for (i = 0; i <= PT_BITS; i++)
    {
        int ligado = (i < PT_BITS) && ((mask & sys & PT_BIT(i)) != 0);

        if (ligado)
        {
            sel++;
            if (ini < 0)
                ini = i;
        }
        else if (ini >= 0)
        {
            if (pos > 0 && pos < sizeof(tmp))
                pos += (size_t)snprintf(tmp + pos, sizeof(tmp) - pos, ",");
            if (pos < sizeof(tmp))
            {
                if (i - 1 == ini)
                    pos += (size_t)snprintf(tmp + pos, sizeof(tmp) - pos, "%d", ini);
                else
                    pos += (size_t)snprintf(tmp + pos, sizeof(tmp) - pos, "%d-%d", ini, i - 1);
            }
            ini = -1;
        }
    }

    if (sel == total)
        snprintf(out, n, "Todos os CPUs (%d)", total);
    else
        snprintf(out, n, "CPUs %s (%d de %d)", tmp, sel, total);
}

static LRESULT CALLBACK PtAfinidadeProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_COMMAND:
        if (LOWORD(w) == IDC_AFF_TODOS && HIWORD(w) == BN_CLICKED)
        {
            int i;
            for (i = 0; i < g_ptAff.n; i++)
                Button_SetCheck(g_ptAff.chk[i], BST_CHECKED);
            return 0;
        }
        if (LOWORD(w) == IDOK)
        {
            DWORD_PTR mask = 0;
            int i;

            for (i = 0; i < g_ptAff.n; i++)
                if (Button_GetCheck(g_ptAff.chk[i]) == BST_CHECKED)
                    mask |= PT_BIT(g_ptAff.cpu[i]);
            if (!mask)
            {
                MessageBoxA(h, "Selecione pelo menos um CPU.", "WinMon",
                            MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            g_ptAff.novaMask = mask;
            g_ptAff.aplicar = 1;
            g_ptAff.fim = 1;
            return 0;
        }
        if (LOWORD(w) == IDCANCEL)
        {
            g_ptAff.fim = 1;
            return 0;
        }
        break;
    case WM_CLOSE:
        g_ptAff.fim = 1;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void ProcDialogoAfinidade(HWND owner, DWORD pid, const char *nome)
{
    const int cols = 4, cw = 84, margem = 12;
    HINSTANCE hi = GetModuleHandleA(NULL);
    DWORD estilo = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exEstilo = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    DWORD_PTR procMask = 0, sysMask = 0;
    HANDLE h;
    HWND dlg, lbl;
    RECT rc;
    char tit[320], txt[32];
    int i, linhas, wCli, hCli, x, y, yBtn;

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h || !GetProcessAffinityMask(h, &procMask, &sysMask) || !sysMask)
    {
        if (h)
            CloseHandle(h);
        MessageBoxA(owner,
                    "Nao foi possivel ler a afinidade deste processo "
                    "(processo protegido ou ja terminado).",
                    "WinMon", MB_OK | MB_ICONWARNING);
        return;
    }
    CloseHandle(h);

    if (!PtRegistarClasse(PT_AFF_CLASSE, PtAfinidadeProc))
        return;

    ZeroMemory(&g_ptAff, sizeof(g_ptAff));
    for (i = 0; i < PT_BITS && g_ptAff.n < 64; i++)
        if (sysMask & PT_BIT(i))
            g_ptAff.cpu[g_ptAff.n++] = i;

    linhas = (g_ptAff.n + cols - 1) / cols;
    wCli = cols * cw + 2 * margem;
    yBtn = 36 + linhas * 24 + 10;
    hCli = yBtn + 26 + margem;

    rc.left = 0;
    rc.top = 0;
    rc.right = wCli;
    rc.bottom = hCli;
    AdjustWindowRectEx(&rc, estilo, FALSE, exEstilo);
    PtCentrar(owner, rc.right - rc.left, rc.bottom - rc.top, &x, &y);

    snprintf(tit, sizeof(tit), "Afinidade de CPU - %s (PID %lu)", nome, (unsigned long)pid);
    dlg = CreateWindowExA(exEstilo, PT_AFF_CLASSE, tit, estilo, x, y,
                          rc.right - rc.left, rc.bottom - rc.top,
                          owner, NULL, hi, NULL);
    if (!dlg)
        return;

    lbl = CreateWindowExA(0, "STATIC", "CPUs onde o processo pode executar:",
                          WS_CHILD | WS_VISIBLE | SS_LEFT,
                          margem, 10, wCli - 2 * margem, 20, dlg, NULL, hi, NULL);
    PtFonte(lbl);

    for (i = 0; i < g_ptAff.n; i++)
    {
        snprintf(txt, sizeof(txt), "CPU %d", g_ptAff.cpu[i]);
        g_ptAff.chk[i] = CreateWindowExA(
            0, "BUTTON", txt,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            margem + (i % cols) * cw, 36 + (i / cols) * 24, cw - 4, 22,
            dlg, (HMENU)(UINT_PTR)(IDC_AFF_CHK_BASE + i), hi, NULL);
        PtFonte(g_ptAff.chk[i]);
        if (procMask & PT_BIT(g_ptAff.cpu[i]))
            Button_SetCheck(g_ptAff.chk[i], BST_CHECKED);
    }

    PtFonte(CreateWindowExA(0, "BUTTON", "Todos",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            margem, yBtn, 72, 26, dlg, (HMENU)(UINT_PTR)IDC_AFF_TODOS, hi, NULL));
    PtFonte(CreateWindowExA(0, "BUTTON", "OK",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            wCli - margem - 160, yBtn, 76, 26, dlg, (HMENU)(UINT_PTR)IDOK, hi, NULL));
    PtFonte(CreateWindowExA(0, "BUTTON", "Cancelar",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            wCli - margem - 80, yBtn, 80, 26, dlg, (HMENU)(UINT_PTR)IDCANCEL, hi, NULL));

    PtLoopModal(dlg, owner, &g_ptAff.fim);

    if (g_ptAff.aplicar)
    {
        h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!h || !SetProcessAffinityMask(h, g_ptAff.novaMask))
            MessageBoxA(owner,
                        "O Windows recusou a alteracao da afinidade "
                        "(processo protegido ou de outro utilizador).",
                        "WinMon", MB_OK | MB_ICONWARNING);
        if (h)
            CloseHandle(h);
    }
}

/* ------------------------------------------------------------------------- */
/* Caminho, linha de comandos e detalhes                                     */
/* ------------------------------------------------------------------------- */

typedef struct
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} PtUnicodeString;
typedef LONG(NTAPI *PtNtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

#define PT_PROCESS_COMMAND_LINE_INFORMATION 60

/* Devolve 1 e preenche out se conseguir ler a linha de comandos. */
static int ProcLinhaComandos(DWORD pid, char *out, size_t n)
{
    HMODULE nt;
    PtNtQueryInformationProcess fn;
    HANDLE h;
    ULONG tam = 0;
    LONG st;
    BYTE *buf = NULL;
    int ok = 0, t;

    if (n == 0)
        return 0;
    out[0] = '\0';
    nt = GetModuleHandleW(L"ntdll.dll");
    fn = nt ? (PtNtQueryInformationProcess)(void *)GetProcAddress(nt, "NtQueryInformationProcess") : NULL;
    if (!fn)
        return 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return 0;

    /* 1.a chamada: devolve normalmente STATUS_INFO_LENGTH_MISMATCH + tamanho */
    fn(h, PT_PROCESS_COMMAND_LINE_INFORMATION, NULL, 0, &tam);
    for (t = 0; t < 3 && tam > 0 && tam < (1u << 20); t++)
    {
        ULONG cap = tam + 64;

        buf = (BYTE *)malloc(cap);
        if (!buf)
            break;
        st = fn(h, PT_PROCESS_COMMAND_LINE_INFORMATION, buf, cap, &tam);
        if (st >= 0)
        {
            ok = 1;
            break;
        }
        free(buf);
        buf = NULL;
    }
    CloseHandle(h);

    if (ok)
    {
        const PtUnicodeString *us = (const PtUnicodeString *)buf;
        int len = us->Length / 2, truncada = 0, r;

        if (!us->Buffer || len <= 0)
        {
            ok = 0;
        }
        else
        {
            if (len > (int)(n - 1) / 2)
            {
                len = (int)(n - 1) / 2;
                truncada = 1;
            }
            r = WideCharToMultiByte(CP_ACP, 0, us->Buffer, len, out, (int)n - 1, NULL, NULL);
            out[r > 0 ? r : 0] = '\0';
            if (truncada && strlen(out) + 4 < n)
                strcat_s(out, n, "...");
            ok = out[0] != '\0';
        }
    }
    free(buf);
    return ok;
}

static int ProcCaminho(DWORD pid, char *out, size_t n)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD tam = (DWORD)n;
    int ok = 0;

    if (n == 0)
        return 0;
    out[0] = '\0';
    if (h)
    {
        ok = QueryFullProcessImageNameA(h, 0, out, &tam) != 0;
        CloseHandle(h);
    }
    return ok;
}

/* Nome do processo pai; ignora um PID reutilizado (pai criado depois do filho). */
static void PtDescreverPai(const ProcessoInfo *p, char *out, size_t n)
{
    int i;

    if (p->ppid == 0)
    {
        snprintf(out, n, "-");
        return;
    }
    for (i = 0; i < totalListaProcessos; i++)
    {
        const ProcessoInfo *q = &listaProcessos[i];

        if (q->pid == p->ppid && !(q->criacao && p->criacao && q->criacao > p->criacao))
        {
            snprintf(out, n, "%s (PID %lu)", q->exeFile, (unsigned long)q->pid);
            return;
        }
    }
    snprintf(out, n, "(ja terminou) PID %lu", (unsigned long)p->ppid);
}

static void PtConstruirDetalhes(const ProcessoInfo *p, char *out, size_t cap)
{
    char caminho[MAX_PATH], linha[8192], pai[320], afin[320];
    DWORD_PTR procMask = 0, sysMask = 0;
    HANDLE h;

    out[0] = '\0';
    PtAnexar(out, cap, "DETALHES DO PROCESSO\r\n\r\n");
    PtAnexar(out, cap, "Nome: %s\r\n", p->exeFile);
    PtAnexar(out, cap, "PID: %lu\r\n", (unsigned long)p->pid);

    PtDescreverPai(p, pai, sizeof(pai));
    PtAnexar(out, cap, "Processo pai: %s\r\n", pai);

    PtAnexar(out, cap, "Prioridade: %s\r\n", PtNomePrioridade(ProcPrioridadeAtual(p->pid)));

    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p->pid);
    if (h && GetProcessAffinityMask(h, &procMask, &sysMask) && sysMask)
    {
        PtFormatarMascara(procMask, sysMask, afin, sizeof(afin));
        PtAnexar(out, cap, "Afinidade: %s\r\n", afin);
    }
    else
        PtAnexar(out, cap, "Afinidade: indisponivel\r\n");
    if (h)
        CloseHandle(h);

    PtAnexar(out, cap, "CPU: %.1f%%   RAM: %lu MB\r\n\r\n",
             p->cpuPercent, (unsigned long)p->memUsageMB);

    if (ProcCaminho(p->pid, caminho, sizeof(caminho)))
        PtAnexar(out, cap, "Caminho:\r\n%s\r\n\r\n", caminho);
    else
        PtAnexar(out, cap, "Caminho:\r\n(indisponivel - processo protegido ou terminado)\r\n\r\n");

    if (ProcLinhaComandos(p->pid, linha, sizeof(linha)))
        PtAnexar(out, cap, "Linha de comandos:\r\n%s\r\n", linha);
    else
        PtAnexar(out, cap, "Linha de comandos:\r\n(indisponivel - processo protegido ou terminado)\r\n");
}

#define PT_TXT_CLASSE "WinMonTextoDlg"
#define IDC_TXT_EDIT 7200
#define IDC_TXT_COPIAR 7201

static HWND g_ptTxtEdit = NULL;
static char *g_ptTxt = NULL;
static volatile int g_ptTxtFim = 0;

static LRESULT CALLBACK PtTextoProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_SIZE:
    {
        int cw = LOWORD(l), ch = HIWORD(l);
        HWND bCop = GetDlgItem(h, IDC_TXT_COPIAR);
        HWND bFechar = GetDlgItem(h, IDCANCEL);

        if (g_ptTxtEdit)
            MoveWindow(g_ptTxtEdit, 8, 8, cw - 16, ch - 52, TRUE);
        if (bCop)
            MoveWindow(bCop, cw - 176, ch - 36, 80, 26, TRUE);
        if (bFechar)
            MoveWindow(bFechar, cw - 88, ch - 36, 80, 26, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)l;
        mmi->ptMinTrackSize.x = 380;
        mmi->ptMinTrackSize.y = 240;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDC_TXT_COPIAR)
        {
            if (g_ptTxt)
                CopiarTextoClipboard(h, g_ptTxt);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL || LOWORD(w) == IDOK)
        {
            g_ptTxtFim = 1;
            return 0;
        }
        break;
    case WM_CLOSE:
        g_ptTxtFim = 1;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

/* Janela modal com texto selecionavel e botao "Copiar". */
static void PtMostrarTexto(HWND owner, const char *titulo, const char *texto)
{
    HINSTANCE hi = GetModuleHandleA(NULL);
    DWORD estilo = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    HWND dlg, bCop, bFechar;
    RECT rc;
    int x, y;

    if (!PtRegistarClasse(PT_TXT_CLASSE, PtTextoProc))
        return;

    free(g_ptTxt);
    g_ptTxt = _strdup(texto);
    g_ptTxtFim = 0;
    g_ptTxtEdit = NULL;

    rc.left = 0;
    rc.top = 0;
    rc.right = 680;
    rc.bottom = 380;
    AdjustWindowRectEx(&rc, estilo, FALSE, WS_EX_CONTROLPARENT);
    PtCentrar(owner, rc.right - rc.left, rc.bottom - rc.top, &x, &y);

    dlg = CreateWindowExA(WS_EX_CONTROLPARENT, PT_TXT_CLASSE, titulo, estilo, x, y,
                          rc.right - rc.left, rc.bottom - rc.top, owner, NULL, hi, NULL);
    if (!dlg)
        return;

    g_ptTxtEdit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", texto,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        8, 8, 660, 328, dlg, (HMENU)(UINT_PTR)IDC_TXT_EDIT, hi, NULL);
    bCop = CreateWindowExA(0, "BUTTON", "Copiar",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                           504, 344, 80, 26, dlg, (HMENU)(UINT_PTR)IDC_TXT_COPIAR, hi, NULL);
    bFechar = CreateWindowExA(0, "BUTTON", "Fechar",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                              592, 344, 80, 26, dlg, (HMENU)(UINT_PTR)IDCANCEL, hi, NULL);
    PtFonte(g_ptTxtEdit);
    PtFonte(bCop);
    PtFonte(bFechar);
    SendMessageA(g_ptTxtEdit, EM_SETSEL, 0, 0);

    PtLoopModal(dlg, owner, &g_ptTxtFim);
    g_ptTxtEdit = NULL;
    free(g_ptTxt);
    g_ptTxt = NULL;
}

static void ProcMostrarDetalhes(HWND owner, const ProcessoInfo *p)
{
    char *txt = (char *)malloc(16384);
    char tit[320];

    if (!txt)
        return;
    PtConstruirDetalhes(p, txt, 16384);
    snprintf(tit, sizeof(tit), "Detalhes - %s (PID %lu)", p->exeFile, (unsigned long)p->pid);
    PtMostrarTexto(owner, tit, txt);
    free(txt);
}

/* ------------------------------------------------------------------------- */
/* Menu de contexto de um processo (linha simples)                           */
/* ------------------------------------------------------------------------- */

static void MenuContextoProcesso(HWND hwnd, const ProcessoInfo *src)
{
    /* copia local: o timer reconstroi g_lvProcessos enquanto o menu esta aberto */
    ProcessoInfo p = *src;
    HMENU m = CreatePopupMenu();
    HMENU sub = CreatePopupMenu();
    char buf[8192];
    POINT pt;
    UINT cmd;

    if (!m || !sub)
    {
        if (m)
            DestroyMenu(m);
        if (sub)
            DestroyMenu(sub);
        return;
    }

    ProcAnexarMenuPrioridade(sub, ProcPrioridadeAtual(p.pid), 20);

    AppendMenuA(m, MF_STRING, 1, "Terminar Processo");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)sub, "Prioridade");
    AppendMenuA(m, MF_STRING, 30, "Afinidade de CPU...");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, 40, "Detalhes (caminho e linha de comandos)...");
    AppendMenuA(m, MF_STRING, 41, "Copiar linha de comandos");
    AppendMenuA(m, MF_STRING, 3, "Abrir Localizacao do Ficheiro");
    AppendMenuA(m, MF_STRING, 4, "Copiar PID");
    AppendMenuA(m, MF_STRING, 5, "Ver ligacoes deste processo");

    GetCursorPos(&pt);
    cmd = (UINT)TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(m); /* destroi tambem o submenu */

    if (cmd >= 20 && cmd < 20 + PT_PRIO_N)
    {
        if (!ProcDefinirPrioridade(p.pid, k_ptPrio[cmd - 20].classe))
            MessageBoxA(hwnd,
                        "O Windows recusou a alteracao da prioridade "
                        "(processo protegido ou de outro utilizador).",
                        "WinMon", MB_OK | MB_ICONWARNING);
        return;
    }

    switch (cmd)
    {
    case 1:
        if (MessageBoxA(hwnd, "Terminar este processo pode causar instabilidade. Deseja continuar?",
                        "Aviso", MB_YESNO | MB_ICONWARNING) == IDYES)
        {
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p.pid);

            if (!h || !TerminateProcess(h, 0))
                MessageBoxA(hwnd, "O Windows recusou terminar este processo.",
                            "WinMon", MB_OK | MB_ICONWARNING);
            if (h)
                CloseHandle(h);
        }
        break;
    case 30:
        ProcDialogoAfinidade(hwnd, p.pid, p.exeFile);
        break;
    case 40:
        ProcMostrarDetalhes(hwnd, &p);
        break;
    case 41:
        if (ProcLinhaComandos(p.pid, buf, sizeof(buf)))
            CopiarTextoClipboard(hwnd, buf);
        else
            MessageBoxA(hwnd, "Linha de comandos indisponivel (processo protegido ou terminado).",
                        "WinMon", MB_OK | MB_ICONINFORMATION);
        break;
    case 3:
        if (ProcCaminho(p.pid, buf, sizeof(buf)))
        {
            char args[MAX_PATH + 32];

            snprintf(args, sizeof(args), "/select,\"%s\"", buf);
            ShellExecuteA(NULL, "open", "explorer.exe", args, NULL, SW_SHOW);
        }
        break;
    case 4:
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)p.pid);
        CopiarTextoClipboard(hwnd, buf);
        break;
    case 5:
        MostrarLigacoesDoProcesso(p.exeFile);
        break;
    }
}
