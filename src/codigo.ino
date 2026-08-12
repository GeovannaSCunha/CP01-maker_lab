#define IN1 3 // Roda esquerda 1
#define IN2 4 // Roda esquerda 2
#define IN3 5 // Roda direita 1
#define IN4 6 // Roda direita 2

char comando;

// Funções
void parar()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void andar_frente()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  delay(10);
}

void andar_tras()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  delay(10);
}

void andar_direita()
{
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  delay(10);
}

void andar_esquerda()
{
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  delay(10);
}

void setup() {
  Serial.begin(9600);
}

void loop() {
  if(Serial.available() > 0)
  {
    comando = Serial.read(); // lê um caractere | se a resposta for string, devemos usar comando = Serial.readString();
    if(comando == 'F')
    {
      andar_frente(); // função
    }else if(comando == 'B') // Se for ler uma string, deve trocar 'B' por "B" (aspas simples por aspas duplas)
    {
      andar_tras(); // função
    }else if(comando == 'R') // Se for ler uma string, deve trocar 'R' por "R" (aspas simples por aspas duplas)
    {
      andar_direita(); // função
    }else if(comando == 'L') // Se for ler uma string, deve trocar 'L' por "L" (aspas simples por aspas duplas)
    {
      andar_esquerda();
    }else {
      parar();
    }
  }
}
