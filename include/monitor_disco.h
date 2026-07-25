#ifndef MONITOR_DISCO_H
#define MONITOR_DISCO_H

#include <stddef.h>
#include <pdh.h>

// Regista na query PDH partilhada os contadores de leitura/escrita de disco.
// Deve ser chamado uma vez em WM_CREATE, ANTES do primeiro PdhCollectQueryData.
void MonitorarDiscoIO_Inicializar(PDH_HQUERY query);

// Escreve no buffer o espaço usado/livre de cada disco fixo e regista alerta
// se algum disco > LIMITE_DISCO_PERCENT
void MonitorarDiscos(char *buffer, size_t size, size_t *offset);

// Escreve no buffer a velocidade de leitura/escrita (bytes/seg) de todos os discos.
// Os valores já foram atualizados pelo PdhCollectQueryData chamado em MonitorarCPU,
// por isso esta função deve ser chamada DEPOIS de MonitorarCPU no ciclo de atualização.
void MonitorarDiscoIO(char *buffer, size_t size, size_t *offset);

#endif // MONITOR_DISCO_H
