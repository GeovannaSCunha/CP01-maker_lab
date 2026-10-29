/*
  tinyml_inferencia.ino
  Carrinho com ESP32-S3 + HC-SR04 + L298N + BLE + TinyML (Edge Impulse)

  Project-based Maker Lab — FIAP — Turma 4ESPX
  Geovanna Silva Cunha (RM97736)
  Victor Camargo Maciel (RM98384)
  Pedro Emerici Gava (RM551043)

  Funcionamento:
  - O HC-SR04 mede a distância, que passa pelo MESMO pré-processamento
    usado na coleta do dataset (limite de faixa + média móvel + normalização 0–1).
  - O modelo treinado no Edge Impulse classifica a leitura em
    "0" (livre, a partir de ~30 cm) ou "1" (obstáculo, até ~25 cm).
  - Se der obstáculo enquanto o carrinho anda para frente, ele para,
    gira (alternando direita/esquerda) e volta a andar.
  - O celular controla o carrinho por BLE (o ESP32-S3 não tem Bluetooth clássico).

  Controle pelo celular (BLE UART, aparece como "Carrinho-ESP32"):
  - Serial Bluetooth Terminal / nRF Toolbox: envie F, B, L, R ou S.
    A velocidade pode vir junto: F150 (0 a 255).
  - Bluefruit Connect: Controller > Control Pad (setas = mover, soltar = parar).
  Os mesmos comandos de texto também funcionam pelo Serial Monitor.

  Ligações:
  - HC-SR04: VCC no 5V da ponte H | GND em comum | TRIG no 12 | ECHO no 13 (divisor 1kΩ/2kΩ)
  - L298N:   IN1 no 4 | IN2 no 5 | IN3 no 6 | IN4 no 7
             ENA no 15 e ENB no 16 (opcional: com os jumpers ENA/ENB encaixados,
             os motores andam sempre na velocidade máxima)
*/

#include <victormaciel13-project-1_inferencing.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------------- Pinos ----------------
const int TRIG = 12;
const int ECHO = 13;

const int IN1 = 4;
const int IN2 = 5;
const int IN3 = 6;
const int IN4 = 7;
const int ENA = 15;
const int ENB = 16;

// ---------------- Pré-processamento ----------------
// IGUAL ao coleta_dataset.ino, que gerou o dataset.csv
const float DIST_MIN = 2.0;     // cm
const float DIST_MAX = 100.0;   // cm
const int   JANELA_MEDIA = 5;   // leituras na média móvel

// ---------------- Ajustes do carrinho ----------------
const float LIMIAR_OBSTACULO = 0.6;              // probabilidade mínima para desviar
const int   TEMPO_GIRO = 400;                    // ms girando no desvio
const unsigned long INTERVALO_INFERENCIA = 100;  // ms entre leituras

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

float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

int velocidade = 150;
char comandoAtual = 'S';
bool comandoAberto = false;   // true logo depois de uma letra, para ler a velocidade
int numeroLido = 0;
bool desvioDireita = true;
unsigned long ultimaInferencia = 0;

char pacote[5];               // pacote do Bluefruit Connect: ! B botão estado checksum
int tamPacote = -1;           // -1 = não está lendo pacote

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

void cuidarConexao() {
  if (!celularConectado && estavaConectado) {
    parar();                        // segurança: perdeu o celular, para o carrinho
    comandoAtual = 'S';
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

  limparLeituras();
  parar();
  iniciarBLE();

  Serial.println("Carrinho pronto. Conecte no BLE 'Carrinho-ESP32'.");
}

void loop() {
  // comandos do celular (BLE) e do Serial Monitor
  uint8_t b;
  while (filaRetirar(b)) processarByte((char)b);
  while (Serial.available()) processarByte((char)Serial.read());

  cuidarConexao();

  if (millis() - ultimaInferencia >= INTERVALO_INFERENCIA) {
    ultimaInferencia = millis();

    float distancia = medirDistancia();
    float normalizado = preprocessar(distancia);
    adicionarFeature(normalizado);

    float pObstaculo = probabilidadeObstaculo();

    Serial.printf("dist: %.1f cm | norm: %.3f | P(obstaculo): %.2f | cmd: %c\n",
                  distancia, normalizado, pObstaculo, comandoAtual);

    if (comandoAtual == 'F' && pObstaculo > LIMIAR_OBSTACULO) {
      desviar();
    }
  }
}

// ======================================================
// Sensor e pré-processamento
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

void adicionarFeature(float valor) {
  for (int i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 1; i++) {
    features[i] = features[i + 1];
  }
  features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - 1] = valor;
}

void limparLeituras() {
  idxHist = 0;
  qtdHist = 0;
  for (int i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i++) {
    features[i] = 1.0;   // começa como "caminho livre"
  }
}

// ======================================================
// TinyML
// ======================================================
float probabilidadeObstaculo() {
  signal_t sinal;
  numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &sinal);

  ei_impulse_result_t resultado = { 0 };
  if (run_classifier(&sinal, &resultado, false) != EI_IMPULSE_OK) {
    Serial.println("Erro ao rodar o modelo");
    return 0;
  }

  // Procura a classe de obstáculo pelo nome, e não pela posição
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    const char *rotulo = resultado.classification[i].label;
    if (strcmp(rotulo, "1") == 0 || strcmp(rotulo, "obstaculo") == 0) {
      return resultado.classification[i].value;
    }
  }
  return 0;
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
  char letra = toupper(c);
  if (letra == 'F' || letra == 'B' || letra == 'L' || letra == 'R' || letra == 'S') {
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
    comandoAtual = 'S';     // qualquer outro caractere para o carrinho
    aplicarComando();
  }
}

void tratarBotao(char botao, char estado) {
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
void moverFrente() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}

void moverTras() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}

void girarDireita() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}

void girarEsquerda() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}

void parar() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}
