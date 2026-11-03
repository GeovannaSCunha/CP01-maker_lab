# salvar_dataset.py
# Lê o ESP32 pela porta serial e grava o dataset.csv direto, já sem as linhas "#".
# Uso: feche o Serial Monitor do Arduino IDE e rode:  python salvar_dataset.py
# Comandos: 0 = livre | 1 = obstaculo | p = pausa | sair = termina e salva

import threading
import serial

PORTA = "COM9"      # mesma porta que aparece no Arduino IDE
BAUD = 115200

esp = serial.Serial(PORTA, BAUD, timeout=1)
arquivo = open("dataset.csv", "w", newline="")
arquivo.write("valor,label\n")
contagem = {"0": 0, "1": 0}
rodando = True


def ler_esp():
    while rodando:
        linha = esp.readline().decode(errors="ignore").strip()
        if not linha or linha == "valor,label":
            continue
        if linha.startswith("#"):
            print(linha)                     # mensagens do ESP32 só aparecem na tela
            continue
        partes = linha.split(",")
        if len(partes) == 2 and partes[1] in contagem:
            arquivo.write(linha + "\n")
            arquivo.flush()
            contagem[partes[1]] += 1
            cm = 2 + float(partes[0]) * 98          # volta o valor para cm, só para conferir
            if contagem[partes[1]] % 10 == 0:
                print(f"  {cm:5.1f} cm | livre={contagem['0']} | obstaculo={contagem['1']}")


threading.Thread(target=ler_esp, daemon=True).start()
print("Conectado. Digite 0, 1 ou p e aperte Enter. Digite 'sair' para terminar.")

while True:
    comando = input().strip().lower()
    if comando == "sair":
        break
    if comando in ("0", "1", "p"):
        esp.write((comando + "\n").encode())

rodando = False
arquivo.close()
esp.close()
print(f"dataset.csv salvo! livre={contagem['0']} | obstaculo={contagem['1']}")
