# PPM-C — Compressor Adaptativo com Codificação Aritmética

Implementação do algoritmo **PPM-C** (*Prediction by Partial Matching, Method C*) com codificação aritmética de precisão inteira. Desenvolvido como projeto da disciplina de ITI — UFPB 2025.2.

---

## Requisitos

- Compilador C++17 (`g++` ≥ 7 ou `clang++` ≥ 5)
- `make`
- Python 3.10+ (apenas para os scripts de experimento)

---

## Compilação

```bash
make
```

O binário `ppm` será gerado na raiz do projeto. Para recompilar do zero:

```bash
make clean && make
```

> A compilação usa `-std=c++17 -O2 -Wall`. Os objetos intermediários ficam em `build/`.

---

## Uso

```
./ppm <comando> [argumentos]
```

### Comandos disponíveis

| Comando  | Descrição |
|----------|-----------|
| `encode` | Comprime um arquivo |
| `decode` | Descomprime um arquivo `.ppm` |
| `bench`  | Compara compressão para Kmax de 0 até N |

---

## encode — Comprimir um arquivo

```
./ppm encode <entrada> <saida.ppm> [kmax] [janela] [limiar%] [reset]
```

| Parâmetro | Padrão | Descrição |
|-----------|--------|-----------|
| `entrada` | — | Arquivo a comprimir |
| `saida.ppm` | — | Arquivo comprimido de saída |
| `kmax` | `5` | Ordem máxima do modelo PPM (0–10) |
| `janela` | `1000` | Tamanho da janela do monitor de taxa |
| `limiar%` | `10` | Piora mínima de taxa (%) para disparar reset |
| `reset` | `0` | `1` = reset adaptativo ativo, `0` = desativado |

### Exemplos

```bash
# Comprimir com Kmax=5, sem reset (padrão)
./ppm encode silesia/dickens dickens.ppm 5

# Comprimir com Kmax=7 e reset adaptativo ativo
./ppm encode silesia/dickens dickens.ppm 7 1000 10 1

# Comprimir arquivo binário com Kmax=3
./ppm encode silesia/mozilla mozilla.ppm 3
```

### Saída

```
Lendo silesia/dickens...
Tamanho original:   10192446 bytes
Kmax:               5
Reset ativo:        não
Bits totais:        26451200
Bits/símbolo:       2.5951
Tamanho comprimido: 3306406 bytes
Taxa de compressão: 32.44%
Tempo (encode):     4.321 s
Taxa progressiva:   dickens.ppm.rate.csv
```

O arquivo `<saida>.rate.csv` contém a taxa progressiva (bits/símbolo) a cada 1000 símbolos processados — útil para análise de estacionariedade.

---

## decode — Descomprimir

```
./ppm decode <entrada.ppm> <saida>
```

| Parâmetro | Descrição |
|-----------|-----------|
| `entrada.ppm` | Arquivo comprimido gerado pelo `encode` |
| `saida` | Arquivo de saída recuperado |

O decoder lê automaticamente os parâmetros `kmax` e `reset` do cabeçalho do arquivo — não é necessário especificá-los manualmente.

### Exemplos

```bash
# Descomprimir
./ppm decode dickens.ppm dickens_recuperado

# Verificar que o arquivo é idêntico ao original
sha256sum silesia/dickens dickens_recuperado
cmp silesia/dickens dickens_recuperado && echo "OK — arquivos idênticos"
```

### Saída

```
Decodificando 10192446 bytes, Kmax=5
Bytes escritos:     10192446
Tempo (decode):     3.876 s
Integridade:        OK (tamanho correto)
```

---

## bench — Tabela comparativa por Kmax

```
./ppm bench <entrada> [kmax_max] [reset] [janela] [limiar%]
```

| Parâmetro | Padrão | Descrição |
|-----------|--------|-----------|
| `entrada` | — | Arquivo a usar no benchmark |
| `kmax_max` | `6` | Testa Kmax de 0 até este valor |
| `reset` | `0` | `1` = reset ativo em todos os testes |
| `janela` | `1000` | Tamanho da janela do monitor |
| `limiar%` | `10` | Limiar de disparo do reset |

### Exemplos

```bash
# Benchmark Kmax 0..6, sem reset
./ppm bench silesia/dickens 6

# Benchmark Kmax 0..8, com reset
./ppm bench silesia/dickens 8 1 1000 10

# Benchmark do corpus completo até Kmax=5
./ppm bench silesia_concat 5
```

### Saída

```
Arquivo: silesia/dickens
Tamanho: 10192446 bytes
Reset:   não

Kmax | Bits/sym | Comprimido(B) | Razão%  | T.enc(s) | T.dec(s)
-----|----------|---------------|---------|----------|---------
   0 |   6.2134 |       7934812 |  77.85% |    0.823 |   0.741
   1 |   4.8901 |       6240200 |  61.23% |    1.341 |   1.102
   2 |   3.5210 |       4494150 |  44.09% |    2.187 |   1.834
   3 |   2.9403 |       3753280 |  36.83% |    3.021 |   2.561
   4 |   2.6812 |       3421100 |  33.57% |    3.987 |   3.312
   5 |   2.5951 |       3311040 |  32.49% |    4.821 |   4.103
   6 |   2.5630 |       3270080 |  32.08% |    6.234 |   5.412
```

---

## Notas sobre os parâmetros

**Kmax**: valores entre 5 e 7 costumam dar a melhor compressão para texto. Valores muito altos (≥ 9) raramente melhoram, pois os contextos longos são visitados poucas vezes e a probabilidade de escape aumenta.

**Reset adaptativo**: beneficia arquivos com mudanças abruptas de características (e.g., corpus concatenados com tipos de arquivo heterogêneos). Para arquivos homogêneos, o overhead de sinalização pode superar o ganho.

**Janela / Limiar**: janelas menores (100–500) detectam mudanças mais rápido mas são mais ruidosas. Um limiar de 10–20% é um bom ponto de partida.
