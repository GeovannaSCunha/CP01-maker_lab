/*
  coleta_dataset.ino
  Coleta do dataset.csv para o modelo TinyML do carrinho (ESP32-S3 + HC-SR04)

  Project-based Maker Lab — FIAP — Turma 4ESPX

  Como usar (Serial Monitor em 115200, com "Nova linha" ou "Sem final de linha"):
    0 = começa a gravar amostras como "livre"     (nada perto, mais de ~30 cm)
    1 = começa a gravar amostras como "obstaculo" (objeto a menos de ~20 cm)
    p = pausa a gravação

  Depois copie as linhas do Serial Monitor (do cabeçalho "valor,label" para baixo)
  e salve como docs/dataset.csv.

  IMPORTANTE: o pré-processamento abaixo é o MESMO do tinyml_inferencia.ino.
*/

const int TRIG = 12;
const int ECHO = 13;   // com divisor de tensão 1kΩ / 2kΩ

// ---------- Pré-processamento (igual ao código final) ----------
const float DIST_MIN = 2.0;     // cm
const float DIST_MAX = 100.0;   // cm
const int   JANELA_MEDIA = 5;   // leituras na média móvel

const unsigned long INTERVALO = 100;  // ms entre amostras

float historico[JANELA_MEDIA];
int idxHist = 0;
int qtdHist = 0;

int rotuloAtual = -1;   // -1 = pausado
int contagem[2] = {0, 0};
unsigned long ultimaAmostra = 0;

void setup() {
  Serial.begin(115200);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  delay(1000);

  Serial.println("# Coleta pronta. Envie 0 (livre), 1 (obstaculo) ou p (pausa).");
  Serial.println("valor,label");
}

void loop() {
  lerTeclado();

  if (millis() - ultimaAmostra >= INTERVALO) {
    ultimaAmostra = millis();

    float distancia = medirDistancia();
    float valor = preprocessar(distancia);

    if (rotuloAtual >= 0) {
      Serial.print(valor, 4);
      Serial.print(",");
      Serial.println(rotuloAtual);
      contagem[rotuloAtual]++;
    }
  }
}

void lerTeclado() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '0' || c == '1') {
      rotuloAtual = c - '0';
      limparHistorico();   // não mistura leituras do rótulo anterior
      Serial.printf("# Gravando classe %d (%s)\n", rotuloAtual,
                    rotuloAtual == 1 ? "obstaculo" : "livre");
    } else if (c == 'p' || c == 'P') {
      rotuloAtual = -1;
      Serial.printf("# Pausado. Total: livre=%d | obstaculo=%d\n",
                    contagem[0], contagem[1]);
    }
  }
}

float medirDistancia() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long duracao = pulseIn(ECHO, HIGH, 30000);
  if (duracao == 0) return DIST_MAX;   // sem eco = nada por perto

  return duracao * 0.0343 / 2.0;
}

float preprocessar(float distancia) {
  // 1) limita a faixa
  distancia = constrain(distancia, DIST_MIN, DIST_MAX);

  // 2) média móvel
  historico[idxHist] = distancia;
  idxHist = (idxHist + 1) % JANELA_MEDIA;
  if (qtdHist < JANELA_MEDIA) qtdHist++;

  float soma = 0;
  for (int i = 0; i < qtdHist; i++) soma += historico[i];
  float media = soma / qtdHist;

  // 3) normaliza para 0–1
  return (media - DIST_MIN) / (DIST_MAX - DIST_MIN);
}

void limparHistorico() {
  idxHist = 0;
  qtdHist = 0;
}
