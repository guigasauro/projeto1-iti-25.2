#include "arith.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// ArithEncoder
// ─────────────────────────────────────────────────────────────────────────────
ArithEncoder::ArithEncoder(BitWriter& w) : writer(w) {}

void ArithEncoder::encode(const SymProb& sp) {
    uint64_t range = high - low + 1;
    high = low + (range * sp.high_num / sp.denom) - 1;
    low  = low + (range * sp.low_num  / sp.denom);
    rescale();
}

void ArithEncoder::rescale() {
    for (;;) {
        if (high < HALF) {
            emitBit(0);
        } else if (low >= HALF) {
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
    writer.writeBit(b);
    while (pending-- > 0) writer.writeBit(!b);
    pending = 0;
}

void ArithEncoder::flush() {
    pending++;
    emitBit(low < FIRST_QTR ? 0 : 1);
    writer.flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// ArithDecoder
// ─────────────────────────────────────────────────────────────────────────────
ArithDecoder::ArithDecoder(BitReader& r) : reader(r) {
    for (int i = 0; i < 32; i++)
        value = (value << 1) | reader.readBit();
}

uint64_t ArithDecoder::getCount(uint64_t denom) const {
    uint64_t range = high - low + 1;
    return ((value - low + 1) * denom - 1) / range;
}

void ArithDecoder::remove(const SymProb& sp) {
    uint64_t range = high - low + 1;
    high  = low + (range * sp.high_num / sp.denom) - 1;
    low   = low + (range * sp.low_num  / sp.denom);
    rescale();
}

void ArithDecoder::rescale() {
    for (;;) {
        if (high < HALF) {
            // shift
        } else if (low >= HALF) {
            low -= HALF; high -= HALF; value -= HALF;
        } else if (low >= FIRST_QTR && high < THIRD_QTR) {
            low -= FIRST_QTR; high -= FIRST_QTR; value -= FIRST_QTR;
        } else break;
        low   <<= 1;
        high   = (high  << 1) | 1;
        value  = (value << 1) | reader.readBit();
    }
}
