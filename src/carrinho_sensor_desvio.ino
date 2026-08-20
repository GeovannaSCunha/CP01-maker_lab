// Sensor ultrassônico HC-SR04 + Motor L298N
// Ao detectar obstáculo próximo, o carrinho para, desvia (gira) e segue em frente
//
// Ligação HC-SR04: VCC->5V, GND->GND, Trig->9, Echo->10
// Ligação L298N: ENA->5, IN1->6, IN2->7, IN3->8, IN4->11, ENB->12

const int pinoTrig = 9;
const int pinoEcho = 10;

const int ENA = 5;
const int IN1 = 6;
const int IN2 = 7;
const int IN3 = 8;
const int IN4 = 11;
const int ENB = 12;

const float distanciaMinima = 15.0; // cm - ajuste conforme necessário
const int velocidade = 150;         // 0-255
const int tempoDeGiro = 400;        // ms - ajuste conforme o quanto ele deve girar

bool proximoDesvioDireita = true; // alterna o lado do desvio a cada obstáculo

void setup() {
  Serial.begin(9600);
  pinMode(pinoTrig, OUTPUT);
  pinMode(pinoEcho, INPUT);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENB, OUTPUT);

  moverFrente();
}

void loop() {
  float distancia = medirDistancia();

  Serial.print("Distancia: ");
  Serial.print(distancia);
  Serial.println(" cm");

  if (distancia > 0 && distancia < distanciaMinima) {
    parar();
    delay(150);

    if (proximoDesvioDireita) {
      Serial.println(">> Obstaculo detectado! Desviando para DIREITA");
      girarDireita();
    } else {
      Serial.println(">> Obstaculo detectado! Desviando para ESQUERDA");
      girarEsquerda();
    }

    proximoDesvioDireita = !proximoDesvioDireita; // alterna o lado pro próximo desvio
    delay(tempoDeGiro);

    moverFrente();
  }

  delay(200);
}

float medirDistancia() {
  digitalWrite(pinoTrig, LOW);
  delayMicroseconds(2);
  digitalWrite(pinoTrig, HIGH);
  delayMicroseconds(10);
  digitalWrite(pinoTrig, LOW);

  long duracao = pulseIn(pinoEcho, HIGH, 30000); // timeout de 30ms (~5m)
  if (duracao == 0) return -1; // sem retorno de eco

  return duracao * 0.034 / 2;
}

void moverFrente() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
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

// Gira girando as rodas em sentidos opostos (giro no próprio eixo)
void girarDireita() {
  digitalWrite(IN1, LOW);   // roda esquerda vai pra frente
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);  // roda direita vai pra trás
  digitalWrite(IN4, LOW);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}

void girarEsquerda() {
  digitalWrite(IN1, HIGH);  // roda esquerda vai pra trás
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);   // roda direita vai pra frente
  digitalWrite(IN4, HIGH);
  analogWrite(ENA, velocidade);
  analogWrite(ENB, velocidade);
}
