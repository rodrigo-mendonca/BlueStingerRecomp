#pragma once
// Teste de contrato do servidor pd contra o legado (N3-0). Desligado por padrao.
//
//   BS_PAD_CONTRATO=<arquivo>           envolve o servidor 8C15C4E6 (e 8C08F4B6, para ver a condicao
//                                       bruta de cada unidade) e confere cada passada com
//                                       pad_contract; no fim grava contagens, cobertura e divergencias
//   BS_PAD_CONTRATO_MUTACAO=<lista>     negativos: troca_ab, gatilho127, old_nativo, info_sem_conectado;
//                                       campos_nao_usados: lixo em r, l, x2, y2, old, name e extend
//                                       das unidades principais depois de cada passada (prova de uso)
//   BS_PAD_MODO=<n>                     forca o modo do servidor (8C2D8EB0) antes de cada passada
//
// Exige slots (build com mods): sem eles as chamadas diretas nao passariam pela sonda.
namespace bs::transitional::pad {

void probe_install();  // le o ambiente; nada sem BS_PAD_CONTRATO
void probe_finish();   // grava o relatorio (tambem no atexit)

} // namespace bs::transitional::pad
