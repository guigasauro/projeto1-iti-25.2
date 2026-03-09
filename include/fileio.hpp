#pragma once
#include <string>
#include <vector>
#include <cstdint>

// LEITURA/ESCRITA DE ARQUIVOS
std::vector<uint8_t> readFile(const std::string& path);
void writeFile(const std::string& path, const std::vector<uint8_t>& data);

// Cabeçalho do arquivo comprimido (6 bytes):
//   [0-3] tamanho original (uint32 big-endian)
//   [4]   kmax
//   [5]   flags (bit 0 = use_reset)
void writeHeader(std::vector<uint8_t>& out, uint32_t orig, int kmax, bool reset);

struct Header { uint32_t orig_size; int kmax; bool use_reset; };
Header readHeader(const std::vector<uint8_t>& raw);
