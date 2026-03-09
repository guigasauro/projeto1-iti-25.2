#include "ppm.hpp"

PPMModel::PPMModel(int k) : kmax(k), table(k + 1) {}

void PPMModel::reset() {
    for (auto& t : table) t.clear();
    history.clear();
}

ByteCtx PPMModel::ctx(int ord) const {
    // Retorna o sufixo do histórico de comprimento ord (contexto ativo)
    if (ord == 0) return "";
    size_t h = history.size();
    return (h >= (size_t)ord) ? history.substr(h - ord) : history;
}

std::optional<SymProb> PPMModel::getProb(int ord,
                                          const ByteCtx& c,
                                          int sym,
                                          const bool excluded[256]) const
{
    auto it = table[ord].find(c);
    if (it == table[ord].end()) return std::nullopt;

    const SymCount& counts = it->second;

    // Soma contagens e símbolos distintos, respeitando exclusões
    uint64_t total = 0, uniq = 0;
    for (auto& [s, cnt] : counts) {
        if (!excluded[s]) { total += cnt; uniq++; }
    }
    if (uniq == 0) return std::nullopt;

    // Método C: escape recebe peso igual ao número de símbolos distintos
    uint64_t denom = total + uniq;

    // sym == -1: retorna o intervalo do escape [total, denom)
    if (sym == -1) {
        return SymProb{ total, denom, denom };
    }

    // Percorre a CDF para encontrar o intervalo do símbolo pedido
    uint64_t cdf = 0;
    for (auto& [s, cnt] : counts) {
        if (excluded[s]) continue;
        if ((int)s == sym) {
            return SymProb{ cdf, cdf + cnt, denom };
        }
        cdf += cnt;
    }

    // Símbolo não está neste contexto → retorna escape
    return SymProb{ total, denom, denom };
}

void PPMModel::addExclusions(int ord, const ByteCtx& c, bool excluded[256]) const {
    auto it = table[ord].find(c);
    if (it == table[ord].end()) return;
    for (auto& [s, cnt] : it->second)
        excluded[s] = true;
}

SymProb PPMModel::getUniform(int sym, const bool excluded[256],
                              bool reset_slot) const
{
    // Constrói a lista de símbolos disponíveis (não excluídos)
    std::vector<int> avail;
    for (int i = 0; i < 256; i++)
        if (!excluded[i]) avail.push_back(i);
    // Reserva o slot 256 para o token de reset quando necessário
    if (reset_slot) avail.push_back(256);
    // Fallback: se tudo foi excluído, usa o alfabeto completo
    if (avail.empty()) {
        for (int i = 0; i < 256; i++) avail.push_back(i);
    }

    // Distribuição uniforme: cada símbolo ocupa um slot de tamanho 1/n
    uint64_t n = avail.size();
    for (uint64_t i = 0; i < n; i++) {
        if (avail[i] == sym) return SymProb{ i, i + 1, n };
    }
    return SymProb{ 0, 1, n }; // não deveria chegar aqui
}

int PPMModel::decodeUniform(uint64_t count, const bool excluded[256],
                            bool reset_slot) const
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

uint64_t PPMModel::uniformDenom(const bool excluded[256],
                                bool reset_slot) const
{
    uint64_t n = 0;
    for (int i = 0; i < 256; i++) if (!excluded[i]) n++;
    if (reset_slot) n++;
    return (n > 0) ? n : 256;
}

void PPMModel::update(uint8_t sym) {
    // Incrementa a contagem de sym em todos os contextos de sufixo (ordens 0..kmax)
    for (int ord = 0; ord <= kmax; ord++)
        table[ord][ctx(ord)][sym]++;
    // Anexa sym ao histórico e mantém o tamanho máximo em kmax bytes
    history += (char)sym;
    if ((int)history.size() > kmax)
        history = history.substr(history.size() - kmax);
}
