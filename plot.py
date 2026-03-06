import csv
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# ── Arquivos do Corpus Silesia (ordem da tabela) ─────────────────────────────
silesia_files = [
    ("dickens",  10_192_446),
    ("mozilla",  51_220_480),
    ("mr",        9_970_564),
    ("nci",      33_553_445),
    ("ooffice",   6_152_192),
    ("osdb",     10_085_684),
    ("reymont",   6_627_202),
    ("samba",    21_606_400),
    ("sao",       7_251_944),
    ("webster",  41_458_703),
    ("xml",       5_345_280),
    ("x-ray",     8_474_240),
]

# Calcula as fronteiras acumuladas (em bytes = posição no stream concatenado)
boundaries = []
acc = 0
for name, size in silesia_files:
    acc += size
    boundaries.append((name, acc))

# Converte bytes → posição em símbolos (1 byte = 1 símbolo)
# A taxa progressiva é amostrada a cada 1000 símbolos, então:
#   posição_amostra = n  →  byte n*1000
# As fronteiras já estão em bytes, usamos diretamente como eixo X.

# ── Carrega CSV de taxa progressiva ──────────────────────────────────────────
# Formato: n,bits_per_sym   (n = posição em símbolos, não bytes)
# Troque o caminho abaixo pelo seu arquivo .rate.csv real.
CSV_FILE = "silesia_no_reset.ppm.rate.csv"   # ← ajuste o caminho se necessário

n_vals, r_vals = [], []
try:
    with open(CSV_FILE) as f:
        next(f)  # pula cabeçalho
        for row in csv.reader(f):
            n_vals.append(int(row[0]))
            r_vals.append(float(row[1]))
except FileNotFoundError:
    # Gera dados simulados para demonstração visual
    print(f"[aviso] '{CSV_FILE}' não encontrado — usando dados simulados.")
    total = sum(s for _, s in silesia_files)
    step = 1000
    n_vals = list(range(step, total + step, step))
    # Simula variação realista: baixo em texto, alto em binário
    rng = np.random.default_rng(42)
    base_rates = {
        "dickens":  2.2, "mozilla": 6.5, "mr":      5.8, "nci":     3.1,
        "ooffice":  5.9, "osdb":    4.2, "reymont": 2.8, "samba":   3.9,
        "sao":      5.5, "webster": 2.6, "xml":     1.8, "x-ray":   5.7,
    }
    r_vals = []
    acc_bits = 0
    for i, n in enumerate(n_vals):
        # descobre em qual arquivo estamos
        pos = n
        cum = 0
        seg_rate = 4.0
        for fname, fsize in silesia_files:
            cum += fsize
            if pos <= cum:
                seg_rate = base_rates[fname]
                break
        noise = rng.normal(0, 0.15)
        acc_bits += (seg_rate + noise) * step
        r_vals.append(acc_bits / n)

# ── Figura ───────────────────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(16, 6))
fig.patch.set_facecolor("#07080c")
ax.set_facecolor("#0f1117")

# Paleta de cores alternada para as regiões de cada arquivo
region_colors = [
    "#1a2a1a", "#1a1a2a", "#2a1a1a", "#1a2a2a",
    "#2a2a1a", "#1a1a1a", "#2a1a2a", "#1a2a1a",
    "#1a1a2a", "#2a1a1a", "#1a2a2a", "#2a2a1a",
]

# Desenha regiões coloridas de fundo (uma por arquivo)
prev_boundary = 0
for i, (fname, boundary) in enumerate(boundaries):
    ax.axvspan(prev_boundary, boundary,
               alpha=0.35, color=region_colors[i % len(region_colors)],
               linewidth=0)
    prev_boundary = boundary

# Linha principal da taxa progressiva
ax.plot(n_vals, r_vals, color="#00e5a0", linewidth=0.9,
        alpha=0.95, zorder=5, label="Bits/símbolo acumulado")

# ── Linhas verticais e labels nas fronteiras ──────────────────────────────────
label_colors = [
    "#4d9fff", "#ff9f4d", "#c084fc", "#67e8f9",
    "#ffd166", "#ff6b6b", "#00e5a0", "#f472b6",
    "#a78bfa", "#34d399", "#fb923c", "#60a5fa",
]

prev_boundary = 0
for i, (fname, boundary) in enumerate(boundaries):
    color = label_colors[i % len(label_colors)]

    # Linha vertical de fronteira
    ax.axvline(x=boundary, color=color, linewidth=1.0,
               linestyle="--", alpha=0.7, zorder=6)

    # Label no topo da linha (alternando altura para não sobrepor)
    ymax = ax.get_ylim()[1] if ax.get_ylim()[1] > 0 else max(r_vals) * 1.15
    label_y_frac = 0.97 if i % 2 == 0 else 0.88
    label_y = min(r_vals) + (max(r_vals) - min(r_vals)) * label_y_frac + \
              (max(r_vals) - min(r_vals)) * 0.05

    # Posição X do label: centro da região
    x_center = (prev_boundary + boundary) / 2

    ax.text(x_center, max(r_vals) * 1.02 + (0.08 if i % 2 == 0 else 0.01),
            fname, color=color, fontsize=8, fontweight="bold",
            ha="center", va="bottom", rotation=0,
            bbox=dict(boxstyle="round,pad=0.25", facecolor="#07080c",
                      edgecolor=color, alpha=0.85, linewidth=0.8),
            zorder=10)

    # Seta apontando para a fronteira
    ax.annotate("",
        xy=(boundary, max(r_vals) * 0.97),
        xytext=(boundary, max(r_vals) * 1.00 + (0.06 if i % 2 == 0 else -0.01)),
        arrowprops=dict(arrowstyle="-|>", color=color, lw=0.8),
        zorder=9)

    prev_boundary = boundary

# ── Estilo dos eixos ──────────────────────────────────────────────────────────
ax.tick_params(colors="#5a6380", labelsize=9)
ax.spines["bottom"].set_color("#262c3a")
ax.spines["left"].set_color("#262c3a")
ax.spines["top"].set_visible(False)
ax.spines["right"].set_visible(False)

# Eixo X: formata em MB para legibilidade
def fmt_mb(x, _):
    return f"{x/1e6:.0f}M"
from matplotlib.ticker import FuncFormatter
ax.xaxis.set_major_formatter(FuncFormatter(fmt_mb))
ax.xaxis.set_major_locator(plt.MultipleLocator(20_000_000))

ax.set_xlabel("Posição no corpus concatenado (bytes)", color="#8b9ab0",
              fontsize=10, labelpad=8)
ax.set_ylabel("Bits/símbolo acumulado", color="#8b9ab0",
              fontsize=10, labelpad=8)
ax.set_title("Comprimento Médio Progressivo — Corpus Silesia",
             color="#dce3f0", fontsize=13, fontweight="bold", pad=20)

ax.set_xlim(0, boundaries[-1][1])
ax.set_ylim(min(r_vals) * 0.95, max(r_vals) * 1.18)

ax.grid(axis="y", color="#1f2330", linewidth=0.6, zorder=0)
ax.grid(axis="x", color="#1f2330", linewidth=0.4, zorder=0)

# Legenda simplificada
line_patch = mpatches.Patch(color="#00e5a0", label="Taxa acumulada (bits/sym)")
ax.legend(handles=[line_patch], loc="lower right",
          facecolor="#0f1117", edgecolor="#262c3a",
          labelcolor="#dce3f0", fontsize=9)

plt.tight_layout(rect=[0, 0, 1, 0.97])
plt.savefig("silesia_progressivo.png", dpi=180, bbox_inches="tight",
            facecolor=fig.get_facecolor())
print("Salvo em silesia_progressivo.png")
plt.show()