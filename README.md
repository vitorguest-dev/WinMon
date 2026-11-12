<div align="center">

# WinMon

**Monitor de hardware e rede para Windows, em C (Win32)**
**Hardware and network monitor for Windows, written in C (Win32)**

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-0078D6)
![Language](https://img.shields.io/badge/language-C11-555555)
![API](https://img.shields.io/badge/API-Win32-blue)
![Status](https://img.shields.io/badge/status-modularização%20fase%201%20%7C%20modularization%20phase%201-orange)

[🇵🇹 Português](#-português) · [🇬🇧 English](#-english)

</div>

---

# 🇵🇹 Português

## Índice

- [Visão geral](#visão-geral)
- [Funcionalidades](#funcionalidades)
- [Nota arquitetural](#nota-arquitetural)
- [Estrutura do projeto](#estrutura-do-projeto)
- [Módulos](#módulos)
- [Requisitos](#requisitos)
- [Compilação](#compilação)
- [Utilização](#utilização)
- [Dicas e resolução de problemas](#dicas-e-resolução-de-problemas)
- [Como contribuir](#como-contribuir)
- [Roadmap](#roadmap)
- [Limitações](#limitações)

## Visão geral

O **WinMon** é uma aplicação Win32 em C que apresenta métricas do sistema em tempo real, organizadas por abas: **Resumo, CPU, Memória, Temperatura, Disco, Rede, GPU, Processos, Ligações, Definições, Rede Local, Serviços, Bateria, Ventoinhas e Sistema**.

Esta versão corresponde ao ficheiro original `main(4).c`, separado em ficheiros por área funcional para facilitar a leitura e a evolução, **preservando a ordem original da implementação**.

## Funcionalidades

| Área | O que oferece |
|---|---|
| **Monitorização em tempo real** | Métricas de CPU, memória, temperatura, disco, rede e GPU, apresentadas em abas dedicadas e num resumo geral |
| **Gráficos interativos** | Desenho dos gráficos em tempo real, com interação e exportação |
| **Processos** | Lista de processos com filtros e ordenação, vista agrupada ou em árvore (pai/filho), menu de contexto com prioridade, afinidade de CPU, detalhes (caminho, linha de comandos, processo pai) e cópia da linha de comandos |
| **Ligações de rede** | Visualização das ligações ativas do sistema |
| **Rede local** | Descoberta de dispositivos via ARP, traceroute e testes DNS |
| **Estado da rede** | Ping e informação de Wi-Fi |
| **Qualidade da ligação** | Latência, jitter, perda de pacotes e tempos mínimo/médio/máximo por destino (Ferramentas de rede → Latência / jitter / perda) |
| **Diagnóstico HTTP/HTTPS** | Teste de URL com código HTTP, tempo até resposta e suporte de TLS/proxy do Windows |
| **Consulta DNS** | Resolução IPv4 (A), IPv6 (AAAA) e tentativa de DNS inverso (PTR) |
| **Wi-Fi** | SSID, qualidade do sinal, tipo PHY, velocidades de receção/emissão, BSSID e algoritmo de autenticação |
| **Bateria** | Carga, alimentação AC/bateria, estado de carga e autonomia estimada |
| **Ventoinhas** | Consulta de ventoinhas e velocidade desejada via WMI, quando o fabricante/driver expõe `Win32_Fan` |
| **Sistema** | Sistema operativo, equipamento, placa-mãe, BIOS/UEFI, processador, módulos de RAM e placa gráfica (WMI, lido em segundo plano) |
| **Sessões, logging e alertas** | Registo de sessões, exportação CSV e alertas configuráveis |
| **Personalização** | Diálogos de estilo, com seleção de cores e fontes |
| **Overlay** | Sobreposição de métricas no ecrã, com perfis |

> A disponibilidade de algumas métricas depende do hardware, dos drivers e dos serviços do Windows.

## Nota arquitetural

> [!IMPORTANT]
> Esta é uma **primeira fase de modularização**. Os ficheiros `.inc` são incluídos por `src/main.c` e compilados como **uma única unidade de tradução**. Isto preserva o acesso às variáveis `static`, tipos e funções, que estão fortemente acoplados no código original.
>
> Ainda **não** são módulos C independentes (`.c` + `.h` com API pública). A extração deve ser feita gradualmente, começando por utilitários sem estado partilhado (ver [Roadmap](#roadmap)).

## Estrutura do projeto

```text
WinMon-modular/
├── README.md
├── Makefile
├── build-mingw.bat
└── src/
    ├── main.c
    └── modules/
        ├── 01_network_status.inc
        ├── 02_utilities_and_collection.inc
        ├── 03_graphics.inc
        ├── 04_ui_and_tabs.inc
        ├── 05_processes_and_connections.inc
        ├── 06_local_network.inc
        ├── 07_panels_and_events.inc
        ├── 08_sessions_logging_alerts.inc
        ├── 09_style_dialogs.inc
        ├── 10_overlay.inc
        ├── 11_window_and_entrypoint.inc
        ├── 12_network_extras.inc
        ├── 13_services.inc
        ├── 14_hardware_extras.inc
        ├── 15_system_info.inc
        └── 16_process_tools.inc
```

## Módulos

| Ficheiro | Responsabilidade |
|---|---|
| `01_network_status.inc` | Ping, Wi-Fi e seleção de cores/fontes |
| `02_utilities_and_collection.inc` | Utilitários, WMI e recolha de métricas |
| `03_graphics.inc` | Desenho dos gráficos, interação e exportação |
| `04_ui_and_tabs.inc` | RichEdit, componentes da interface e criação de abas |
| `05_processes_and_connections.inc` | Lista de processos, filtros, ordenação e ligações |
| `06_local_network.inc` | Descoberta ARP, traceroute e testes DNS |
| `07_panels_and_events.inc` | Painéis, layout e despacho de eventos |
| `08_sessions_logging_alerts.inc` | Sessões, CSV e alertas |
| `09_style_dialogs.inc` | Diálogos de estilo e personalização |
| `10_overlay.inc` | Overlay, perfis e desenho do overlay |
| `12_network_extras.inc` | Ferramentas de rede: adaptadores, latência/jitter/perda, scan TCP local, consumo, estatísticas, Wake-on-LAN, MTU, rotas, teste HTTP/HTTPS, consulta DNS e detalhes Wi-Fi |
| `13_services.inc` | Monitorização de serviços Windows |
| `14_hardware_extras.inc` | Monitorização da bateria e consulta WMI de ventoinhas |
| `15_system_info.inc` | Aba Sistema: consultas WMI (SO, BIOS, placa-mãe, CPU, RAM, GPU) numa thread própria |
| `16_process_tools.inc` | Menu de contexto de processos: prioridade, afinidade de CPU, detalhes e linha de comandos |
| `11_window_and_entrypoint.inc` | `WindowProc` e `WinMain` |

## Requisitos

- **Windows 10/11** (API Win32).
- Um compilador C para Windows, de preferência **x64**:
  - MinGW-w64 / UCRT64, **ou**
  - Visual Studio Build Tools (MSVC).
- **Windows SDK** e as bibliotecas de sistema usadas: PDH, IP Helper, Winsock, PSAPI, DXGI, WIC, WMI, Common Controls, RichEdit, ICMP e DNS.

## Compilação

### MinGW-w64

Abra o terminal **MSYS2 UCRT64** (ou um terminal com `gcc` no `PATH`), entre na pasta do projeto e execute:

```sh
make
```

O `Makefile` gera `build/WinMon.exe`. Em alternativa, compile diretamente:

```sh
gcc -std=c11 -O2 -Wall -Wextra -municode -mwindows \
  -o WinMon.exe src/main.c \
  -lpdh -liphlpapi -lws2_32 -luser32 -lpsapi -lgdi32 \
  -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid \
  -ladvapi32 -lcomdlg32 -lwindowscodecs -ldnsapi -lwlanapi -lwinhttp -luuid
```

> [!NOTE]
> Se a versão do MinGW não aceitar `-municode`, remova a opção (não deve ser necessária para `WinMain`/APIs ANSI). O comando pode exigir ajustes conforme a distribuição/versão. O código original contém algumas opções específicas do MSVC, pelo que convém verificar os avisos/erros do compilador usado.

O script `build-mingw.bat` oferece uma alternativa para Windows com MinGW no `PATH`.

### MSVC

Abra a **x64 Native Tools Command Prompt for VS** ou a **Developer PowerShell for VS**, na raiz do projeto:

```bat
cl /nologo /std:c11 /O2 /W4 /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT /Fe:WinMon.exe src\main.c ^
  pdh.lib iphlpapi.lib ws2_32.lib user32.lib psapi.lib gdi32.lib shell32.lib ^
  comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib comdlg32.lib ^
  windowscodecs.lib dnsapi.lib wlanapi.lib winhttp.lib
```

Se o linker indicar uma biblioteca em falta, confirme que o Windows SDK está instalado e acrescente a biblioteca correspondente. As diretivas `#pragma comment(lib, ...)` do código também declaram várias dependências para MSVC.

## Utilização

1. Compile o executável (ver acima).
2. Execute `WinMon.exe` num computador Windows.
3. Explore as abas: Resumo, CPU, Memória, Temperatura, Disco, Rede, GPU, Processos, Ligações, Definições, Rede Local, Serviços, Bateria, Ventoinhas e Sistema.
4. Use **Definições** para ajustar alertas, aparência, overlay, intervalo de atualização e logging, quando disponíveis.

**Notas:**

- O logging e o histórico podem criar ficheiros de dados junto ao executável, conforme a implementação. Garanta permissões de escrita na pasta.
- Algumas métricas dependem do hardware, dos drivers e dos serviços do Windows.
- A recolha de informação de processos e rede pode ter limitações de permissões. Execute primeiro como utilizador normal; não use privilégios elevados sem necessidade.

## Dicas e resolução de problemas

### Dicas de utilização

- **Comece como utilizador normal.** Só use privilégios elevados se for realmente necessário.
- **Escolha uma pasta com permissões de escrita** para o executável, pois o logging e o histórico podem criar ficheiros junto a ele.
- **Configure alertas em Definições** para ser avisado quando uma métrica ultrapassar os limites que definir.
- **Use a exportação CSV** das sessões para analisar os dados numa folha de cálculo.
- **Use os filtros e a ordenação** na aba Processos para encontrar rapidamente os maiores consumidores de recursos.
- **Temperatura, GPU e RPM das ventoinhas** podem não estar disponíveis: dependem do hardware, dos drivers e dos fornecedores WMI. A página Ventoinhas indica quando `Win32_Fan` não é exposto.
- A API nativa de energia do Windows fornece carga e autonomia estimada, mas não uma medição universal da saúde/capacidade máxima da bateria.
- Para testar a qualidade da ligação, abra **Rede Local → Ferramentas de rede** e selecione **Latência / jitter / perda**. O teste envia 10 pedidos ICMP por destino; firewalls ou destinos que bloqueiem ICMP podem produzir perda aparente. Os resultados são acrescentados a `winmon_rede.csv`, junto ao executável.

### Problemas comuns

| Problema | O que verificar |
|---|---|
| `-municode` não reconhecido | Remova a opção do comando do GCC |
| Biblioteca em falta no linker | Confirme que o Windows SDK está instalado e acrescente a biblioteca correspondente |
| `make` ou `gcc` não encontrado | Abra o terminal **MSYS2 UCRT64** ou confirme que o MinGW-w64 está no `PATH` |
| `cl` não encontrado | Use a **x64 Native Tools Command Prompt for VS** ou a **Developer PowerShell for VS** |
| Avisos ou erros específicos do MSVC | O código original usa algumas opções MSVC; verifique os diagnósticos do compilador usado |
| Não aparecem dados de alguns processos ou ligações | Pode ser uma limitação de permissões do Windows |

## Como contribuir

Contribuições são bem-vindas: correções, melhorias, documentação ou a extração de módulos independentes (ver [Roadmap](#roadmap)).

### Fluxo de trabalho

1. Faça **fork** do repositório e clone-o localmente.
2. Crie um **branch** para a sua alteração, por exemplo `feature/nova-metrica` ou `fix/overlay-resolucao`.
3. Implemente a alteração seguindo o [guia técnico](#adicionar-uma-funcionalidade) abaixo.
4. **Compile com avisos ativados** e teste (ver a checklist no passo 5).
5. Faça commits pequenos e focados, com mensagens claras.
6. Abra um **Pull Request** a descrever o que mudou, porquê e como testou.

### Boas práticas

- Siga o estilo e as convenções do código existente.
- Mantenha cada Pull Request focado numa única alteração.
- Não misture refatorações grandes com novas funcionalidades.
- Não reordene os `#include` em `src/main.c` sem verificar as dependências.
- Documente quem inicializa e liberta cada recurso Win32 novo.

### Mensagens de commit (sugestão)

Use mensagens curtas, no imperativo e com prefixo opcional:

```text
feat: adicionar teste DNS com servidor personalizado
fix: libertar handle em falha de ping
docs: atualizar instruções de compilação
refactor: extrair formatação de bytes para utilitário
```

### Reportar problemas

Ao abrir uma issue, indique: versão do Windows, compilador e versão, passos para reproduzir, comportamento esperado vs. observado e, se aplicável, o output do compilador.

### Checklist do Pull Request

- [ ] Compila sem novos avisos (`/W4` no MSVC ou `-Wall -Wextra` no GCC).
- [ ] Testado no caso normal e em falhas de API.
- [ ] Sem fugas de handles, objetos GDI ou memória.
- [ ] README atualizado, se o comportamento ou a estrutura mudaram.

### Adicionar uma funcionalidade

#### 1. Identifique a área responsável

Coloque a funcionalidade no módulo cuja responsabilidade corresponde ao comportamento. Por exemplo, uma nova visualização de métricas pertence normalmente a `03_graphics.inc`, e a sua integração na interface a `04_ui_and_tabs.inc` ou `07_panels_and_events.inc`.

#### 2. Preserve a ordem de inclusão

Os `.inc` são incluídos numa ordem deliberada em `src/main.c`. Como partilham o mesmo contexto de compilação, uma função ou tipo tem de estar declarado antes de ser usado, ou ter uma declaração antecipada. **Não reordene os `#include` sem verificar todas as dependências.**

#### 3. Evite aumentar o estado global

Antes de criar variáveis globais, prefira passar dados por parâmetros ou agrupá-los numa estrutura. Documente quem inicializa e liberta cada recurso Win32 (handles, fontes, brushes, threads, eventos e memória dinâmica).

#### 4. Integre comandos e interface de forma consistente

- Atribua um identificador `IDC_...` ou `ID_...` que não esteja em uso.
- Crie o controlo na função responsável pela interface.
- Trate o comando no despacho de eventos correspondente.
- Atualize o layout e a limpeza de recursos, se necessário.
- Se usar threads, **não atualize controlos da UI a partir da thread de trabalho**: envie mensagens para a janela e trate-as na thread da interface.

#### 5. Teste antes de integrar

- [ ] Compile com avisos ativados (`/W4` no MSVC ou `-Wall -Wextra` no GCC).
- [ ] Teste o caso normal, falhas de API, ausência de hardware/serviço e encerramento da aplicação.
- [ ] Confirme que todos os handles, objetos GDI e alocações são libertados.
- [ ] Teste com permissões de utilizador normal.
- [ ] Teste em diferentes resoluções/monitores, se a funcionalidade envolver o overlay.

## Roadmap

**Próximo passo recomendado: evoluir para módulos C independentes.** Extraia uma área de cada vez para `src/<area>.c` e `include/<area>.h`:

1. Comece por funções utilitárias puras (formatação, comparadores) que não dependam de variáveis `static` globais.
2. Declare no `.h` apenas a API necessária; mantenha as funções internas como `static` no `.c`.
3. Passe dependências por parâmetros ou estruturas de contexto, em vez de expor globais.
4. Atualize o build para compilar todos os `.c` e ligá-los no executável.
5. Faça uma extração de cada vez e compare o comportamento antes/depois.

## Limitações

- A separação foi feita por regiões funcionais, mantendo a sequência e o contexto de compilação originais.
- Não foi possível validar uma compilação Windows no ambiente em que a modularização foi feita.
- Não foram alteradas intencionalmente as regras funcionais do programa.
- Recomenda-se uma compilação de verificação em Windows antes de usar a aplicação em produção.

[⬆ Voltar ao topo](#winmon)

---

# 🇬🇧 English

## Table of contents

- [Overview](#overview)
- [Features](#features)
- [Architecture note](#architecture-note)
- [Project structure](#project-structure)
- [Modules](#modules-1)
- [Requirements](#requirements)
- [Building](#building)
- [Usage](#usage)
- [Tips and troubleshooting](#tips-and-troubleshooting)
- [How to contribute](#how-to-contribute)
- [Roadmap](#roadmap-1)
- [Limitations](#limitations-1)

## Overview

**WinMon** is a Win32 application written in C that shows real-time system metrics, organized in tabs: **Summary, CPU, Memory, Temperature, Disk, Network, GPU, Processes, Connections, Settings and Local Network**.

This version is based on the original `main(4).c` file, split into files by functional area to make it easier to read and evolve, **while preserving the original order of the implementation**.

## Features

| Area | What it offers |
|---|---|
| **Real-time monitoring** | CPU, memory, temperature, disk, network and GPU metrics, shown in dedicated tabs and in a general summary |
| **Interactive charts** | Real-time chart rendering, with interaction and export |
| **Processes** | Process list with filters and sorting |
| **Network connections** | View of the system's active connections |
| **Local network** | Device discovery via ARP, traceroute and DNS tests |
| **Network status** | Ping and Wi-Fi information |
| **Sessions, logging and alerts** | Session recording, CSV export and configurable alerts |
| **Customization** | Style dialogs, with color and font selection |
| **Overlay** | On-screen metrics overlay, with profiles |

> The availability of some metrics depends on hardware, drivers and Windows services.

## Architecture note

> [!IMPORTANT]
> This is a **first phase of modularization**. The `.inc` files are included by `src/main.c` and compiled as a **single translation unit**. This keeps access to the `static` variables, types and functions, which are tightly coupled in the original code.
>
> They are **not yet** independent C modules (`.c` + `.h` with a public API). Extraction should be done gradually, starting with utilities that have no shared state (see [Roadmap](#roadmap-1)).

## Project structure

```text
WinMon-modular/
├── README.md
├── Makefile
├── build-mingw.bat
└── src/
    ├── main.c
    └── modules/
        ├── 01_network_status.inc
        ├── 02_utilities_and_collection.inc
        ├── 03_graphics.inc
        ├── 04_ui_and_tabs.inc
        ├── 05_processes_and_connections.inc
        ├── 06_local_network.inc
        ├── 07_panels_and_events.inc
        ├── 08_sessions_logging_alerts.inc
        ├── 09_style_dialogs.inc
        ├── 10_overlay.inc
        ├── 11_window_and_entrypoint.inc
        ├── 12_network_extras.inc
        ├── 13_services.inc
        ├── 14_hardware_extras.inc
        ├── 15_system_info.inc
        └── 16_process_tools.inc
```

## Modules

| File | Responsibility |
|---|---|
| `01_network_status.inc` | Ping, Wi-Fi, and color/font selection |
| `02_utilities_and_collection.inc` | Utilities, WMI and metrics collection |
| `03_graphics.inc` | Chart drawing, interaction and export |
| `04_ui_and_tabs.inc` | RichEdit, UI components and tab creation |
| `05_processes_and_connections.inc` | Process list, filters, sorting and connections |
| `06_local_network.inc` | ARP discovery, traceroute and DNS tests |
| `07_panels_and_events.inc` | Panels, layout and event dispatching |
| `08_sessions_logging_alerts.inc` | Sessions, CSV and alerts |
| `09_style_dialogs.inc` | Style and customization dialogs |
| `10_overlay.inc` | Overlay, profiles and overlay rendering |
| `11_window_and_entrypoint.inc` | `WindowProc` and `WinMain` |

## Requirements

- **Windows 10/11** (Win32 API).
- A C compiler for Windows, preferably **x64**:
  - MinGW-w64 / UCRT64, **or**
  - Visual Studio Build Tools (MSVC).
- The **Windows SDK** and the system libraries used: PDH, IP Helper, Winsock, PSAPI, DXGI, WIC, WMI, Common Controls, RichEdit, ICMP and DNS.

## Building

### MinGW-w64

Open an **MSYS2 UCRT64** terminal (or any terminal with MinGW-w64 `gcc` on the `PATH`), go to the project folder and run:

```sh
make
```

The `Makefile` produces `build/WinMon.exe`. Alternatively, compile directly:

```sh
gcc -std=c11 -O2 -Wall -Wextra -municode -mwindows \
  -o WinMon.exe src/main.c \
  -lpdh -liphlpapi -lws2_32 -luser32 -lpsapi -lgdi32 \
  -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid \
  -ladvapi32 -lcomdlg32 -lwindowscodecs -ldnsapi -lwlanapi -lwinhttp -luuid
```

> [!NOTE]
> If your MinGW version does not accept `-municode`, remove the option (it should not be needed for `WinMain`/ANSI APIs). The command may need adjustments depending on the distribution/version. The original code contains some MSVC-specific options, so check the warnings/errors from your compiler.

The `build-mingw.bat` script is an alternative for Windows with MinGW on the `PATH`.

### MSVC

Open the **x64 Native Tools Command Prompt for VS** or **Developer PowerShell for VS**, at the project root:

```bat
cl /nologo /std:c11 /O2 /W4 /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT /Fe:WinMon.exe src\main.c ^
  pdh.lib iphlpapi.lib ws2_32.lib user32.lib psapi.lib gdi32.lib shell32.lib ^
  comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib comdlg32.lib ^
  windowscodecs.lib dnsapi.lib wlanapi.lib winhttp.lib
```

If the linker reports a missing library, make sure the Windows SDK is installed and add the corresponding system library. The `#pragma comment(lib, ...)` directives in the code also declare several dependencies for MSVC.

## Usage

1. Build the executable (see above).
2. Run `WinMon.exe` on a Windows machine.
3. Explore the tabs: Summary, CPU, Memory, Temperature, Disk, Network, GPU, Processes, Connections, Settings and Local Network.
4. Use **Settings** to adjust alerts, appearance, overlay, refresh interval and logging, where available.

**Notes:**

- Logging and history may create data files next to the executable, depending on the implementation. Make sure the folder is writable.
- Some metrics depend on hardware, drivers and Windows services.
- Process and network information may be limited by permissions. Run as a normal user first; do not use elevated privileges unless necessary.

## Tips and troubleshooting

### Usage tips

- **Start as a normal user.** Only use elevated privileges if truly necessary.
- **Pick a writable folder** for the executable, since logging and history may create files next to it.
- **Configure alerts in Settings** to be notified when a metric crosses the limits you set.
- **Use the CSV export** of sessions to analyze the data in a spreadsheet.
- **Use filters and sorting** in the Processes tab to quickly find the biggest resource consumers.
- **Temperature and GPU** may show empty on some machines: it depends on hardware, drivers and Windows services.

### Common problems

| Problem | What to check |
|---|---|
| `-municode` not recognized | Remove the option from the GCC command |
| Missing library at link time | Make sure the Windows SDK is installed and add the corresponding library |
| `make` or `gcc` not found | Open the **MSYS2 UCRT64** terminal or make sure MinGW-w64 is on the `PATH` |
| `cl` not found | Use the **x64 Native Tools Command Prompt for VS** or **Developer PowerShell for VS** |
| MSVC-specific warnings or errors | The original code uses some MSVC options; check your compiler's diagnostics |
| No data for some processes or connections | It may be a Windows permissions limitation |

## How to contribute

Contributions are welcome: fixes, improvements, documentation, or extracting independent modules (see [Roadmap](#roadmap-1)).

### Workflow

1. **Fork** the repository and clone it locally.
2. Create a **branch** for your change, for example `feature/new-metric` or `fix/overlay-resolution`.
3. Implement the change following the [technical guide](#adding-a-feature) below.
4. **Build with warnings enabled** and test (see the checklist in step 5).
5. Make small, focused commits with clear messages.
6. Open a **Pull Request** describing what changed, why, and how you tested it.

### Best practices

- Follow the style and conventions of the existing code.
- Keep each Pull Request focused on a single change.
- Do not mix large refactors with new features.
- Do not reorder the `#include` directives in `src/main.c` without checking dependencies.
- Document who initializes and releases each new Win32 resource.

### Commit messages (suggestion)

Use short, imperative messages with an optional prefix:

```text
feat: add DNS test with custom server
fix: release handle on ping failure
docs: update build instructions
refactor: extract byte formatting into a utility
```

### Reporting issues

When opening an issue, include: Windows version, compiler and version, steps to reproduce, expected vs. observed behavior and, if applicable, the compiler output.

### Pull Request checklist

- [ ] Builds without new warnings (`/W4` on MSVC or `-Wall -Wextra` on GCC).
- [ ] Tested in the normal case and on API failures.
- [ ] No leaked handles, GDI objects or memory.
- [ ] README updated if behavior or structure changed.

### Adding a feature

#### 1. Identify the responsible area

Put the feature in the module whose responsibility matches the behavior. For example, a new metrics visualization usually belongs in `03_graphics.inc`, and its UI integration in `04_ui_and_tabs.inc` or `07_panels_and_events.inc`.

#### 2. Preserve the include order

The `.inc` files are included in a deliberate order in `src/main.c`. Since they share the same compilation context, a function or type must be declared before use, or have a suitable forward declaration. **Do not reorder the `#include` directives without checking all dependencies.**

#### 3. Avoid growing global state

Before adding global variables, prefer passing data through parameters or grouping it in a struct. Document who initializes and releases each Win32 resource (handles, fonts, brushes, threads, events and dynamic memory).

#### 4. Add commands and UI consistently

- Assign an `IDC_...` or `ID_...` identifier that is not already in use.
- Create the control in the function responsible for the UI.
- Handle the command in the matching event dispatcher.
- Update layout and resource cleanup if needed.
- If you use threads, **do not update UI controls directly from the worker thread**: post messages to the window and handle them on the UI thread.

#### 5. Test before integrating

- [ ] Build with warnings enabled (`/W4` on MSVC or `-Wall -Wextra` on GCC).
- [ ] Test the normal case, API failures, missing hardware/service, and application shutdown.
- [ ] Confirm that all handles, GDI objects and allocations are released.
- [ ] Test with normal user permissions.
- [ ] Test on different resolutions/monitors if the feature involves the overlay.

## Roadmap

**Recommended next step: evolve into independent C modules.** Extract one area at a time into `src/<area>.c` and `include/<area>.h`:

1. Start with pure utility functions (formatting, comparators) that do not depend on global `static` variables.
2. Declare only the required API in the `.h`; keep internal functions `static` in the `.c`.
3. Pass dependencies via parameters or context structs instead of exposing globals.
4. Update the build to compile all `.c` files and link them into the executable.
5. Extract one area at a time and compare behavior before/after.

## Limitations

- The split was done by functional regions, keeping the original sequence and compilation context.
- A Windows build could not be validated in the environment where the modularization was done.
- The program's functional rules were not intentionally changed.
- A verification build on Windows is recommended before using the application in production.

[⬆ Back to top](#winmon)
