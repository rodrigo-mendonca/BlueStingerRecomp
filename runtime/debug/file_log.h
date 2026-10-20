#pragma once
// N1: registro normalizado das leituras de recursos (contrato do portao P1, docs/N1-PLANO.md).
//
// BS_FILE_LOG=<arquivo.tsv> grava, em ordem, cada bloco de 2 048 bytes de dado de arquivo que
// chega ao jogo: (arquivo logico, deslocamento, SHA-256 do bloco recortado no fim do arquivo).
// O provedor legado registra no GD-ROM HLE (setor -> arquivo); o nativo, no adaptador gdFs.
// Setores que nao sao de arquivo (TOC, descritores, diretorios) entram como "outro".
#include <cstddef>
#include <cstdint>
#include <string>

namespace bs {

extern bool g_file_log;

// `data` tem `n` bytes do arquivo a partir de `offset` (n <= 2 048, ja recortado no fim).
// `source`: "gdrom" (setores do GD-ROM HLE, legado) ou "nativo" (adaptador gdFs).
void file_log_chunk(const char* source, const std::string& logical, uint64_t offset, const uint8_t* data, size_t n);
void file_log_other(const char* source, const char* what, uint32_t where);

void        file_log_init(const std::string& path);  // vazio desliga
bool        file_log_flush();
std::string file_log_dump();
// Ultima sala lida (arquivo R*.BIN, sem a pasta), "" se nenhuma: o titulo do modo espectador.
std::string file_log_last_room();

} // namespace bs
