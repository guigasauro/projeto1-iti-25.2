#include "fileio.hpp"
#include <fstream>
#include <stdexcept>

std::vector<uint8_t> readFile(const std::string& path) {
    // Lê todo o conteúdo do arquivo de uma vez em modo binário
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao abrir: " + path);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f), {});
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    // Escreve o vetor inteiro no arquivo em modo binário
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao escrever: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void writeHeader(std::vector<uint8_t>& out, uint32_t orig, int kmax, bool reset) {
    // Bytes 0-3: tamanho original em big-endian
    out.push_back((orig >> 24) & 0xFF);
    out.push_back((orig >> 16) & 0xFF);
    out.push_back((orig >>  8) & 0xFF);
    out.push_back((orig      ) & 0xFF);
    // Byte 4: kmax; byte 5: flags (bit 0 = use_reset)
    out.push_back((uint8_t)kmax);
    out.push_back((uint8_t)(reset ? 1 : 0));
}

Header readHeader(const std::vector<uint8_t>& raw) {
    if (raw.size() < 6) throw std::runtime_error("Arquivo muito pequeno");
    Header h;
    // Reconstrói o tamanho original a partir dos 4 bytes big-endian
    h.orig_size = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
                | ((uint32_t)raw[2] <<  8) |  (uint32_t)raw[3];
    h.kmax      = (int)raw[4];
    h.use_reset = (raw[5] & 1) != 0;
    return h;
}
