#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// BIT I/O
struct BitWriter {
    std::vector<uint8_t> buf;
    uint8_t  cur  = 0;
    int      bits = 0;
    uint64_t total_bits = 0;

    void writeBit(int b);
    void flush();
};

struct BitReader {
    const std::vector<uint8_t>& buf;
    size_t byte_pos = 0;
    int    bit_pos  = 7;

    explicit BitReader(const std::vector<uint8_t>& b) : buf(b) {}

    int readBit();
};
