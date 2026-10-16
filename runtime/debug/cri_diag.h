#pragma once
// Diagnostico do N2b-0 (docs/N2b-PLANO.md, condicao de ociosidade do servidor do CRI). So
// existe para medir; nunca e o comportamento do produto.
//
// BS_CRI_DIAG=contar   conta, por passada do servidor do CRI (8C1586B8, thread 3), quantas
//                      das 32 entradas da tabela de handles (0x8C2D0D84, 120 bytes cada) estao
//                      ativas (campo +4 diferente de 0).
// BS_CRI_DIAG=pular    alem de contar, NAO executa as passadas sem nenhuma entrada ativa.
// BS_CRI_DIAG=pular2   NAO executa as passadas sem entrada ativa, sem handle ADX ativo (estado
//                      1-5) e sem objeto servidor registrado.
// BS_CRI_DIAG=pular3   NAO executa as passadas sem entrada ativa, sem handle ADX ativo e sem
//                      stream em uso no gerenciador de streaming (0x8C2DA958, 68 bytes).
// BS_CRI_DIAG=menos1   NAO executa a primeira passada de cada VBlank (negativo do criterio S4:
//                      uma ativacao a menos por VBlank tem de ser detectada).
// BS_CRI_DIAG=repetir3 controle do pular3: executa DUAS vezes cada passada que o pular3 pularia
//                      (se a passada e ociosa, so o tempo muda; separa efeito de tempo de
//                      efeito de trabalho omitido). "puladas" conta as repetidas.
// BS_CRI_DIAG=fluxo    classifica cada passada (trabalho ou ociosa) pelo que o gerenciador de
//                      streaming (8C174888) fez: leitura em curso, emissao (8C174BAA), buffer
//                      esvaziado (8C1749E0), candidata (8C174A36), estado mudado; MW/ADX ativos.
//                      "Elegivel" = a passada anterior foi ociosa e nenhuma interrupcao, troca de
//                      thread ou VBlank aconteceu desde entao; "violacao" = elegivel com trabalho.
// BS_CRI_FLUXO_LOG=<arquivo>  com fluxo/pular5/repetir5: uma linha por passada com trabalho
//                      (VBlank, tipos); compara a sequencia de trabalho entre os modos (S4').
// BS_CRI_CESSAO_LOG=<arquivo>  intervalos do BS_EVENT_TRACE cobertos por cada chamada com troca a
//                      8C1588AA (exclusao mutua da passada): a TN normalizada remove so as trocas
//                      la dentro.
// BS_CRI_SNAP_VB=<V>   nos VBlanks V-2..V, compara toda a RAM principal e a de som antes e depois
//                      de cada passada (fora a pilha morta e o indice do rodizio).
// BS_CRI_DIAG=pular5   NAO executa as passadas elegiveis (o que o N2b-2 faria bloqueando a thread 3
//                      ate o proximo evento).
// BS_CRI_DIAG=repetir5 controle do pular5: executa duas vezes as passadas elegiveis.
// BS_CRI_DIAG=consumidor  negativo do B11: gasta trabalho extra quando o hash dos medidores do
//                      CRI (gravados a partir do TMU) e impar; um consumidor do TMU assim muda TN.
// BS_CRI_MEDIDORES=<arquivo>  B11: os medidores do CRI (0x8C1DD1C4-0x8C1DD23C), uma linha por
//                      VBlank; sozinho, liga o modo "contar".
// Os modos "pular" servem para provar (ou derrubar) que uma passada assim nao tem efeito
// observavel; o N2b-2 bloquearia a thread 3 nessa condicao.
// Precisa do build com slots (BS_MODS): a passada e chamada direto pela thread 3.
#include <cstdint>
#include <string>

namespace bs {

void        cri_diag_install();  // le BS_CRI_DIAG; sem a variavel nao faz nada
std::string cri_diag_summary();  // vazio se desligado

} // namespace bs
