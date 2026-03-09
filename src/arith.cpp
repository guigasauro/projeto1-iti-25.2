#include "arith.hpp"

ArithEncoder::ArithEncoder(BitWriter& w) : writer(w) {}

void ArithEncoder::encode(const SymProb& sp) {
    uint64_t range = high - low + 1;

    // Atualiza high e low proporcionalmente a SymProb
    high = low + (range * sp.high_num / sp.denom) - 1;
    low  = low + (range * sp.low_num  / sp.denom);
    
    rescale();
}

void ArithEncoder::rescale() {
    for (;;) {
        // Enquanto os 2 MSB de low e high forem iguais, emite o bit e desloca
        if (high < HALF) {
            emitBit(0);
        } else if (low >= HALF) {
            // Caso contrário, se low estiver no terceiro quarto e high no segundo quarto,
            // adia a emissão do bit e desloca ambos para a esquerda, "removendo" o terceiro quarto
            emitBit(1);
            low  -= HALF; high -= HALF;
        } else if (low >= FIRST_QTR && high < THIRD_QTR) {
            pending++;
            low  -= FIRST_QTR; high -= FIRST_QTR;
        } else break;
        low  <<= 1;
        high = (high << 1) | 1;
    }
}

void ArithEncoder::emitBit(int b) {
    // Chamada de I/O
    writer.writeBit(b);
    while (pending-- > 0) writer.writeBit(!b);
    pending = 0;
}

void ArithEncoder::flush() {
    // Emite o bit seguinte de low (0 ou 1) e os bits pendentes
    pending++;
    emitBit(low < FIRST_QTR ? 0 : 1);
    writer.flush();
}

ArithDecoder::ArithDecoder(BitReader& r) : reader(r) {
    // Inicializa value lendo os 32 primeiros bits do bitstream
    for (int i = 0; i < 32; i++)
        value = (value << 1) | reader.readBit();
}

uint64_t ArithDecoder::getCount(uint64_t denom) const {
    // Calcula o count correspondente ao value atual, proporcional ao intervalo [low, high)
    uint64_t range = high - low + 1;
    return ((value - low + 1) * denom - 1) / range;
}

void ArithDecoder::remove(const SymProb& sp) {
    uint64_t range = high - low + 1;

    // Atualiza high e low proporcionalmente a SymProb
    high  = low + (range * sp.high_num / sp.denom) - 1;
    low   = low + (range * sp.low_num  / sp.denom);
    rescale();
}

void ArithDecoder::rescale() {
    // Enquanto os 2 MSB de low e high forem iguais, lê o próximo bit e desloca
    for (;;) {
        if (high < HALF) {
            // shift
        } else if (low >= HALF) {
            // Caso contrário, se low estiver no terceiro quarto e high no segundo quarto,
            // adia a leitura do bit e desloca ambos para a esquerda, "removendo" o terceiro quarto
            low -= HALF; high -= HALF; value -= HALF;
        } else if (low >= FIRST_QTR && high < THIRD_QTR) {
            // cuida do caso em que low e high estão em quartos adjacentes, mas não no mesmo meio
            low -= FIRST_QTR; high -= FIRST_QTR; value -= FIRST_QTR;
        } else break;
        low   <<= 1;
        high   = (high  << 1) | 1;
        value  = (value << 1) | reader.readBit();
    }
}
