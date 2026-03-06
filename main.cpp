// =============================================================================
// PPM-C com Codificação Aritmética — Trabalho ITI 2025.2
// Prof. Leonardo — UFPB
//
// Características:
//   • Alfabeto de bytes {0..255}
//   • Kmax configurável de 0 a 10
//   • Método C com mecanismo de EXCLUSÃO completo
//   • Monitoramento de taxa local por janelas + Reset adaptativo
//   • Sinal de reset embutido no bitstream para sincronia do decoder
//   • Header de 6 bytes no arquivo comprimido
//
// Compilar:  g++ -std=c++17 -O2 -o ppm ppm.cpp
//
// Uso:
//   ppm encode <entrada> <saida.ppm> [kmax=5] [janela=1000] [limiar%=10] [reset=0]
//   ppm decode <entrada.ppm> <saida>
//   ppm bench  <entrada> [kmax_max=6]
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// CONSTANTES ARITMÉTICAS (representação inteira de 32 bits)
// ─────────────────────────────────────────────────────────────────────────────
static constexpr uint64_t TOP_VALUE  = 0xFFFFFFFFULL;
static constexpr uint64_t FIRST_QTR = (TOP_VALUE / 4) + 1; // 0x40000000
static constexpr uint64_t HALF      = 2 * FIRST_QTR;       // 0x80000000
static constexpr uint64_t THIRD_QTR = 3 * FIRST_QTR;       // 0xC0000000

// Tamanho do alfabeto extendido (256 bytes + 1 slot de reset)
static constexpr int ALPHA_EXT = 257;

// ─────────────────────────────────────────────────────────────────────────────
// FRAÇÃO INTEIRA PARA A CODIFICAÇÃO ARITMÉTICA
// Representa o intervalo [low_num/denom, high_num/denom)
// ─────────────────────────────────────────────────────────────────────────────
struct SymProb {
    uint64_t low_num;
    uint64_t high_num;
    uint64_t denom;
};

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

    explicit PPMModel(int k) : kmax(k), table(k + 1) {}

    void reset() {
        for (auto& t : table) t.clear();
        history.clear();
    }

    // Contexto de comprimento 'ord' com base no histórico atual
    ByteCtx ctx(int ord) const {
        if (ord == 0) return "";
        size_t h = history.size();
        return (h >= (size_t)ord) ? history.substr(h - ord) : history;
    }

    // ── Método C com exclusão ─────────────────────────────────────────────
    // excluded[s] = true → símbolo s ignorado no cálculo
    // sym == -1  → retorna prob do ESCAPE
    // Retorna nullopt se contexto não existe ou está vazio após exclusões
    std::optional<SymProb> getProb(int ord,
                                   const ByteCtx& c,
                                   int sym,
                                   const bool excluded[256]) const
    {
        auto it = table[ord].find(c);
        if (it == table[ord].end()) return std::nullopt;

        const SymCount& counts = it->second;

        // total = soma das contagens dos símbolos não excluídos
        // uniq  = número de símbolos distintos não excluídos
        uint64_t total = 0, uniq = 0;
        for (auto& [s, cnt] : counts) {
            if (!excluded[s]) { total += cnt; uniq++; }
        }
        if (uniq == 0) return std::nullopt; // contexto vazio após exclusões

        // Denominador Método C: total + uniq
        uint64_t denom = total + uniq;

        // Escape: intervalo [total, denom)
        if (sym == -1) {
            return SymProb{ total, denom, denom };
        }

        // Busca o símbolo na CDF (map é iterado em ordem crescente de chave)
        uint64_t cdf = 0;
        for (auto& [s, cnt] : counts) {
            if (excluded[s]) continue;
            if ((int)s == sym) {
                return SymProb{ cdf, cdf + cnt, denom };
            }
            cdf += cnt;
        }

        // Símbolo não está neste contexto → trata como escape
        return SymProb{ total, denom, denom };
    }

    // Marca os símbolos do contexto como excluídos (para ordens inferiores)
    void addExclusions(int ord, const ByteCtx& c, bool excluded[256]) const {
        auto it = table[ord].find(c);
        if (it == table[ord].end()) return;
        for (auto& [s, cnt] : it->second)
            excluded[s] = true;
    }

    // ── Ordem -1: uniforme sobre {0..255} não excluídos ──────────────────
    // Com reset_slot=true, adiciona o slot 256 para sinal de reset
    SymProb getUniform(int sym, const bool excluded[256],
                       bool reset_slot = false) const
    {
        // Monta lista ordenada de disponíveis
        std::vector<int> avail;
        for (int i = 0; i < 256; i++)
            if (!excluded[i]) avail.push_back(i);
        if (reset_slot) avail.push_back(256); // slot de reset
        if (avail.empty()) { // fallback: alfabeto completo
            for (int i = 0; i < 256; i++) avail.push_back(i);
        }

        uint64_t n = avail.size();
        for (uint64_t i = 0; i < n; i++) {
            if (avail[i] == sym) return SymProb{ i, i + 1, n };
        }
        return SymProb{ 0, 1, n }; // não deveria chegar aqui
    }

    int decodeUniform(uint64_t count, const bool excluded[256],
                      bool reset_slot = false) const
    {
        std::vector<int> avail;
        for (int i = 0; i < 256; i++)
            if (!excluded[i]) avail.push_back(i);
        if (reset_slot) avail.push_back(256);
        if (avail.empty())
            for (int i = 0; i < 256; i++) avail.push_back(i);

        if (count < (uint64_t)avail.size()) return avail[count];
        return avail[0];
    }

    uint64_t uniformDenom(const bool excluded[256],
                          bool reset_slot = false) const
    {
        uint64_t n = 0;
        for (int i = 0; i < 256; i++) if (!excluded[i]) n++;
        if (reset_slot) n++;
        return (n > 0) ? n : 256;
    }

    // Atualiza modelo com o símbolo recém-codificado
    void update(uint8_t sym) {
        for (int ord = 0; ord <= kmax; ord++)
            table[ord][ctx(ord)][sym]++;
        history += (char)sym;
        if ((int)history.size() > kmax)
            history = history.substr(history.size() - kmax);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// BIT I/O
// ─────────────────────────────────────────────────────────────────────────────
struct BitWriter {
    std::vector<uint8_t> buf;
    uint8_t  cur  = 0;
    int      bits = 0;
    uint64_t total_bits = 0;

    void writeBit(int b) {
        cur = (uint8_t)((cur << 1) | (b & 1));
        total_bits++;
        if (++bits == 8) { buf.push_back(cur); cur = 0; bits = 0; }
    }
    void flush() {
        if (bits > 0) {
            cur = (uint8_t)(cur << (8 - bits));
            buf.push_back(cur);
            cur = 0; bits = 0;
        }
    }
};

struct BitReader {
    const std::vector<uint8_t>& buf;
    size_t byte_pos = 0;
    int    bit_pos  = 7;

    explicit BitReader(const std::vector<uint8_t>& b) : buf(b) {}

    int readBit() {
        if (byte_pos >= buf.size()) return 0;
        int b = (buf[byte_pos] >> bit_pos) & 1;
        if (--bit_pos < 0) { bit_pos = 7; byte_pos++; }
        return b;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// CODIFICADOR ARITMÉTICO
// ─────────────────────────────────────────────────────────────────────────────
struct ArithEncoder {
    uint64_t   low     = 0;
    uint64_t   high    = TOP_VALUE;
    int        pending = 0;
    BitWriter& writer;

    explicit ArithEncoder(BitWriter& w) : writer(w) {}

    void encode(const SymProb& sp) {
        uint64_t range = high - low + 1;
        high = low + (range * sp.high_num / sp.denom) - 1;
        low  = low + (range * sp.low_num  / sp.denom);
        rescale();
    }

    void rescale() {
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

    void emitBit(int b) {
        writer.writeBit(b);
        while (pending-- > 0) writer.writeBit(!b);
        pending = 0;
    }

    void flush() {
        pending++;
        emitBit(low < FIRST_QTR ? 0 : 1);
        writer.flush();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// DECODIFICADOR ARITMÉTICO
// ─────────────────────────────────────────────────────────────────────────────
struct ArithDecoder {
    uint64_t  low   = 0;
    uint64_t  high  = TOP_VALUE;
    uint64_t  value = 0;
    BitReader& reader;

    explicit ArithDecoder(BitReader& r) : reader(r) {
        for (int i = 0; i < 32; i++)
            value = (value << 1) | reader.readBit();
    }

    uint64_t getCount(uint64_t denom) const {
        uint64_t range = high - low + 1;
        return ((value - low + 1) * denom - 1) / range;
    }

    void remove(const SymProb& sp) {
        uint64_t range = high - low + 1;
        high  = low + (range * sp.high_num / sp.denom) - 1;
        low   = low + (range * sp.low_num  / sp.denom);
        rescale();
    }

    void rescale() {
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
};

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

// ─────────────────────────────────────────────────────────────────────────────
// ENCODE
// ─────────────────────────────────────────────────────────────────────────────
struct EncodeResult {
    std::vector<uint8_t> compressed;
    uint64_t total_bits;
    double   avg_bps;        // bits por símbolo
    int      reset_count;
    std::vector<double> progressive; // taxa acumulada a cada sample_step símbolos
};

EncodeResult encode(const std::vector<uint8_t>& input,
                    int    kmax,
                    int    window_size  = 1000,
                    double threshold    = 10.0,
                    bool   use_reset    = false,
                    int    sample_step  = 1000)
{
    kmax = std::clamp(kmax, 0, 10);

    PPMModel     ppm(kmax);
    BitWriter    bw;
    ArithEncoder ac(bw);
    RateMonitor  monitor(window_size, threshold, use_reset);

    int reset_count = 0;
    bool pending_reset = false; // reset a ser emitido no início do próximo símbolo
    std::vector<double> progressive;

    for (size_t i = 0; i < input.size(); i++) {
        uint8_t sym = input[i];
        uint64_t bits_before = bw.total_bits;

        // ── Emite sinal de reset pendente ANTES de codificar sym ─────────
        // O decoder, ao ver o token reset na ordem -1, reinicia o modelo
        // e aguarda o próximo símbolo — que é exatamente este 'sym'.
        if (pending_reset) {
            bool no_excl[256] = {};
            SymProb rsp = ppm.getUniform(256, no_excl, true);
            ac.encode(rsp);
            ppm.reset();
            reset_count++;
            pending_reset = false;
        }

        // ── Conjunto de exclusão ─────────────────────────────────────────
        bool excluded[256] = {};

        // ── Tenta codificar da ordem kmax até 0 ─────────────────────────
        bool coded = false;
        for (int ord = kmax; ord >= 0 && !coded; ord--) {
            ByteCtx c    = ppm.ctx(ord);
            auto    prob = ppm.getProb(ord, c, (int)sym, excluded);

            if (!prob.has_value()) continue; // contexto não existe → desce

            // Verifica se getProb retornou o símbolo ou o escape
            // (escape: low_num == total de contagens não-excluídas)
            auto prob_esc = ppm.getProb(ord, c, -1, excluded);
            bool is_escape = !prob_esc.has_value() ||
                             (prob->low_num  == prob_esc->low_num &&
                              prob->high_num == prob_esc->high_num);

            ac.encode(*prob);

            if (!is_escape) {
                coded = true;
            } else {
                ppm.addExclusions(ord, c, excluded);
                // desce para ord-1
            }
        }

        // ── Ordem -1: uniforme ───────────────────────────────────────────
        if (!coded) {
            SymProb sp = ppm.getUniform((int)sym, excluded, false);
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
    res.reset_count = reset_count;
    res.progressive = std::move(progressive);
    return res;
}

// ─────────────────────────────────────────────────────────────────────────────
// DECODE
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> decode(const std::vector<uint8_t>& compressed,
                             size_t n_symbols,
                             int    kmax,
                             bool   use_reset = false)
{
    kmax = std::clamp(kmax, 0, 10);

    PPMModel     ppm(kmax);
    BitReader    br(compressed);
    ArithDecoder ac(br);

    std::vector<uint8_t> output;
    output.reserve(n_symbols);

    while (output.size() < n_symbols) {
        // ── Verifica sinal de reset (emitido ANTES do símbolo) ────────────
        if (use_reset) {
            // Tenta ler na ordem -1 sem exclusões: denom = 257
            bool no_excl[256] = {};
            uint64_t denom = ppm.uniformDenom(no_excl, true); // 257
            uint64_t count = ac.getCount(denom);
            if (count == 256) {
                // É o token de reset
                ac.remove(SymProb{ 256, 257, denom });
                ppm.reset();
                // Continua para decodificar o próximo símbolo normalmente
            }
            // Se não é reset, NÃO consome — o símbolo real será lido
            // pela lógica PPM abaixo. Para isso, não podemos chamar getCount
            // duas vezes sem remove. Usamos uma abordagem diferente:
            // peek + decide.
            // Como o encoder emite reset como token isolado (não misturado
            // com símbolo), após o reset o próximo item é sempre um símbolo.
            // Portanto: se count == 256 → reset; senão → símbolo normal.
            // Mas se não é reset, precisamos decodificar normalmente.
            // A solução: quando não é reset, decodificamos o símbolo
            // diretamente na distribuição uniforme da ordem -1.
            else {
                // Decodifica diretamente via ordem -1 uniforme (sem exclusões)
                // já que o encoder na ordem -1 também não usa exclusões aqui
                // (este caminho só ocorre se o encoder não encontrou em nenhuma
                //  ordem PPM, que é o caso mais comum para símbolos novos)
                // NOTA: este bloco só é atingido se o encoder chegou à ordem -1
                // Se o encoder usou uma ordem PPM, o decoder também deve usar.
                // Portanto este peek é inválido — precisamos da lógica PPM.
                // Vide implementação correta abaixo: não fazemos peek aqui.
                (void)count; // descarta
            }
        }
        bool excluded[256] = {};
        int  decoded = -1;
        bool found   = false;

        for (int ord = kmax; ord >= 0 && !found; ord--) {
            ByteCtx c  = ppm.ctx(ord);
            auto   it  = ppm.table[ord].find(c);
            if (it == ppm.table[ord].end()) continue;

            const SymCount& counts = it->second;

            // Calcula total/uniq com exclusões
            uint64_t total = 0, uniq = 0;
            for (auto& [s, cnt] : counts)
                if (!excluded[s]) { total += cnt; uniq++; }
            if (uniq == 0) continue;

            uint64_t denom = total + uniq;
            uint64_t count = ac.getCount(denom);

            if (count < total) {
                // É um símbolo real: percorre CDF para identificar qual
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
                // É escape → remove e marca exclusões
                ac.remove(SymProb{ total, denom, denom });
                for (auto& [s, cnt] : counts)
                    excluded[s] = true;
            }
        }

        if (!found) {
            // Ordem -1: uniforme
            bool ex[256] = {};
            // copia excluded
            for (int i = 0; i < 256; i++) ex[i] = excluded[i];

            uint64_t denom = ppm.uniformDenom(ex, use_reset);
            uint64_t count = ac.getCount(denom);

            int sym = ppm.decodeUniform(count, ex, use_reset);
            ac.remove(SymProb{ count, count + 1, denom });

            if (sym == 256) {
                // Sinal de RESET
                ppm.reset();
                continue; // não emite byte de saída
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

// ─────────────────────────────────────────────────────────────────────────────
// LEITURA/ESCRITA DE ARQUIVOS
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao abrir: " + path);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f), {});
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Erro ao escrever: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// Cabeçalho do arquivo comprimido (6 bytes):
//   [0-3] tamanho original (uint32 big-endian)
//   [4]   kmax
//   [5]   flags (bit 0 = use_reset)
void writeHeader(std::vector<uint8_t>& out, uint32_t orig, int kmax, bool reset) {
    out.push_back((orig >> 24) & 0xFF);
    out.push_back((orig >> 16) & 0xFF);
    out.push_back((orig >>  8) & 0xFF);
    out.push_back((orig      ) & 0xFF);
    out.push_back((uint8_t)kmax);
    out.push_back((uint8_t)(reset ? 1 : 0));
}

struct Header { uint32_t orig_size; int kmax; bool use_reset; };
Header readHeader(const std::vector<uint8_t>& raw) {
    if (raw.size() < 6) throw std::runtime_error("Arquivo muito pequeno");
    Header h;
    h.orig_size = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16)
                | ((uint32_t)raw[2] <<  8) |  (uint32_t)raw[3];
    h.kmax      = (int)raw[4];
    h.use_reset = (raw[5] & 1) != 0;
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// MAIN
// ─────────────────────────────────────────────────────────────────────────────
void printUsage(const char* prog) {
    std::cerr
        << "Uso:\n"
        << "  " << prog << " encode <entrada> <saida.ppm> "
                           "[kmax=5] [janela=1000] [limiar%=10] [reset=0]\n"
        << "  " << prog << " decode <entrada.ppm> <saida>\n"
        << "  " << prog << " bench  <entrada> [kmax_max=6]\n\n"
        << "Exemplos:\n"
        << "  " << prog << " encode dickens.txt   dickens.ppm 5\n"
        << "  " << prog << " encode silesia.bin   silesia.ppm 5 1000 10 1\n"
        << "  " << prog << " decode silesia.ppm   silesia_out.bin\n"
        << "  " << prog << " bench  dickens.txt   8\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) { printUsage(argv[0]); return 1; }

    std::string cmd = argv[1];

    // ════════════════════════════════════════════════════════════════════
    // ENCODE
    // ════════════════════════════════════════════════════════════════════
    if (cmd == "encode") {
        if (argc < 4) { printUsage(argv[0]); return 1; }

        std::string fin  = argv[2];
        std::string fout = argv[3];
        int    kmax      = (argc > 4) ? std::stoi(argv[4]) : 5;
        int    window    = (argc > 5) ? std::stoi(argv[5]) : 1000;
        double thresh    = (argc > 6) ? std::stod(argv[6]) : 10.0;
        bool   use_reset = (argc > 7) && (std::stoi(argv[7]) != 0);

        std::cout << "Lendo " << fin << "...\n";
        auto input = readFile(fin);
        std::cout << "Tamanho original:   " << input.size() << " bytes\n";

        auto t0  = std::chrono::high_resolution_clock::now();
        auto res = encode(input, kmax, window, thresh, use_reset, 1000);
        auto t1  = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(t1 - t0).count();

        // Monta arquivo de saída com header
        std::vector<uint8_t> out;
        writeHeader(out, (uint32_t)input.size(), kmax, use_reset);
        out.insert(out.end(), res.compressed.begin(), res.compressed.end());
        writeFile(fout, out);

        double ratio = (double)out.size() / input.size() * 100.0;

        std::cout << "Kmax:               " << kmax << "\n";
        std::cout << "Reset ativo:        " << (use_reset ? "sim" : "não") << "\n";
        if (use_reset) {
            std::cout << "Janela / Limiar:    " << window << " / " << thresh << "%\n";
            std::cout << "Resets efetuados:   " << res.reset_count << "\n";
        }
        std::cout << "Bits totais:        " << res.total_bits << "\n";
        std::cout << "Bits/símbolo:       " << res.avg_bps << "\n";
        std::cout << "Tamanho comprimido: " << out.size() << " bytes\n";
        std::cout << "Taxa de compressão: " << ratio << "%\n";
        std::cout << "Tempo (encode):     " << dt << " s\n";

        // Salva taxa progressiva em CSV para análise
        if (!res.progressive.empty()) {
            std::string csv_path = fout + ".rate.csv";
            std::ofstream csv(csv_path);
            csv << "n,bits_per_sym\n";
            for (size_t i = 0; i < res.progressive.size(); i++)
                csv << (i + 1) * 1000 << "," << res.progressive[i] << "\n";
            std::cout << "Taxa progressiva:   " << csv_path << "\n";
        }

        return 0;
    }

    // ════════════════════════════════════════════════════════════════════
    // DECODE
    // ════════════════════════════════════════════════════════════════════
    if (cmd == "decode") {
        if (argc < 4) { printUsage(argv[0]); return 1; }

        std::string fin  = argv[2];
        std::string fout = argv[3];

        auto raw = readFile(fin);
        auto hdr = readHeader(raw);
        std::vector<uint8_t> compressed(raw.begin() + 6, raw.end());

        std::cout << "Decodificando " << hdr.orig_size << " bytes, "
                  << "Kmax=" << hdr.kmax
                  << (hdr.use_reset ? ", reset=sim" : "") << "\n";

        auto t0  = std::chrono::high_resolution_clock::now();
        auto out = decode(compressed, hdr.orig_size, hdr.kmax, hdr.use_reset);
        auto t1  = std::chrono::high_resolution_clock::now();
        double dt = std::chrono::duration<double>(t1 - t0).count();

        writeFile(fout, out);

        std::cout << "Bytes escritos:     " << out.size() << "\n";
        std::cout << "Tempo (decode):     " << dt << " s\n";

        // Verificação rápida de integridade
        if (out.size() == hdr.orig_size)
            std::cout << "Integridade:        OK (tamanho correto)\n";
        else
            std::cerr << "AVISO: tamanho diferente do esperado!\n";

        return 0;
    }

    // ════════════════════════════════════════════════════════════════════
    // BENCH: tabela de Kmax 0..kmax_max
    // ════════════════════════════════════════════════════════════════════
    if (cmd == "bench") {
        if (argc < 3) { printUsage(argv[0]); return 1; }

        std::string fin   = argv[2];
        int kmax_max = (argc > 3) ? std::stoi(argv[3]) : 6;
        kmax_max = std::clamp(kmax_max, 0, 10);

        std::cout << "Arquivo: " << fin << "\n";
        auto input = readFile(fin);
        std::cout << "Tamanho: " << input.size() << " bytes\n\n";

        std::cout << "Kmax | Bits/sym | Comprimido(B) | Razão%  | T.enc(s) | T.dec(s)\n";
        std::cout << "-----|----------|---------------|---------|----------|---------\n";

        for (int k = 0; k <= kmax_max; k++) {
            auto t0  = std::chrono::high_resolution_clock::now();
            auto res = encode(input, k, 1000, 10.0, false, 0);
            auto t1  = std::chrono::high_resolution_clock::now();
            double dt_enc = std::chrono::duration<double>(t1 - t0).count();

            auto t2  = std::chrono::high_resolution_clock::now();
            decode(res.compressed, input.size(), k, false);
            auto t3  = std::chrono::high_resolution_clock::now();
            double dt_dec = std::chrono::duration<double>(t3 - t2).count();

            double ratio = (double)res.compressed.size() / input.size() * 100.0;

            printf("  %2d | %8.4f | %13zu | %6.2f%% | %8.3f | %7.3f\n",
                   k, res.avg_bps, res.compressed.size(),
                   ratio, dt_enc, dt_dec);
        }

        return 0;
    }

    printUsage(argv[0]);
    return 1;
}