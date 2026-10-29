# WinMon — organização modular

Projeto C para Windows baseado no ficheiro `main(4).c` fornecido. O código foi separado em ficheiros por áreas funcionais para facilitar a leitura e a evolução, preservando a ordem original da implementação.

> **Nota arquitetural importante:** esta é uma primeira fase de modularização. Os ficheiros `.inc` são incluídos por `src/main.c` e compilados como uma única unidade de tradução. Isto preserva o acesso às variáveis `static`, tipos e funções que estão fortemente acoplados no código original. Não são ainda módulos C independentes (`.c` + `.h` com API pública). A extração para unidades independentes deve ser feita gradualmente, começando por utilitários sem estado partilhado.

## Estrutura

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
        └── 11_window_and_entrypoint.inc
```

### Responsabilidade de cada módulo

- `01_network_status.inc` — ping, Wi-Fi e seleção de cores/fontes.
- `02_utilities_and_collection.inc` — utilitários, WMI e recolha de métricas.
- `03_graphics.inc` — desenho dos gráficos, interação e exportação.
- `04_ui_and_tabs.inc` — RichEdit, componentes da interface e criação de abas.
- `05_processes_and_connections.inc` — lista de processos, filtros, ordenação e ligações.
- `06_local_network.inc` — descoberta ARP, traceroute e testes DNS.
- `07_panels_and_events.inc` — painéis, layout e despacho de eventos.
- `08_sessions_logging_alerts.inc` — sessões, CSV e alertas.
- `09_style_dialogs.inc` — diálogos de estilo e personalização.
- `10_overlay.inc` — overlay, perfis e desenho do overlay.
- `11_window_and_entrypoint.inc` — `WindowProc` e `WinMain`.

## Requisitos

- Windows 10/11 (o programa usa a API Win32).
- Um compilador C compatível com Windows: MinGW-w64/UCRT64 ou Visual Studio Build Tools (MSVC), preferencialmente x64.
- Windows SDK e bibliotecas de sistema usadas pelo projeto: PDH, IP Helper, Winsock, PSAPI, DXGI, WIC, WMI, Common Controls, RichEdit, ICMP e DNS.

## Compilar com MinGW-w64

Abra o terminal **MSYS2 UCRT64** (ou um terminal com `gcc` MinGW-w64 no `PATH`), entre na pasta do projeto e execute:

```sh
make
```

Ou compile diretamente:

```sh
gcc -std=c11 -O2 -Wall -Wextra -municode -mwindows \
  -o WinMon.exe src/main.c \
  -lpdh -liphlpapi -lws2_32 -luser32 -lpsapi -lgdi32 \
  -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid \
  -ladvapi32 -lcomdlg32 -lwindowscodecs -ldnsapi -luuid
```

Se a versão do MinGW não aceitar `-municode`, isso não deve ser necessário para `WinMain`/APIs ANSI; remova essa opção. O comando pode exigir ajustes conforme a distribuição/versão do MinGW. O código original contém algumas opções específicas do MSVC, pelo que convém confirmar os avisos/erros do compilador usado.

O `Makefile` gera `build/WinMon.exe`. O script `build-mingw.bat` oferece uma alternativa para Windows com MinGW no `PATH`.

## Compilar com MSVC

Abra **x64 Native Tools Command Prompt for VS** ou **Developer PowerShell for VS**, na raiz do projeto, e execute:

```bat
cl /nologo /std:c11 /O2 /W4 /GS /sdl /guard:cf /DYNAMICBASE /NXCOMPAT /Fe:WinMon.exe src\main.c ^
  pdh.lib iphlpapi.lib ws2_32.lib user32.lib psapi.lib gdi32.lib shell32.lib ^
  comctl32.lib ole32.lib oleaut32.lib wbemuuid.lib advapi32.lib comdlg32.lib ^
  windowscodecs.lib dnsapi.lib
```

Se o linker indicar uma biblioteca em falta, confirme que o Windows SDK está instalado e acrescente a biblioteca de sistema correspondente. As diretivas `#pragma comment(lib, ...)` do código também declaram várias dependências para MSVC.

## Executar e utilizar

1. Compile o executável seguindo uma das opções acima.
2. Execute `WinMon.exe` num computador Windows.
3. Explore as abas Resumo, CPU, Memória, Temperatura, Disco, Rede, GPU, Processos, Ligações, Definições e Rede Local.
4. Use **Definições** para ajustar alertas, aparência, overlay, intervalo de atualização e logging, quando essas opções estiverem disponíveis.
5. O logging e o histórico podem criar ficheiros de dados junto ao executável, conforme a implementação. Garanta que a pasta tem permissões de escrita.

Algumas métricas dependem do hardware, dos drivers e dos serviços do Windows. A recolha de informação de processos e rede pode ter limitações de permissões. Execute como utilizador normal primeiro; não use privilégios elevados sem necessidade.

## Como adicionar funcionalidades futuras

### 1. Identifique a área responsável

Coloque a funcionalidade no módulo cuja responsabilidade corresponde ao comportamento. Por exemplo, uma nova visualização de métricas pertence normalmente a `03_graphics.inc` e a sua integração na interface a `04_ui_and_tabs.inc` ou `07_panels_and_events.inc`.

### 2. Preserve a ordem de inclusão

Os `.inc` são incluídos numa ordem deliberada em `src/main.c`. Como ainda partilham o mesmo contexto de compilação, uma função ou tipo tem de estar declarado antes de ser usado, ou ter uma declaração antecipada adequada. Não reordene os `#include` sem verificar todas as dependências.

### 3. Evite aumentar o estado global

Antes de criar novas variáveis globais, prefira passar os dados por parâmetros ou agrupá-los numa estrutura. Documente quem inicializa e liberta cada recurso Win32 (handles, fontes, brushes, threads, eventos e memória dinâmica).

### 4. Adicione comandos e interface de forma consistente

- Atribua um identificador `IDC_...` ou `ID_...` que não esteja em uso.
- Crie o controlo na função responsável pela interface.
- Trate o comando no despacho de eventos correspondente.
- Atualize o layout e a limpeza de recursos, se necessário.
- Se usar threads, não atualize controlos da UI diretamente a partir da thread de trabalho; envie mensagens para a janela e trate-as na thread da interface.

### 5. Teste antes de integrar

- Compile com avisos ativados (`/W4` no MSVC ou `-Wall -Wextra` no GCC).
- Teste o caso normal, falhas de API, ausência de hardware/serviço e encerramento da aplicação.
- Confirme que todos os handles, objetos GDI e alocações são libertados.
- Teste com permissões de utilizador normal e em diferentes resoluções/monitores quando a funcionalidade envolve o overlay.

### 6. Evolua para módulos C independentes (próximo passo recomendado)

Para uma separação real por compilação, extraia uma área de cada vez para `src/<area>.c` e `include/<area>.h`:

1. Comece por funções utilitárias puras (por exemplo, formatação e comparadores) que não dependam de variáveis `static` globais.
2. Declare no `.h` apenas a API necessária; mantenha funções internas como `static` no `.c` correspondente.
3. Passe dependências explicitamente por parâmetros ou por estruturas de contexto em vez de expor globais.
4. Atualize o build para compilar todos os `.c` e ligá-los no executável.
5. Faça uma extração de cada vez e compare o comportamento antes/depois.

## Limitações desta entrega

- A separação foi feita por regiões funcionais mantendo a sequência e o contexto de compilação original; não foi possível validar uma compilação Windows neste ambiente.
- Não foram alteradas intencionalmente as regras funcionais do programa.
- Recomenda-se uma compilação de verificação em Windows antes de usar a aplicação em produção.
