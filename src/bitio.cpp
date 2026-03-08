#include "bitio.hpp"

void BitWriter::writeBit(int b) {
    cur = (uint8_t)((cur << 1) | (b & 1));
    total_bits++;
    if (++bits == 8) { buf.push_back(cur); cur = 0; bits = 0; }
}

void BitWriter::flush() {
    if (bits > 0) {
        cur = (uint8_t)(cur << (8 - bits));
        buf.push_back(cur);
        cur = 0; bits = 0;
    }
}

int BitReader::readBit() {
    if (byte_pos >= buf.size()) return 0;
    int b = (buf[byte_pos] >> bit_pos) & 1;
    if (--bit_pos < 0) { bit_pos = 7; byte_pos++; }
    return b;
}
