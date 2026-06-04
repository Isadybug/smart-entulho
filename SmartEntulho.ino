#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Definição dos Pinos de Hardware
const int PIN_TRIG = 12;
const int PIN_ECHO = 13;
const int LED_AMARELO = 14;
const int LED_VERDE = 27;
const int LED_VERMELHO = 26;

// Parâmetros da lixeira (em centímetros)
const float ALTURA_MAXIMA_LIXEIRA = 50.0;

// Identificador único desta lixeira
const char* ID_LIXEIRA = "MACK_URB_001";

// Tópico MQTT onde os dados são publicados
const char* MQTT_TOPIC = "v1/smartbin/status";

WiFiClientSecure espClient;
PubSubClient client(espClient);
unsigned long ultimaMensagem = 0;

void conectar_wifi() {
  delay(10);
  Serial.print("Conectando ao WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado! IP: " + WiFi.localIP().toString());
}

void reconectar_mqtt() {
  while (!client.connected()) {
    Serial.print("Tentando conexão MQTT...");
    String clientId = "ESP32_Lixeira_";
    clientId += String(random(0, 1000));

    if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println("Conectado ao broker MQTT!");
    } else {
      Serial.print("Falhou, rc=");
      Serial.print(client.state());
      Serial.println(" — nova tentativa em 5s...");
      delay(5000);
    }
  }
}

float lerDistancia_MediaMovel() {
  float somaDistancias = 0;
  int leiturasValidas = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    long duracao = pulseIn(PIN_ECHO, HIGH, 30000);
    float distancia = duracao * 0.0343 / 2.0;

    if (distancia > 2.0 && distancia <= (ALTURA_MAXIMA_LIXEIRA + 10.0)) {
      somaDistancias += distancia;
      leiturasValidas++;
    }
    delay(40);
  }

  if (leiturasValidas == 0) return ALTURA_MAXIMA_LIXEIRA;
  return somaDistancias / leiturasValidas;
}

void atualizarLEDs(float percentual) {
  if (percentual < 50.0) {
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_AMARELO, LOW);
    digitalWrite(LED_VERMELHO, LOW);
  } else if (percentual <= 80.0) {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARELO, HIGH);
    digitalWrite(LED_VERMELHO, LOW);
  } else {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARELO, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  conectar_wifi();

  // setInsecure() aceita qualquer certificado TLS — suficiente para protótipo.
  // Em produção, use espClient.setCACert() com o certificado do broker.
  espClient.setInsecure();
  client.setServer(MQTT_SERVER, MQTT_PORT);
}

void loop() {
  if (!client.connected()) {
    reconectar_mqtt();
  }
  client.loop();

  unsigned long agora = millis();
  if (agora - ultimaMensagem < 1000) return;
  ultimaMensagem = agora;

  float distancia = lerDistancia_MediaMovel();
  if (distancia > ALTURA_MAXIMA_LIXEIRA) distancia = ALTURA_MAXIMA_LIXEIRA;

  float lixoAltura = ALTURA_MAXIMA_LIXEIRA - distancia;
  float percentual = constrain((lixoAltura / ALTURA_MAXIMA_LIXEIRA) * 100.0, 0.0, 100.0);

  Serial.printf("Distancia: %.1f cm | Preenchimento: %.1f%%\n", distancia, percentual);

  atualizarLEDs(percentual);

  const char* status;
  if (percentual > 80.0)      status = "CRITICO";
  else if (percentual >= 50.0) status = "ALERTA";
  else                         status = "OK";

  StaticJsonDocument<128> doc;
  doc["id_lixeira"]         = ID_LIXEIRA;
  doc["volume_porcentagem"] = serialized(String(percentual, 1));
  doc["status"]             = status;

  char buffer[128];
  serializeJson(doc, buffer);
  client.publish(MQTT_TOPIC, buffer);
}
