/*
  model.h
  Modelo TinyML do carrinho — exportado do Edge Impulse

  Project-based Maker Lab — FIAP — Turma 4ESPX
  Geovanna Silva Cunha (RM97736) | Victor Camargo Maciel (RM98384) | Pedro Emerici Gava (RM551043)

  Projeto Edge Impulse: victormaciel13-project-1 (ID 1146772), deploy v1
  Origem: biblioteca Arduino gerada pelo Edge Impulse (EON Compiler, int8),
          arquivo src/tflite-model/tflite_learn_1146772_3_compiled.cpp

  ---------------------------------------------------------------------------
  Entrada : 1 valor — distância normalizada 0–1, calculada como
            (média móvel de 5 leituras, limitada a 2–100 cm - 2) / 98
  Saída   : 2 classes (softmax)
              [0] "0" = caminho livre
              [1] "1" = obstáculo
  Rede    : Dense(20, ReLU) -> Dense(10, ReLU) -> Dense(2) -> Softmax
  Pesos   : quantizados em int8 (bias em int32), 262 parâmetros no total
  Treino  : 200 ciclos, learning rate 0,005 | acurácia de validação 100%
  ---------------------------------------------------------------------------

  No carrinho, a inferência é feita pela biblioteca do Edge Impulse
  (src/tinyml_inferencia.ino), que usa exatamente estes mesmos pesos.
  Este arquivo deixa o modelo legível e independente da biblioteca:
  a função modelo_prever(), no final, executa a mesma rede em C puro.

  Valor real de cada número: real = valor_quantizado * escala
*/

#ifndef MODELO_CARRINHO_H
#define MODELO_CARRINHO_H

#include <stdint.h>
#include <math.h>

#define MODELO_NUM_ENTRADAS   1
#define MODELO_NUM_CLASSES    2
#define MODELO_CLASSE_LIVRE      0
#define MODELO_CLASSE_OBSTACULO  1

static const char *MODELO_ROTULOS[MODELO_NUM_CLASSES] = { "0", "1" };

// Pré-processamento usado na coleta e na inferência
#define MODELO_DIST_MIN_CM     2.0f
#define MODELO_DIST_MAX_CM     100.0f
#define MODELO_JANELA_MEDIA    5

// Quantização da entrada e da saída (int8)
#define MODELO_ENTRADA_ESCALA  0.0022674510255455971f
#define MODELO_ENTRADA_ZERO    (-128)
#define MODELO_SAIDA_ESCALA    0.00390625f
#define MODELO_SAIDA_ZERO      (-128)

// ---------------- Camada 1: Dense 1 -> 20 (ReLU) ----------------
#define MODELO_C1_PESO_ESCALA  0.0105102164670825f
#define MODELO_C1_BIAS_ESCALA  2.3831400540075265e-05f
static const int8_t modelo_c1_pesos[20 * 1] = {
  0, -49, 120, -42, 123, -15, -24, 115, 99, -42,
  123, -7, -23, -70, 127, 0, -85, -10, -43, -2,
};
static const int32_t modelo_c1_bias[20] = {
  0, 0, -6255, 0, -8812, 0, 26045, -8366, 2918, 0,
  -5693, 0, 0, 17247, -9748, 0, 20904, 0, 0, 0,
};

// ---------------- Camada 2: Dense 20 -> 10 (ReLU) ----------------
// pesos organizados como [saída][entrada]
#define MODELO_C2_PESO_ESCALA  0.011536347679793835f
#define MODELO_C2_BIAS_ESCALA  3.0228520699893124e-05f
static const int8_t modelo_c2_pesos[10 * 20] = {
  13, -26, 65, 4, 121, 5, 31, 67, 60, -17, 86, 6, 21, -38, 108, -27, -93, -9, -23, 8,
  -19, -38, 23, 22, 18, -14, -17, -32, -31, 6, -17, -18, 21, 7, 23, 30, -17, 11, -15, -10,
  -8, -32, -3, 35, -16, -20, -14, 5, -16, -4, 9, 17, -7, -31, -4, -23, 26, -32, -33, -27,
  20, 4, 93, 1, 37, 25, -2, 50, 46, 24, 88, 16, -24, -71, 58, -15, -77, -18, 35, 28,
  -15, 20, -32, -14, -28, 23, -12, 9, -31, 20, 30, 19, -7, 16, 8, -32, -16, 33, 24, 19,
  -29, 23, -87, 17, -82, -39, 59, -59, -47, -16, -31, 20, -17, 127, -83, -22, 85, 21, -22, 1,
  31, 16, 47, -26, 91, 26, 10, 60, 46, 1, 80, 14, -24, -65, 68, -6, -55, -6, -5, -12,
  -5, 30, -38, 20, -59, 29, 82, -101, 22, -10, -66, 37, 22, 86, -90, 17, 114, 2, 4, 28,
  21, 13, 81, 14, 89, 11, -12, 56, 48, 4, 87, 7, -2, -65, 48, 32, -92, 24, -16, -11,
  15, -24, 66, -1, 96, 33, 3, 82, 85, -31, 44, 16, -25, -70, 89, -6, -85, 29, 1, 26,
};
static const int32_t modelo_c2_bias[10] = {
  -10313, 0, -846, -170, 0, 17184, -4959, 15263, 2443, -3452,
};

// ---------------- Camada 3: Dense 10 -> 2 (Softmax) ----------------
#define MODELO_C3_PESO_ESCALA  0.014241781085729599f
#define MODELO_C3_BIAS_ESCALA  0.0001770667004166171f
static const int8_t modelo_c3_pesos[2 * 10] = {
  96, -40, 27, 114, 49, -127, 50, -114, 67, 104,
  -40, 6, -8, -81, -38, 96, -113, 93, -100, -55,
};
static const int32_t modelo_c3_bias[2] = {
  -2206, 2206,
};

// ---------------------------------------------------------------------------
// Executa o modelo em C puro (pesos convertidos para float).
// valor: distância já normalizada (0–1)
// prob : recebe a probabilidade de cada classe ([0] livre, [1] obstáculo)
// Retorna o índice da classe mais provável.
// ---------------------------------------------------------------------------
static inline int modelo_prever(float valor, float prob[MODELO_NUM_CLASSES]) {
  float c1[20], c2[10], z[2];

  for (int i = 0; i < 20; i++) {
    float s = modelo_c1_pesos[i] * MODELO_C1_PESO_ESCALA * valor
            + modelo_c1_bias[i] * MODELO_C1_BIAS_ESCALA;
    c1[i] = s > 0 ? s : 0;
  }

  for (int o = 0; o < 10; o++) {
    float s = modelo_c2_bias[o] * MODELO_C2_BIAS_ESCALA;
    for (int i = 0; i < 20; i++) s += modelo_c2_pesos[o * 20 + i] * MODELO_C2_PESO_ESCALA * c1[i];
    c2[o] = s > 0 ? s : 0;
  }

  for (int o = 0; o < 2; o++) {
    float s = modelo_c3_bias[o] * MODELO_C3_BIAS_ESCALA;
    for (int i = 0; i < 10; i++) s += modelo_c3_pesos[o * 10 + i] * MODELO_C3_PESO_ESCALA * c2[i];
    z[o] = s;
  }

  float m = z[0] > z[1] ? z[0] : z[1];
  float e0 = expf(z[0] - m), e1 = expf(z[1] - m);
  prob[0] = e0 / (e0 + e1);
  prob[1] = e1 / (e0 + e1);
  return prob[1] > prob[0] ? MODELO_CLASSE_OBSTACULO : MODELO_CLASSE_LIVRE;
}

#endif  // MODELO_CARRINHO_H
