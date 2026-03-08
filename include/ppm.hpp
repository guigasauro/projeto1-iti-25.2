#pragma once
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include "arith.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// MODELO PPM-C COM EXCLUSÃO
// ─────────────────────────────────────────────────────────────────────────────
using ByteCtx    = std::string;
using SymCount   = std::map<uint8_t, uint32_t>;   // símbolo → contagem
using ContextMap = std::unordered_map<ByteCtx, SymCount>;

struct PPMModel {
    int kmax;
    std::vector<ContextMap> table; // table[ord][ctx][sym] = contagem
    ByteCtx history;               // últimos kmax bytes

    explicit PPMModel(int k);

    void reset();

    // Contexto de comprimento 'ord' com base no histórico atual
    ByteCtx ctx(int ord) const;

    // Método C com exclusão.
    // excluded[s] = true → símbolo s ignorado.
    // sym == -1  → retorna prob do ESCAPE.
    // Retorna nullopt se contexto não existe ou está vazio após exclusões.
    std::optional<SymProb> getProb(int ord,
                                   const ByteCtx& c,
                                   int sym,
                                   const bool excluded[256]) const;

    // Marca os símbolos do contexto como excluídos (para ordens inferiores)
    void addExclusions(int ord, const ByteCtx& c, bool excluded[256]) const;

    // Ordem -1: uniforme sobre {0..255} não excluídos.
    // Com reset_slot=true, adiciona o slot 256 para sinal de reset.
    SymProb getUniform(int sym, const bool excluded[256],
                       bool reset_slot = false) const;

    int decodeUniform(uint64_t count, const bool excluded[256],
                      bool reset_slot = false) const;

    uint64_t uniformDenom(const bool excluded[256],
                          bool reset_slot = false) const;

    // Atualiza modelo com o símbolo recém-codificado
    void update(uint8_t sym);
};
