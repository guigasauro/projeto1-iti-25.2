#include "codec.hpp"
#include "ppm.hpp"
#include "arith.hpp"
#include "monitor.hpp"
#include <algorithm>

EncodeResult encode(const std::vector<uint8_t>& input,
                    int    kmax,
                    int    window_size,
                    double threshold,
                    bool   use_reset,
                    int    sample_step)
{
    kmax = std::clamp(kmax, 0, 10);

    PPMModel     ppm(kmax);
    BitWriter    bw;
    ArithEncoder ac(bw);
    RateMonitor  monitor(window_size, threshold, use_reset);

    int reset_count = 0;
    bool pending_reset = false; // reset a ser emitido no início do próximo símbolo
    std::vector<double> progressive;
    std::vector<size_t> reset_positions;

    for (size_t i = 0; i < input.size(); i++) {
        uint8_t sym = input[i];
        uint64_t bits_before = bw.total_bits;

        // ── Emite sinal de reset pendente ANTES de codificar sym ─────────
        // O token de reset (símbolo 256) percorre a mesma cascade PPM que
        // qualquer símbolo normal, acumulando exclusões em cada ordem.
        // Assim encoder e decoder usam exatamente o mesmo denom na ordem -1.
        if (pending_reset) {
            bool excl_r[256] = {};
            for (int ord = kmax; ord >= 0; ord--) {
                ByteCtx c     = ppm.ctx(ord);
                auto prob_esc = ppm.getProb(ord, c, -1, excl_r);
                if (!prob_esc.has_value()) continue;
                ac.encode(*prob_esc);
                ppm.addExclusions(ord, c, excl_r);
            }
            SymProb rsp = ppm.getUniform(256, excl_r, true);
            ac.encode(rsp);
            ppm.reset();
            reset_count++;
            reset_positions.push_back(i); // posição do símbolo que vem logo após o reset
            pending_reset = false;
        }

        // ── Conjunto de exclusão ─────────────────────────────────────────
        bool excluded[256] = {};

        // ── Tenta codificar da ordem kmax até 0 ─────────────────────────
        bool coded = false;
        for (int ord = kmax; ord >= 0 && !coded; ord--) {
            ByteCtx c    = ppm.ctx(ord);
            auto    prob = ppm.getProb(ord, c, (int)sym, excluded);

            if (!prob.has_value()) continue; // contexto inexistente → desce para ordem inferior

            auto prob_esc = ppm.getProb(ord, c, -1, excluded);
            bool is_escape = !prob_esc.has_value() ||
                             (prob->low_num  == prob_esc->low_num &&
                              prob->high_num == prob_esc->high_num);

            ac.encode(*prob);

            if (!is_escape) {
                coded = true;
            } else {
                ppm.addExclusions(ord, c, excluded);
            }
        }

        // ── Ordem -1: uniforme ───────────────────────────────────────────
        // use_reset=true reserva o slot 256, mantendo o denom igual ao decoder
        if (!coded) {
            SymProb sp = ppm.getUniform((int)sym, excluded, use_reset);
            ac.encode(sp);
        }

        ppm.update(sym);

        // ── Progressive rate ─────────────────────────────────────────────
        if (sample_step > 0 && (int)(i + 1) % sample_step == 0)
            progressive.push_back((double)bw.total_bits / (i + 1));

        // ── Monitor de taxa → agenda reset para próximo símbolo ──────────
        uint64_t bits_used = bw.total_bits - bits_before;
        if (use_reset && !pending_reset && monitor.feed(bits_used))
            pending_reset = true;
    }

    ac.flush();

    EncodeResult res;
    res.compressed  = std::move(bw.buf);
    res.total_bits  = bw.total_bits;
    res.avg_bps     = input.empty() ? 0.0 : (double)bw.total_bits / input.size();
    res.reset_count     = reset_count;
    res.progressive     = std::move(progressive);
    res.reset_positions = std::move(reset_positions);
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// DECODE
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> decode(const std::vector<uint8_t>& compressed,
                             size_t n_symbols,
                             int    kmax,
                             bool   use_reset)
{
    kmax = std::clamp(kmax, 0, 10);

    PPMModel     ppm(kmax);
    BitReader    br(compressed);
    ArithDecoder ac(br);

    std::vector<uint8_t> output;
    output.reserve(n_symbols);

    while (output.size() < n_symbols) {
        // O token de reset é decodificado naturalmente pela cascade PPM:
        // quando o decoder chega à ordem -1 e obtém símbolo 256, reseta o
        // modelo e continua. Não há peek — encoder e decoder usam o mesmo denom.

        bool excluded[256] = {};
        int  decoded = -1;
        bool found   = false;

        for (int ord = kmax; ord >= 0 && !found; ord--) {
            ByteCtx c  = ppm.ctx(ord);
            auto   it  = ppm.table[ord].find(c);
            if (it == ppm.table[ord].end()) continue;

            const SymCount& counts = it->second;

            uint64_t total = 0, uniq = 0;
            for (auto& [s, cnt] : counts)
                if (!excluded[s]) { total += cnt; uniq++; }
            if (uniq == 0) continue;

            uint64_t denom = total + uniq;
            uint64_t count = ac.getCount(denom);

            if (count < total) {
                uint64_t cdf = 0;
                for (auto& [s, cnt] : counts) {
                    if (excluded[s]) continue;
                    if (count < cdf + cnt) {
                        decoded = (int)s;
                        ac.remove(SymProb{ cdf, cdf + cnt, denom });
                        found = true;
                        break;
                    }
                    cdf += cnt;
                }
            } else {
                ac.remove(SymProb{ total, denom, denom });
                for (auto& [s, cnt] : counts)
                    excluded[s] = true;
            }
        }

        if (!found) {
            // Ordem -1: uniforme sobre símbolos não excluídos
            bool ex[256] = {};
            for (int i = 0; i < 256; i++) ex[i] = excluded[i];

            uint64_t denom = ppm.uniformDenom(ex, use_reset);
            uint64_t count = ac.getCount(denom);

            int sym = ppm.decodeUniform(count, ex, use_reset);
            ac.remove(SymProb{ count, count + 1, denom });

            // Token 256 sinaliza reset: reinicia o modelo e não emite byte
            if (sym == 256) {
                ppm.reset();
                continue;
            }
            decoded = sym;
        }

        if (decoded >= 0 && decoded < 256) {
            output.push_back((uint8_t)decoded);
            ppm.update((uint8_t)decoded);
        }
    }

    return output;
}
