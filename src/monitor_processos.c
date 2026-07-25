#include "globals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <psapi.h>
#include "utils.h"
#include "monitor_processos.h"

// Estado interno deste módulo, não partilhado com o resto do programa
static ProcessoCpuHistorico historicoCpu[MAX_HISTORICO];
static int totalHistorico = 0;
static ULONGLONG lastSystemTime = 0;
static ProcessoInfo listaProcessos[MAX_PROCESSES];

// Procura o histórico de CPU de um PID; devolve NULL se não existir
static ProcessoCpuHistorico *EncontrarHistorico(DWORD pid) {
    for (int i = 0; i < totalHistorico; i++) {
        if (historicoCpu[i].pid == pid) return &historicoCpu[i];
    }
    return NULL;
}

// Comparador para o qsort (ordem decrescente de RAM)
static int CompararProcessosPorRAM(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->memUsageMB < p2->memUsageMB) return 1;
    if (p1->memUsageMB > p2->memUsageMB) return -1;
    return 0;
}

// Comparador para o qsort (ordem decrescente de CPU)
static int CompararProcessosPorCPU(const void *a, const void *b) {
    const ProcessoInfo *p1 = (const ProcessoInfo *)a;
    const ProcessoInfo *p2 = (const ProcessoInfo *)b;
    if (p1->cpuPercent < p2->cpuPercent) return 1;
    if (p1->cpuPercent > p2->cpuPercent) return -1;
    return 0;
}

void MonitorarProcessos(char *buffer, size_t size, size_t *offset) {
    *offset += snprintf(buffer + *offset, size - *offset, "=== [ TOP 12 PROCESSOS (MAIOR CONSUMO CPU) ] ===\r\n");

    // Tempo de sistema atual (para calcular delta de tempo decorrido)
    FILETIME ftIdle, ftKernelSys, ftUserSys;
    GetSystemTimes(&ftIdle, &ftKernelSys, &ftUserSys);
    ULONGLONG currentSystemTime = FileTimeToU64(ftKernelSys) + FileTimeToU64(ftUserSys);
    ULONGLONG deltaSystemTime = (lastSystemTime != 0) ? (currentSystemTime - lastSystemTime) : 0;

    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32 pe32 = {.dwSize = sizeof(PROCESSENTRY32)};
    int totalProcessos = 0;
    ProcessoCpuHistorico novoHistorico[MAX_HISTORICO];
    int totalNovoHistorico = 0;

    if (Process32First(hProcessSnap, &pe32)) {
        do {
            if (pe32.th32ProcessID != 0 && totalProcessos < MAX_PROCESSES) {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
                SIZE_T memUsageMB = 0;
                double cpuPercent = 0.0;

                if (hProcess) {
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                        memUsageMB = pmc.WorkingSetSize / (1024 * 1024);
                    }

                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        ULONGLONG kernelU64 = FileTimeToU64(ftKernel);
                        ULONGLONG userU64 = FileTimeToU64(ftUser);
                        ULONGLONG totalProcTime = kernelU64 + userU64;

                        ProcessoCpuHistorico *hist = EncontrarHistorico(pe32.th32ProcessID);
                        if (hist && deltaSystemTime > 0) {
                            ULONGLONG deltaProc = totalProcTime - (hist->lastKernelTime + hist->lastUserTime);
                            // Percentagem relativa ao total do sistema, multiplicada pelo nº de núcleos
                            cpuPercent = ((double)deltaProc / (double)deltaSystemTime) * 100.0 * numProcessadores;
                            if (cpuPercent < 0.0) cpuPercent = 0.0;
                            if (cpuPercent > 100.0 * numProcessadores) cpuPercent = 100.0 * numProcessadores;
                        }

                        // Guarda para o próximo ciclo
                        if (totalNovoHistorico < MAX_HISTORICO) {
                            novoHistorico[totalNovoHistorico].pid = pe32.th32ProcessID;
                            novoHistorico[totalNovoHistorico].lastKernelTime = kernelU64;
                            novoHistorico[totalNovoHistorico].lastUserTime = userU64;
                            novoHistorico[totalNovoHistorico].valido = 1;
                            totalNovoHistorico++;
                        }
                    }
                    CloseHandle(hProcess);
                }

                listaProcessos[totalProcessos].pid = pe32.th32ProcessID;
                listaProcessos[totalProcessos].memUsageMB = memUsageMB;
                listaProcessos[totalProcessos].cpuPercent = cpuPercent;
                strncpy_s(listaProcessos[totalProcessos].exeFile, MAX_PATH, pe32.szExeFile, _TRUNCATE);

                totalProcessos++;
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }
    CloseHandle(hProcessSnap);

    // Atualiza o histórico global para o próximo ciclo
    memcpy(historicoCpu, novoHistorico, sizeof(ProcessoCpuHistorico) * totalNovoHistorico);
    totalHistorico = totalNovoHistorico;
    lastSystemTime = currentSystemTime;

    // Ordena por CPU (mais relevante que RAM para identificar picos de uso)
    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo), CompararProcessosPorCPU);

    int limite = (totalProcessos < 12) ? totalProcessos : 12;
    for (int i = 0; i < limite; i++) {
        *offset += snprintf(buffer + *offset, size - *offset,
                            "PID: %-6u | CPU: %5.1f%% | RAM: %5lu MB | %s\r\n",
                            listaProcessos[i].pid, listaProcessos[i].cpuPercent,
                            (unsigned long)listaProcessos[i].memUsageMB, listaProcessos[i].exeFile);
    }
    *offset += snprintf(buffer + *offset, size - *offset, "\r\n");

    // Também mostra o top 5 por RAM, para não perder essa informação
    *offset += snprintf(buffer + *offset, size - *offset, "=== [ TOP 5 PROCESSOS (MAIOR CONSUMO RAM) ] ===\r\n");
    qsort(listaProcessos, totalProcessos, sizeof(ProcessoInfo), CompararProcessosPorRAM);
    int limiteRAM = (totalProcessos < 5) ? totalProcessos : 5;
    for (int i = 0; i < limiteRAM; i++) {
        *offset += snprintf(buffer + *offset, size - *offset,
                            "PID: %-6u | RAM: %5lu MB | CPU: %5.1f%% | %s\r\n",
                            listaProcessos[i].pid, (unsigned long)listaProcessos[i].memUsageMB,
                            listaProcessos[i].cpuPercent, listaProcessos[i].exeFile);
    }
}
