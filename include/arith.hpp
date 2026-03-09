#pragma once
#include <cstdint>
#include "constants.hpp"
#include "bitio.hpp"

// FRAÇÃO INTEIRA PARA A CODIFICAÇÃO ARITMÉTICA
// Representa o intervalo [low_num/denom, high_num/denom)
struct SymProb {
    uint64_t low_num;
    uint64_t high_num;
    uint64_t denom;
};

// CODIFICADOR ARITMÉTICO
struct ArithEncoder {
    uint64_t   low     = 0;
    uint64_t   high    = TOP_VALUE;
    int        pending = 0;
    BitWriter& writer;

    explicit ArithEncoder(BitWriter& w);

    void encode(const SymProb& sp);
    void flush();

private:
    void rescale();
    void emitBit(int b);
};

// DECODIFICADOR ARITMÉTICO
struct ArithDecoder {
    uint64_t   low   = 0;
    uint64_t   high  = TOP_VALUE;
    uint64_t   value = 0;
    BitReader& reader;

    explicit ArithDecoder(BitReader& r);

    uint64_t getCount(uint64_t denom) const;
    void remove(const SymProb& sp);

private:
    void rescale();
};
