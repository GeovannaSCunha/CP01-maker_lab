# gerar_dataset_sintetico.py
# Gera dataset_microclima.csv SINTÉTICO (demonstrativo) no mesmo formato do coletado
# pelo ESP32: sessao,zona,temp_c,ur_pct,vpd_kpa,temp_n,ur_n,label
# O rótulo é calculado pela mesma regra do VPD do coleta_microclima.ino.
# Uso: python gerar_dataset_sintetico.py

import math
import numpy as np
import pandas as pd

SEMENTE = 42
LINHAS_POR_SESSAO = 90        # ~3 min a cada 2 s
SESSOES_POR_ZONA = 5

TEMP_MAX_NORM = 50.0
UR_MAX_NORM = 100.0

# Mesmos limiares do coleta_microclima.ino
VPD_UMIDO_ATE = 0.4
VPD_IDEAL_ATE = 1.2
VPD_ATENCAO_ATE = 2.0
TEMP_CRITICA = 35.0

# Zona: (faixa de temperatura °C, faixa de umidade %) onde a sessão começa e termina
ZONAS = {
    0: ((20, 28), (50, 75)),   # ambiente normal
    1: ((28, 40), (15, 45)),   # quente e seco
    2: ((18, 27), (82, 90)),   # úmido
    3: ((20, 32), (30, 90)),   # intermediária (cruza as fronteiras)
}

rng = np.random.default_rng(SEMENTE)


def calcular_vpd(t, ur):
    es = 0.6108 * math.exp(17.27 * t / (t + 237.3))
    return es * (1.0 - ur / 100.0)


def classificar_regra(t, vpd):
    if t >= TEMP_CRITICA or vpd >= VPD_ATENCAO_ATE:
        return 3
    if vpd < VPD_UMIDO_ATE:
        return 1
    if vpd < VPD_IDEAL_ATE:
        return 0
    return 2


linhas = []
sessao = 0
for _ in range(SESSOES_POR_ZONA):
    for zona, (faixa_t, faixa_u) in ZONAS.items():
        sessao += 1
        # a sessão vai de um ponto a outro da zona (deriva lenta, como no sensor real)
        t_ini, t_fim = rng.uniform(*faixa_t, 2)
        u_ini, u_fim = rng.uniform(*faixa_u, 2)
        temps = np.linspace(t_ini, t_fim, LINHAS_POR_SESSAO)
        urs = np.linspace(u_ini, u_fim, LINHAS_POR_SESSAO)
        # ruído pequeno + resolução inteira do DHT11
        temps = np.round(temps + rng.normal(0, 0.4, LINHAS_POR_SESSAO))
        urs = np.round(urs + rng.normal(0, 1.0, LINHAS_POR_SESSAO))
        temps = np.clip(temps, 0, 50)
        urs = np.clip(urs, 20, 90)      # faixa do DHT11

        for t, u in zip(temps, urs):
            vpd = calcular_vpd(t, u)
            linhas.append({
                "sessao": sessao,
                "zona": zona,
                "temp_c": round(float(t), 1),
                "ur_pct": round(float(u), 1),
                "vpd_kpa": round(vpd, 3),
                "temp_n": round(float(t) / TEMP_MAX_NORM, 4),
                "ur_n": round(float(u) / UR_MAX_NORM, 4),
                "label": classificar_regra(float(t), vpd),
            })

df = pd.DataFrame(linhas)
df.to_csv("dataset_microclima.csv", index=False)

nomes = {0: "ideal", 1: "umido", 2: "atencao", 3: "critico"}
print("dataset_microclima.csv salvo (SINTETICO).")
print("Amostras por classe:")
print(df["label"].map(nomes).value_counts().to_string())
raras = [nomes[k] for k in nomes if (df["label"] == k).sum() < 100]
if raras:
    print("\nAVISO: classes com menos de 100 amostras:", raras)
    print("Ajuste as faixas em ZONAS (ou aumente SESSOES_POR_ZONA) e rode de novo.")