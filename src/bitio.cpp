#include "bitio.hpp"

void BitWriter::writeBit(int b) {
    // Empacota os bits da MSB para LSB dentro de cur
    cur = (uint8_t)((cur << 1) | (b & 1));
    total_bits++;
    // Quando o byte está completo, empurra para o buffer e reinicia
    if (++bits == 8) { buf.push_back(cur); cur = 0; bits = 0; }
}

void BitWriter::flush() {
    // Alinha o último byte parcial com zeros à direita e empurra
    if (bits > 0) {
        cur = (uint8_t)(cur << (8 - bits));
        buf.push_back(cur);
        cur = 0; bits = 0;
    }
}

int BitReader::readBit() {
    // Bits esgotados → retorna 0 para completar o decodificador aritmético
    if (byte_pos >= buf.size()) return 0;
    // Lê do MSB para o LSB dentro de cada byte
    int b = (buf[byte_pos] >> bit_pos) & 1;
    // Avança para o próximo byte quando bit_pos zera
    if (--bit_pos < 0) { bit_pos = 7; byte_pos++; }
    return b;
}
