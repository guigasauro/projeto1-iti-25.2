#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <iomanip>
#include <cmath>

// Estrutura para armazenar o modelo de probabilidade
struct Modelo {
    std::map<char, double> prob;       // probabilidade de cada símbolo
    std::map<char, double> cdf_baixo;  // limite inferior acumulado
    std::map<char, double> cdf_alto;   // limite superior acumulado

    // Constrói o modelo a partir de frequências
    void construir(const std::string& texto) {
        std::map<char, int> freq;
        for (char c : texto) freq[c]++;

        double total = texto.size();
        double acumulado = 0.0;

        for (auto& [c, f] : freq) {
            prob[c] = f / total;
            cdf_baixo[c] = acumulado;
            acumulado += prob[c];
            cdf_alto[c] = acumulado;
        }
    }

    void exibir() const {
        std::cout << "\n=== Modelo de Probabilidade ===\n";
        std::cout << std::fixed << std::setprecision(6);
        for (auto& [c, p] : prob) {
            std::cout << "  '" << c << "': prob=" << p
                      << "  intervalo=[" << cdf_baixo.at(c)
                      << ", " << cdf_alto.at(c) << ")\n";
        }
    }
};

// Codificador Aritmético
double codificar(const std::string& mensagem, const Modelo& modelo) {
    double baixo = 0.0;
    double alto  = 1.0;

    std::cout << "\n=== Processo de Codificação ===\n";
    std::cout << std::fixed << std::setprecision(8);

    for (char c : mensagem) {
        double intervalo = alto - baixo;

        // Refina o intervalo com base no símbolo atual
        alto  = baixo + intervalo * modelo.cdf_alto.at(c);
        baixo = baixo + intervalo * modelo.cdf_baixo.at(c);

        std::cout << "  Símbolo '" << c << "' -> intervalo: ["
                  << baixo << ", " << alto << ")\n";
    }

    // O código é qualquer valor dentro do intervalo final
    double codigo = (baixo + alto) / 2.0;
    return codigo;
}

// Decodificador Aritmético
std::string decodificar(double codigo, int tamanho, const Modelo& modelo) {
    std::string resultado;

    std::cout << "\n=== Processo de Decodificação ===\n";
    std::cout << std::fixed << std::setprecision(8);

    for (int i = 0; i < tamanho; i++) {
        // Encontra qual símbolo contém o código no seu intervalo
        for (auto& [c, baixo] : modelo.cdf_baixo) {
            double alto = modelo.cdf_alto.at(c);

            if (codigo >= baixo && codigo < alto) {
                resultado += c;
                std::cout << "  Código " << codigo << " -> símbolo '" << c << "'\n";

                // Remove a contribuição do símbolo encontrado
                codigo = (codigo - baixo) / (alto - baixo);
                break;
            }
        }
    }

    return resultado;
}

int main() {
    std::string mensagem = "assassinar";

    std::cout << "Mensagem original: " << mensagem << "\n";
    std::cout << "Tamanho: " << mensagem.size() << " símbolos\n";

    // 1. Constrói o modelo de probabilidade
    Modelo modelo;
    modelo.construir(mensagem);
    modelo.exibir();

    // 2. Codifica a mensagem
    double codigo = codificar(mensagem, modelo);
    std::cout << "\n>>> Código aritmético: " << std::setprecision(10) << codigo << "\n";

    // 3. Decodifica o código
    std::string recuperada = decodificar(codigo, mensagem.size(), modelo);
    std::cout << "\n>>> Mensagem decodificada: " << recuperada << "\n";

    // 4. Verificação
    std::cout << "\n=== Verificação ===\n";
    std::cout << "  Correto: " << (mensagem == recuperada ? "SIM ✓" : "NÃO ✗") << "\n";

    // 5. Eficiência informacional
    double entropia = 0.0;
    for (auto& [c, p] : modelo.prob) {
        if (p > 0) entropia -= p * std::log2(p);
    }
    std::cout << "\n=== Eficiência ===\n";
    std::cout << "  Entropia por símbolo: " << entropia << " bits\n";
    std::cout << "  Bits totais ideais:   " << entropia * mensagem.size() << " bits\n";

    return 0;
}