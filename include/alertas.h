#ifndef ALERTAS_H
#define ALERTAS_H

// Regista um intervalo de texto [inicio, fim) a colorir de vermelho por estar em alerta.
// Chamado pelos módulos de monitorização (RAM, disco) quando um valor ultrapassa o limite.
void RegistarAlerta(long inicio, long fim);

// Aplica as cores no RichEdit: preto por omissão, vermelho nos intervalos registados neste ciclo
void AplicarCoresDeAlerta(void);

// Atualiza o título da janela principal para refletir se há algum alerta ativo
void AtualizarTituloJanela(void);

#endif // ALERTAS_H
