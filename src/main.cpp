//
// Compilar:  make   (ou: g++ -std=c++17 -O2 -o ppm main.cpp bitio.cpp arith.cpp ppm.cpp codec.cpp fileio.cpp)
//
// Uso:
//   ppm encode <entrada> <saida.ppm> [kmax=5] [janela=1000] [limiar%=10] [reset=0]
//   ppm decode <entrada.ppm> <saida>
//   ppm bench  <entrada> [kmax_max=6] [reset=0] [janela=1000] [limiar%=10]
// =============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <cstdio>
#include <algorithm>

#include "codec.hpp"
#include "fileio.hpp"

void printUsage(const char* prog) {
    std::cerr
        << "Uso:\n"
        << "  " << prog << " encode <entrada> <saida.ppm> "
                           "[kmax=5] [janela=1000] [limiar%=10] [reset=0]\n"
        << "  " << prog << " decode <entrada.ppm> <saida>\n"
        << "  " << prog << " bench  <entrada> [kmax_max=6] [reset=0] [janela=1000] [limiar%=10]\n\n"
        << "Exemplos:\n"
        << "  " << prog << " encode dickens.txt   dickens.ppm 5\n"
        << "  " << prog << " encode silesia.bin   silesia.ppm 5 1000 10 1\n"
        << "  " << prog << " decode silesia.ppm   silesia_out.bin\n"
        << "  " << prog << " bench  dickens.txt   8\n"
        << "  " << prog << " bench  silesia_concat 6 1 1000 10\n";
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

        // Prefixa o bitstream com o cabeçalho de 6 bytes e grava o arquivo
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

        // Salva taxa acumulada amostrada a cada 1000 símbolos
        if (!res.progressive.empty()) {
            std::string csv_path = fout + ".rate.csv";
            std::ofstream csv(csv_path);
            csv << "n,bits_per_sym\n";
            for (size_t i = 0; i < res.progressive.size(); i++)
                csv << (i + 1) * 1000 << "," << res.progressive[i] << "\n";
            std::cout << "Taxa progressiva:   " << csv_path << "\n";
        }

        // Salva a posição (em símbolos) de cada reset executado
        if (!res.reset_positions.empty()) {
            std::string rst_path = fout + ".reset.csv";
            std::ofstream rst(rst_path);
            rst << "n\n";
            for (size_t pos : res.reset_positions)
                rst << pos << "\n";
            std::cout << "Posições de reset:  " << rst_path << "\n";
        }

        return 0;
    }

    // DECODE
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

        // Verifica se o número de bytes recuperados bate com o cabeçalho
        if (out.size() == hdr.orig_size)
            std::cout << "Integridade:        OK (tamanho correto)\n";
        else
            std::cerr << "AVISO: tamanho diferente do esperado!\n";

        return 0;
    }

    // BENCH: tabela de Kmax 0..kmax_max
    if (cmd == "bench") {
        if (argc < 3) { printUsage(argv[0]); return 1; }

        std::string fin  = argv[2];
        int    kmax_max  = (argc > 3) ? std::stoi(argv[3]) : 6;
        bool   use_reset = (argc > 4) && (std::stoi(argv[4]) != 0);
        int    window    = (argc > 5) ? std::stoi(argv[5]) : 1000;
        double thresh    = (argc > 6) ? std::stod(argv[6]) : 10.0;
        kmax_max = std::clamp(kmax_max, 0, 10);

        std::cout << "Arquivo: " << fin << "\n";
        auto input = readFile(fin);
        std::cout << "Tamanho: " << input.size() << " bytes\n";
        std::cout << "Reset:   " << (use_reset ? "sim" : "não");
        if (use_reset)
            std::cout << "  (janela=" << window << ", limiar=" << thresh << "%)";
        std::cout << "\n\n";

        std::cout << "Kmax | Bits/sym | Comprimido(B) | Razão%  | T.enc(s) | T.dec(s)\n";
        std::cout << "-----|----------|---------------|---------|----------|---------\n";

        for (int k = 0; k <= kmax_max; k++) {
            auto t0  = std::chrono::high_resolution_clock::now();
            auto res = encode(input, k, window, thresh, use_reset, 0);
            auto t1  = std::chrono::high_resolution_clock::now();
            double dt_enc = std::chrono::duration<double>(t1 - t0).count();

            auto t2  = std::chrono::high_resolution_clock::now();
            decode(res.compressed, input.size(), k, use_reset);
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