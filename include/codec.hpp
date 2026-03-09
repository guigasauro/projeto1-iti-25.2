#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ENCODE / DECODE
// ─────────────────────────────────────────────────────────────────────────────
struct EncodeResult {
    std::vector<uint8_t> compressed;
    uint64_t total_bits;
    double   avg_bps;        // bits por símbolo
    int      reset_count;
    std::vector<double> progressive;      // taxa acumulada a cada sample_step símbolos
    std::vector<size_t> reset_positions;  // posição n (em símbolos) de cada reset
};

EncodeResult encode(const std::vector<uint8_t>& input,
                    int    kmax,
                    int    window_size  = 1000,
                    double threshold    = 10.0,
                    bool   use_reset    = false,
                    int    sample_step  = 1000);

std::vector<uint8_t> decode(const std::vector<uint8_t>& compressed,
                             size_t n_symbols,
                             int    kmax,
                             bool   use_reset = false);
