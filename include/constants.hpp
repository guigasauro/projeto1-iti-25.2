#pragma once
#include <cstdint>

// CONSTANTES ARITMÉTICAS (representação inteira de 32 bits)
static constexpr uint64_t TOP_VALUE  = 0xFFFFFFFFULL;
static constexpr uint64_t FIRST_QTR = (TOP_VALUE / 4) + 1; // 0x40000000
static constexpr uint64_t HALF      = 2 * FIRST_QTR;       // 0x80000000
static constexpr uint64_t THIRD_QTR = 3 * FIRST_QTR;       // 0xC0000000

// Tamanho do alfabeto extendido (256 bytes + 1 slot de reset)
static constexpr int ALPHA_EXT = 257;
