/*
  tinyml_inferencia.ino
  Carrinho com ESP32-S3 + HC-SR04 + DHT11 + L298N + BLE + TinyML (Edge Impulse)

  Project-based Maker Lab — FIAP — Turma 4ESPX
  Geovanna Silva Cunha (RM97736)
  Victor Camargo Maciel (RM98384)
  Pedro Emerici Gava (RM551043)

  Funcionamento:
  - O HC-SR04 mede a distância e o carrinho desvia quando a média móvel das
    últimas leituras fica abaixo de DIST_OBSTACULO_CM (limiar fixo, sem modelo).
  - Se der obstáculo enquanto o carrinho anda para frente, ele para,
    gira (alternando direita/esquerda) e volta a andar.
  - O celular controla o carrinho por BLE (o ESP32-S3 não tem Bluetooth clássico).

  Microclima da horta (DHT11) — GS CitizenTech:
  - O DHT11 mede temperatura e umidade do ar. Uma mediana móvel de 5 leituras
    (IGUAL ao coleta_microclima.ino) é normalizada (T/50 e UR/100) e entra no
    modelo TinyML treinado no Edge Impulse, que classifica o risco:
    ideal / umido / atencao / critico.
  - Modo degradado: se o modelo falhar ou ficar incerto (confiança baixa),
    o carrinho usa a regra do VPD (classificarRegra) como reserva e avisa no log.
  - Modo PATRULHA (comando P): o carrinho anda e, quando encontra um obstáculo
    (um canteiro ou vaso), PARA, mede o microclima, manda o relatório ao celular
    e depois desvia normalmente, seguindo para o próximo.
  - Comando M: mede o microclima agora, com o carrinho parado.
  - Se o DHT11 falhar, o carrinho continua patrulhando e avisa "sem leitura".
  - Fora do modo patrulha, o carrinho se comporta exatamente como antes.

  Controle pelo celular (BLE UART, aparece como "Carrinho-ESP32"):
  - Serial Bluetooth Terminal / nRF Toolbox: envie F, B, L, R ou S.
    A velocidade pode vir junto: F150 (0 a 255).
    P liga/desliga a patrulha | M mede o microclima agora.
  - Bluefruit Connect: Controller > Control Pad (setas = mover, soltar = parar).
  Os mesmos comandos de texto também funcionam pelo Serial Monitor.
  Qualquer comando manual (F, B, L, R, S ou o Control Pad) desliga a patrulha.

  Ligações:
  - HC-SR04: VCC no 5V da ponte H | GND em comum | TRIG no 12 | ECHO no 13 (divisor 1kΩ/2kΩ)
  - L298N:   IN1 no 4 | IN2 no 5 | IN3 no 6 | IN4 no 7
             ENA no 15 e ENB no 16 (opcional: com os jumpers ENA/ENB encaixados,
             os motores andam sempre na velocidade máxima)
  - DHT11:   módulo de 3 pinos | VCC no 3V3 (NÃO use 5V) | GND em comum | DATA (S) no 8

  Bibliotecas: "DHT sensor library" e "Adafruit Unified Sensor" (Adafruit)
  + biblioteca exportada do Edge Impulse (ei-microclima-arduino-1.0.1-impulse-#1).
  IMPORTANTE: inclua só UMA biblioteca do Edge Impulse por sketch.
*/

#include <microclima_inferencing.h>   // confira o nome em Arduino/libraries/<pasta>/src/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <strings.h>

// ---------------- Pinos ----------------
const int TRIG = 12;
const int ECHO = 13;

const int IN1 = 4;
const int IN2 = 5;
const int IN3 = 6;
const int IN4 = 7;
const int ENA = 15;
const int ENB = 16;

const int PIN_DHT = 8;   // confira se está livre na sua placa

// ---------------- Polaridade dos motores ----------------
// Se um motor girar ao contrário do esperado, mude a chave dele para true/false.
// Teste: F deve ir para frente | B para trás | R girar à direita | L à esquerda.
const bool INVERTER_MOTOR_A = true;   // motor A (IN1/IN2/ENA)
const bool INVERTER_MOTOR_B = true;   // motor B (IN3/IN4/ENB)

// ---------------- Ultrassônico (limiar fixo) ----------------
const float DIST_MIN = 2.0;     // cm
const float DIST_MAX = 100.0;   // cm
const int   JANELA_MEDIA = 5;   // leituras na média móvel
const float DIST_OBSTACULO_CM = 27.0;   // média abaixo disso = obstáculo

// ---------------- Ajustes do carrinho ----------------
const int   TEMPO_GIRO = 400;                    // ms girando no desvio
const unsigned long INTERVALO_INFERENCIA = 100;  // ms entre leituras do ultrassônico
const unsigned long INTERVALO_PRINT = 500;       // ms entre os prints da distância

// ---------------- Microclima (DHT11) ----------------
#define TIPO_DHT DHT11
DHT dht(PIN_DHT, TIPO_DHT);

// IGUAL ao coleta_microclima.ino, que gera o dataset de microclima
const int   JANELA_DHT = 5;                    // leituras na mediana móvel
const unsigned long INTERVALO_DHT = 2000;      // ms entre leituras (o DHT11 pede >= 1 s)
const float TEMP_MAX_NORM = 50.0;              // °C  (IGUAL à coleta)
const float UR_MAX_NORM   = 100.0;             // %   (IGUAL à coleta)

// Confiança mínima do modelo; abaixo disso usa a regra do VPD como reserva
const float LIMIAR_CONFIANCA = 0.6;

// Limiares da regra (reserva), em kPa de VPD (cite a fonte das faixas no README)
const float VPD_UMIDO_ATE   = 0.4;    // abaixo: ar saturado, risco de fungos
const float VPD_IDEAL_ATE   = 1.2;    // 0,4 a 1,2: ideal para hortaliças
const float VPD_ATENCAO_ATE = 2.0;    // 1,2 a 2,0: atenção; acima: crítico
const float TEMP_CRITICA    = 35.0;   // °C: crítico independente do VPD

const int MAX_FALHAS_DHT = 5;                  // falhas seguidas = sensor sem resposta
const unsigned long VALIDADE_DHT = 10000;      // ms: leitura mais velha que isso não vale
const unsigned long TIMEOUT_MEDICAO = 16000;   // ms no máximo parado medindo
const unsigned long COOLDOWN_MEDICAO = 15000;  // ms mínimos entre duas medições da patrulha

const char *NOMES_CLASSE[4] = {"ideal", "umido", "atencao", "critico"};
const char *SUGESTOES[4] = {
  "Condicoes ideais.\n",
  "Ar muito umido: ventilar (risco de fungos).\n",
  "Ar seco/quente: irrigar ou sombrear.\n",
  "ESTRESSE TERMICO: irrigar e sombrear ja!\n"
};

// ---------------- BLE UART (Nordic UART Service) ----------------
#define NOME_BLE     "Carrinho-ESP32"
#define UUID_SERVICO "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // celular -> ESP32
#define UUID_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // ESP32 -> celular

BLECharacteristic *caracteristicaTx = nullptr;
volatile bool celularConectado = false;
bool estavaConectado = false;

// Fila simples: o BLE recebe os bytes em outra tarefa e o loop() processa
const int TAM_FILA = 64;
volatile uint8_t fila[TAM_FILA];
volatile int filaInicio = 0;
volatile int filaFim = 0;

// ---------------- Estado ----------------
float historico[JANELA_MEDIA];
int idxHist = 0;
int qtdHist = 0;

int velocidade = 150;
char comandoAtual = 'S';
bool comandoAberto = false;   // true logo depois de uma letra, para ler a velocidade
int numeroLido = 0;
bool desvioDireita = true;
unsigned long ultimaInferencia = 0;
unsigned long ultimoPrint = 0;

char pacote[5];               // pacote do Bluefruit Connect: ! B botão estado checksum
int tamPacote = -1;           // -1 = não está lendo pacote

// Estado do microclima
float histTemp[JANELA_DHT];
float histUr[JANELA_DHT];
int idxDht = 0;
int qtdDht = 0;
int falhasDht = 0;
unsigned long ultimaAmostraDht = 0;
unsigned long ultimaLeituraDhtOk = 0;

bool modoPatrulha = false;
bool pedidoMedicao = false;     // comando M: o loop() faz a medição
bool cancelarMedicao = false;   // comando manual durante a medição
bool medindo = false;
bool jaMediu = false;
int numeroCanteiro = 0;
unsigned long ultimaMedicao = 0;

// ======================================================
// BLE
// ======================================================
void filaAdicionar(uint8_t b) {
  int proximo = (filaFim + 1) % TAM_FILA;
  if (proximo != filaInicio) {
    fila[filaFim] = b;
    filaFim = proximo;
  }
}

bool filaRetirar(uint8_t &b) {
  if (filaInicio == filaFim) return false;
  b = fila[filaInicio];
  filaInicio = (filaInicio + 1) % TAM_FILA;
  return true;
}

class CallbacksServidor : public BLEServerCallbacks {
  void onConnect(BLEServer *servidor) override { celularConectado = true; }
  void onDisconnect(BLEServer *servidor) override { celularConectado = false; }
};

class CallbacksRx : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto valor = c->getValue();   // funciona nas versões 2.x e 3.x do core ESP32
    for (size_t i = 0; i < valor.length(); i++) {
      filaAdicionar((uint8_t)valor[i]);
    }
  }
};

void iniciarBLE() {
  BLEDevice::init(NOME_BLE);
  BLEServer *servidor = BLEDevice::createServer();
  servidor->setCallbacks(new CallbacksServidor());

  BLEService *servico = servidor->createService(UUID_SERVICO);

  caracteristicaTx = servico->createCharacteristic(UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  caracteristicaTx->addDescriptor(new BLE2902());

  BLECharacteristic *caracteristicaRx = servico->createCharacteristic(
      UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  caracteristicaRx->setCallbacks(new CallbacksRx());

  servico->start();
  BLEAdvertising *anuncio = BLEDevice::getAdvertising();
  anuncio->addServiceUUID(UUID_SERVICO);
  anuncio->setScanResponse(true);
  BLEDevice::startAdvertising();
}

void enviarCelular(const char *mensagem) {
  if (celularConectado && caracteristicaTx != nullptr) {
    caracteristicaTx->setValue((uint8_t *)mensagem, strlen(mensagem));
    caracteristicaTx->notify();
  }
}

// Para textos longos (relatórios): quebra em pedaços de 20 bytes,
// que cabem no MTU padrão do BLE mesmo que o celular não negocie um maior
void enviarTexto(const char *texto) {
  if (!celularConectado || caracteristicaTx == nullptr) return;
  size_t total = strlen(texto);
  for (size_t i = 0; i < total; i += 20) {
    size_t n = total - i;
    if (n > 20) n = 20;
    caracteristicaTx->setValue((uint8_t *)(texto + i), n);
    caracteristicaTx->notify();
    delay(15);
  }
}

void cuidarConexao() {
  if (!celularConectado && estavaConectado) {
    parar();                        // segurança: perdeu o celular, para o carrinho
    comandoAtual = 'S';
    modoPatrulha = false;           // e desliga a patrulha
    cancelarMedicao = true;
    delay(300);
    BLEDevice::startAdvertising();  // volta a aparecer para conectar de novo
    Serial.println("Celular desconectado");
  }
  if (celularConectado && !estavaConectado) {
    Serial.println("Celular conectado");
  }
  estavaConectado = celularConectado;
}

// ======================================================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  dht.begin();

  limparLeituras();
  parar();
  iniciarBLE();

  Serial.println("Carrinho pronto. Conecte no BLE 'Carrinho-ESP32'.");
  Serial.println("Comandos: F B L R S | P = patrulha | M = medir microclima");
  Serial.printf("Modelo de microclima: %d entradas, %d classes\n",
                (int)EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, (int)EI_CLASSIFIER_LABEL_COUNT);
}

void loop() {
  // comandos do celular (BLE) e do Serial Monitor
  lerComandos();

  cuidarConexao();

  amostrarDHT();   // leitura do DHT11 a cada INTERVALO_DHT, sem travar o loop

  if (pedidoMedicao && !medindo) {   // comando M
    pedidoMedicao = false;
    modoPatrulha = false;
    comandoAtual = 'S';
    parar();
    medirMicroclima("Agora");
  }

  if (millis() - ultimaInferencia >= INTERVALO_INFERENCIA) {
    ultimaInferencia = millis();

    float distancia = medirDistancia();
    float mediaCm = mediaDistancia(distancia);
    bool obstaculo = (qtdHist >= JANELA_MEDIA) && (mediaCm < DIST_OBSTACULO_CM);

    if (millis() - ultimoPrint >= INTERVALO_PRINT) {
      ultimoPrint = millis();
      Serial.printf("dist: %.1f cm | media: %.1f cm | obstaculo: %s | cmd: %c\n",
                    distancia, mediaCm, obstaculo ? "sim" : "nao", comandoAtual);
    }

    if (comandoAtual == 'F' && obstaculo) {
      tratarObstaculo();
    }
  }
}

void lerComandos() {
  uint8_t b;
  while (filaRetirar(b)) processarByte((char)b);
  while (Serial.available()) processarByte((char)Serial.read());
}

// ======================================================
// Sensor ultrassônico
// ======================================================
float medirDistancia() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long duracao = pulseIn(ECHO, HIGH, 30000);  // timeout de 30 ms
  if (duracao == 0) return DIST_MAX;          // sem eco = nada por perto

  return duracao * 0.0343 / 2.0;
}

// Limita a faixa e devolve a média móvel em cm
float mediaDistancia(float distancia) {
  distancia = constrain(distancia, DIST_MIN, DIST_MAX);

  historico[idxHist] = distancia;
  idxHist = (idxHist + 1) % JANELA_MEDIA;
  if (qtdHist < JANELA_MEDIA) qtdHist++;

  float soma = 0;
  for (int i = 0; i < qtdHist; i++) soma += historico[i];
  return soma / qtdHist;
}

void limparLeituras() {
  idxHist = 0;
  qtdHist = 0;
}

// ======================================================
// Microclima (DHT11)
// ======================================================

// Lê o DHT11 a cada INTERVALO_DHT e guarda na janela da mediana móvel
void amostrarDHT() {
  if (millis() - ultimaAmostraDht < INTERVALO_DHT) return;
  ultimaAmostraDht = millis();

  float t = dht.readTemperature();
  float u = dht.readHumidity();

  if (isnan(t) || isnan(u)) {
    falhasDht++;
    if (falhasDht == MAX_FALHAS_DHT) {
      Serial.println("!! DHT11 sem resposta. Verifique VCC (3V3), GND e DATA (GPIO 8).");
      enviarCelular("DHT11 sem resposta\n");
    }
    return;
  }

  falhasDht = 0;
  ultimaLeituraDhtOk = millis();

  histTemp[idxDht] = t;
  histUr[idxDht] = u;
  idxDht = (idxDht + 1) % JANELA_DHT;
  if (qtdDht < JANELA_DHT) qtdDht++;

  Serial.printf("DHT11: %.0f C | %.0f %% | VPD %.2f kPa\n", t, u, calcularVPD(t, u));
}

void limparHistoricoDHT() {
  idxDht = 0;
  qtdDht = 0;
}

// Só vale se a janela estiver cheia, o sensor responder e a última leitura for recente
bool dhtValido() {
  return qtdDht >= JANELA_DHT &&
         falhasDht < MAX_FALHAS_DHT &&
         (millis() - ultimaLeituraDhtOk) < VALIDADE_DHT;
}

float medianaDht(const float *valores, int n) {
  float copia[JANELA_DHT];
  for (int i = 0; i < n; i++) copia[i] = valores[i];
  for (int i = 1; i < n; i++) {               // ordenação simples (n pequeno)
    float x = copia[i];
    int j = i - 1;
    while (j >= 0 && copia[j] > x) {
      copia[j + 1] = copia[j];
      j--;
    }
    copia[j + 1] = x;
  }
  return copia[n / 2];
}

// Déficit de pressão de vapor em kPa (temperatura em °C, umidade relativa em %)
float calcularVPD(float tempC, float urPct) {
  float es = 0.6108 * exp(17.27 * tempC / (tempC + 237.3));   // pressão de saturação
  return es * (1.0 - urPct / 100.0);
}

// Regra de referência (reserva): 0 ideal | 1 umido | 2 atencao | 3 critico
int classificarRegra(float tempC, float vpd) {
  if (tempC >= TEMP_CRITICA || vpd >= VPD_ATENCAO_ATE) return 3;
  if (vpd < VPD_UMIDO_ATE) return 1;
  if (vpd < VPD_IDEAL_ATE) return 0;
  return 2;
}

// ======================================================
// TinyML do microclima
// ======================================================

// Converte o rótulo do Edge Impulse em índice (0 ideal | 1 umido | 2 atencao | 3 critico).
// Procura pelo NOME, e não pela posição: o Edge Impulse ordena os rótulos em ordem alfabética.
int indiceDoRotulo(const char *rotulo) {
  for (int i = 0; i < 4; i++) {
    if (strcasecmp(rotulo, NOMES_CLASSE[i]) == 0) return i;
  }
  return -1;
}

// Roda o modelo. Retorna 0..3, ou -1 se o modelo falhar ou ficar incerto.
// A entrada é a MESMA da coleta: {temp_n, ur_n} = {T/50, UR/100}.
int classificarModelo(float tempC, float urPct, float &confianca) {
  confianca = 0;

  if (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE != 2) {
    Serial.println("!! Modelo espera outro numero de entradas (esperado: 2). Revise a janela no Edge Impulse.");
    return -1;
  }

  float entrada[2] = { tempC / TEMP_MAX_NORM, urPct / UR_MAX_NORM };

  signal_t sinal;
  if (numpy::signal_from_buffer(entrada, 2, &sinal) != 0) return -1;

  ei_impulse_result_t resultado = { 0 };
  if (run_classifier(&sinal, &resultado, false) != EI_IMPULSE_OK) {
    Serial.println("!! Erro ao rodar o modelo de microclima");
    return -1;
  }

  int melhor = -1;
  float maior = 0;
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (resultado.classification[i].value > maior) {
      maior = resultado.classification[i].value;
      melhor = indiceDoRotulo(resultado.classification[i].label);
    }
  }

  confianca = maior;
  if (melhor < 0 || maior < LIMIAR_CONFIANCA) return -1;   // incerto
  return melhor;
}

// Modelo primeiro; se falhar ou ficar incerto, usa a regra do VPD como reserva.
// usouModelo diz qual dos dois decidiu (aparece no log e no relatório).
int classificarMicroclima(float tempC, float urPct, float vpd, bool &usouModelo, float &confianca) {
  int classeModelo = classificarModelo(tempC, urPct, confianca);
  int classeRegra = classificarRegra(tempC, vpd);

  if (classeModelo >= 0) {
    usouModelo = true;
    if (classeModelo != classeRegra) {
      Serial.printf("   (modelo: %s | regra: %s)\n",
                    NOMES_CLASSE[classeModelo], NOMES_CLASSE[classeRegra]);
    }
    return classeModelo;
  }

  usouModelo = false;
  Serial.printf("   Modelo incerto ou falhou (conf %.2f): usando a regra do VPD\n", confianca);
  return classeRegra;
}

// Para o carrinho, espera a janela do DHT11 encher com leituras novas e relata.
// Retorna false se um comando manual (ou a perda do celular) cancelou a medição.
bool medirMicroclima(const char *rotulo) {
  medindo = true;
  cancelarMedicao = false;
  parar();

  Serial.printf(">> %s: parado, medindo microclima...\n", rotulo);
  enviarTexto("Medindo microclima\n");

  limparHistoricoDHT();   // só vale o que for lido aqui, neste ponto
  unsigned long inicio = millis();
  while (qtdDht < JANELA_DHT && millis() - inicio < TIMEOUT_MEDICAO && !cancelarMedicao) {
    lerComandos();        // mantém BLE e Serial respondendo
    cuidarConexao();
    amostrarDHT();
    delay(20);
  }
  medindo = false;

  if (cancelarMedicao) {
    Serial.println(">> Medicao cancelada");
    return false;
  }

  relatarMicroclima(rotulo);
  return true;
}

void relatarMicroclima(const char *rotulo) {
  char msg[96];

  if (!dhtValido()) {
    Serial.printf(">> %s: DHT11 sem leitura valida\n", rotulo);
    snprintf(msg, sizeof(msg), "%s: sem leitura\n", rotulo);
    enviarTexto(msg);
    return;
  }

  float t = medianaDht(histTemp, qtdDht);
  float u = medianaDht(histUr, qtdDht);
  float vpd = calcularVPD(t, u);

  bool usouModelo;
  float confianca;
  int classe = classificarMicroclima(t, u, vpd, usouModelo, confianca);

  Serial.printf(">> %s: %.1f C | %.0f %% | VPD %.2f kPa | %s (%s, conf %.2f)\n",
                rotulo, t, u, vpd, NOMES_CLASSE[classe],
                usouModelo ? "TinyML" : "regra", confianca);

  snprintf(msg, sizeof(msg), "%s: %.0fC %.0f%% VPD %.1f -> %s (%s)\n",
           rotulo, t, u, vpd, NOMES_CLASSE[classe], usouModelo ? "ML" : "regra");
  enviarTexto(msg);
  enviarTexto(SUGESTOES[classe]);
}

void alternarPatrulha() {
  if (modoPatrulha) {
    modoPatrulha = false;
    cancelarMedicao = true;
    comandoAtual = 'S';
    parar();
    Serial.println(">> Patrulha DESLIGADA");
    enviarCelular("Patrulha desligada\n");
  } else {
    modoPatrulha = true;
    cancelarMedicao = false;
    jaMediu = false;
    numeroCanteiro = 0;
    comandoAtual = 'F';
    moverFrente();
    Serial.println(">> Patrulha LIGADA");
    enviarCelular("Patrulha ligada\n");
  }
}

// Obstáculo à frente: na patrulha mede o microclima antes de desviar
void tratarObstaculo() {
  if (modoPatrulha && (!jaMediu || millis() - ultimaMedicao >= COOLDOWN_MEDICAO)) {
    char rotulo[16];
    snprintf(rotulo, sizeof(rotulo), "Canteiro %d", numeroCanteiro + 1);

    bool concluida = medirMicroclima(rotulo);
    if (!concluida || !modoPatrulha) return;   // o usuário assumiu o controle

    numeroCanteiro++;
    jaMediu = true;
    ultimaMedicao = millis();
  }
  desviar();   // sem patrulha (ou no intervalo entre medições): desvio normal
}

// ======================================================
// Desvio
// ======================================================
void desviar() {
  parar();
  delay(150);

  if (desvioDireita) {
    Serial.println(">> Obstaculo! Desviando para a DIREITA");
    enviarCelular("Obstaculo: desviando para a direita\n");
    girarDireita();
  } else {
    Serial.println(">> Obstaculo! Desviando para a ESQUERDA");
    enviarCelular("Obstaculo: desviando para a esquerda\n");
    girarEsquerda();
  }

  desvioDireita = !desvioDireita;   // alterna o lado no próximo desvio
  delay(TEMPO_GIRO);

  limparLeituras();   // descarta as leituras antigas do obstáculo
  moverFrente();
}

// ======================================================
// Comandos
// ======================================================
void processarByte(char c) {
  // Pacote do Bluefruit Connect (Control Pad): '!' 'B' botão estado checksum
  if (c == '!') {
    tamPacote = 0;
    pacote[tamPacote++] = c;
    return;
  }
  if (tamPacote >= 0) {
    pacote[tamPacote++] = c;
    if (tamPacote == 2 && c != 'B') { tamPacote = -1; return; }
    if (tamPacote == 5) {
      tratarBotao(pacote[2], pacote[3]);
      tamPacote = -1;
    }
    return;
  }

  // Comandos de texto: F, B, L, R, S (+ velocidade opcional, ex: F150)
  // e os do microclima: P (patrulha) e M (medir agora)
  char letra = toupper(c);
  if (letra == 'P') {
    comandoAberto = false;
    alternarPatrulha();
  } else if (letra == 'M') {
    comandoAberto = false;
    pedidoMedicao = true;
  } else if (letra == 'F' || letra == 'B' || letra == 'L' || letra == 'R' || letra == 'S') {
    modoPatrulha = false;       // controle manual desliga a patrulha
    cancelarMedicao = true;
    comandoAtual = letra;
    comandoAberto = true;
    numeroLido = 0;
    aplicarComando();
  } else if (isDigit(c) && comandoAberto) {
    numeroLido = numeroLido * 10 + (c - '0');
    if (numeroLido <= 255) {
      velocidade = numeroLido;
      aplicarComando();
    }
  } else if (c == '\n' || c == '\r' || c == ' ') {
    comandoAberto = false;
  } else {
    comandoAberto = false;
    modoPatrulha = false;
    cancelarMedicao = true;
    comandoAtual = 'S';     // qualquer outro caractere para o carrinho
    aplicarComando();
  }
}

void tratarBotao(char botao, char estado) {
  modoPatrulha = false;              // controle manual desliga a patrulha
  cancelarMedicao = true;
  if (estado == '1') {               // botão apertado
    switch (botao) {
      case '5': comandoAtual = 'F'; break;   // seta para cima
      case '6': comandoAtual = 'B'; break;   // seta para baixo
      case '7': comandoAtual = 'L'; break;   // seta para a esquerda
      case '8': comandoAtual = 'R'; break;   // seta para a direita
      default: return;                       // botões 1–4 não fazem nada
    }
  } else {                           // botão solto
    comandoAtual = 'S';
  }
  aplicarComando();
}

void aplicarComando() {
  switch (comandoAtual) {
    case 'F': moverFrente();   break;
    case 'B': moverTras();     break;
    case 'L': girarEsquerda(); break;
    case 'R': girarDireita();  break;
    default:  parar();         break;
  }
}

// ======================================================
// Motores
// ======================================================

// sentido: 1 = frente | -1 = tras | 0 = parado
// A inversão de polaridade (INVERTER_MOTOR_A/B) é aplicada aqui, em um só lugar.
void motorA(int sentido) {
  if (INVERTER_MOTOR_A) sentido = -sentido;
  digitalWrite(IN1, sentido > 0 ? HIGH : LOW);
  digitalWrite(IN2, sentido < 0 ? HIGH : LOW);
  analogWrite(ENA, sentido == 0 ? 0 : velocidade);
}

void motorB(int sentido) {
  if (INVERTER_MOTOR_B) sentido = -sentido;
  digitalWrite(IN3, sentido > 0 ? HIGH : LOW);
  digitalWrite(IN4, sentido < 0 ? HIGH : LOW);
  analogWrite(ENB, sentido == 0 ? 0 : velocidade);
}

void moverFrente() {
  motorA(1);
  motorB(1);
}

void moverTras() {
  motorA(-1);
  motorB(-1);
}

void girarDireita() {
  motorA(-1);
  motorB(1);
}

void girarEsquerda() {
  motorA(1);
  motorB(-1);
}

void parar() {
  motorA(0);
  motorB(0);
}