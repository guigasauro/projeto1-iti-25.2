#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// MONITOR DE TAXA LOCAL
// ─────────────────────────────────────────────────────────────────────────────
struct RateMonitor {
    int    window_size;
    double threshold_pct; // % de piora para disparar reset
    bool   enabled;

    uint64_t bits_prev = 0, syms_prev = 0;
    uint64_t bits_curr = 0, syms_curr = 0;
    int      count     = 0;

    RateMonitor(int j, double pct, bool en)
        : window_size(j), threshold_pct(pct), enabled(en) {}

    // Retorna true se deve disparar reset
    bool feed(uint64_t bits_used) {
        if (!enabled) return false;
        bits_curr += bits_used;
        syms_curr++;
        if (++count < window_size) return false;

        bool should_reset = false;
        if (syms_prev > 0) {
            double r_prev = (double)bits_prev / syms_prev;
            double r_curr = (double)bits_curr / syms_curr;
            if (r_curr > r_prev * (1.0 + threshold_pct / 100.0))
                should_reset = true;
        }
        // Rotaciona
        bits_prev = bits_curr; syms_prev = syms_curr;
        bits_curr = 0;         syms_curr = 0;
        count = 0;
        return should_reset;
    }
};
