# preparar_edge_impulse.py
# Lê dataset_microclima.csv e gera as pastas ei_treino/ e ei_teste/
# com um CSV por trecho (sessão + rótulo constante), no formato do Edge Impulse:
#   nome do arquivo = <rotulo>.s<sessao>_<trecho>.csv
#   colunas = timestamp (ms), temp_n, ur_n
# Uso: python preparar_edge_impulse.py

import os
import pandas as pd

ENTRADA = "dataset_microclima.csv"
INTERVALO_MS = 2000          # igual ao INTERVALO do coleta_microclima.ino
MIN_LINHAS = 5               # descarta trechos muito curtos
NOMES = {0: "ideal", 1: "umido", 2: "atencao", 3: "critico"}

df = pd.read_csv(ENTRADA)
df["classe"] = df["label"].map(NOMES)

print("Amostras por classe:")
print(df["classe"].value_counts().to_string(), "\n")

# 1 de cada 3 sessões vai para o teste
sessoes = sorted(df["sessao"].unique())
sessoes_teste = set(sessoes[2::3])
print("Sessões:", sessoes)
print("Sessões de teste:", sorted(sessoes_teste), "\n")

for pasta in ("ei_treino", "ei_teste"):
    os.makedirs(pasta, exist_ok=True)

resumo = {"ei_treino": {}, "ei_teste": {}}

for s in sessoes:
    parte = df[df["sessao"] == s].reset_index(drop=True)
    pasta = "ei_teste" if s in sessoes_teste else "ei_treino"

    # trechos consecutivos com o mesmo rótulo
    parte["trecho"] = (parte["label"] != parte["label"].shift()).cumsum()
    for n, trecho in parte.groupby("trecho"):
        if len(trecho) < MIN_LINHAS:
            continue
        classe = trecho["classe"].iloc[0]
        saida = trecho[["temp_n", "ur_n"]].copy()
        saida.insert(0, "timestamp", range(0, len(saida) * INTERVALO_MS, INTERVALO_MS))
        nome = f"{classe}.s{s}_{n}.csv"
        saida.to_csv(os.path.join(pasta, nome), index=False)
        resumo[pasta][classe] = resumo[pasta].get(classe, 0) + len(trecho)

for pasta, contagem in resumo.items():
    print(pasta, contagem)