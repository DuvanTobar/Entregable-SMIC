//Codigo que combina la logica de ambas ESP, tanto la ESP32 como la ESP32-S3, este seria el completo.
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <DHT.h>
#include <ESP32Servo.h>

const char* SSID      = "Redmi";
const char* PASS      = "23092018";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;

#define TOPIC_RFID1        "esp32/rfid1"
#define TOPIC_RFID2        "esp32/rfid2"
#define TOPIC_CMD_MOTOR    "esp32/cmd/motor"
#define TOPIC_STATUS       "esp32/status"
#define TOPIC_TEMP         "invernadero/temperatura"
#define TOPIC_HUM          "esp32/hum"
#define TOPIC_BOMBA        "esp32/bomb"
#define TOPIC_TECHO        "invernadero/techo/estado"
#define TOPIC_ACCESO1      "invernadero/acceso/puerta1"
#define TOPIC_ACCESO2      "invernadero/acceso/puerta2"
#define TOPIC_CMD_BOMBA    "invernadero/cmd/bomba"
#define TOPIC_MOVIMIENTO1  "esp32/movimiento1"
#define TOPIC_SERVO1       "esp32/servo1"
#define TOPIC_MOTORES      "esp32/motores"

#define SCK_PIN   12
#define MISO_PIN  13
#define MOSI_PIN  11
#define SS_1_PIN  10
#define RST_1_PIN  9
#define SS_2_PIN  15
#define RST_2_PIN 21

#define L293D_ENA  38
#define L293D_IN1  39
#define L293D_IN2  40
#define VELOCIDAD  200

#define DHTPIN   25
#define DHTTYPE  DHT11

#define PIN_RELE    23
#define SERVO1_PIN   4
#define PIR1_PIN    14

#define PWM_FREQ_HZ    5000
#define PWM_RESOLUTION    8
#define DEBOUNCE_MS    2000
#define HEARTBEAT_MS  30000
#define WIFI_RETRY_MS  5000
#define MQTT_RETRY_MS  3000

SPIClass spiRFID(FSPI);
MFRC522 rfid1(SS_1_PIN, RST_1_PIN);
MFRC522 rfid2(SS_2_PIN, RST_2_PIN);

DHT dht(DHTPIN, DHTTYPE);
Servo servo1;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

String lastUID1 = "", lastUID2 = "";
unsigned long lastTime1 = 0, lastTime2 = 0;
unsigned long lastHeartbeat = 0, lastWifiCheck = 0, lastMqttRetry = 0;
unsigned long ultimaLectura = 0, ultimoEncendido = 0;

int  motorSpeed = 0;
bool motorDir   = true;
bool techoCerrado     = false;
bool motoresActivos   = false;
bool motoresEncendidos = false;
bool servoActivo      = false;
unsigned long servoInicio = 0;
int pirAnterior = LOW;

const unsigned long INTERVALO_MOTOR = 10000;
const unsigned long DURACION_MOTOR  =  3000;
const unsigned long SERVO_MS        =  1000;
const unsigned long INTERVALO_SENSOR = 3000;

String uidToString(MFRC522& lector) {
  String uid = "";
  for (byte i = 0; i < lector.uid.size; i++) {
    if (lector.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(lector.uid.uidByte[i], HEX);
    if (i < lector.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

void motorStop() {
  ledcWrite(L293D_ENA, 0);
  digitalWrite(L293D_IN1, LOW);
  digitalWrite(L293D_IN2, LOW);
  motorSpeed = 0;
}

void motorSet(int speed, bool forward) {
  speed = constrain(speed, 0, 255);
  motorSpeed = speed;
  motorDir   = forward;
  if (speed == 0) { motorStop(); return; }
  digitalWrite(L293D_IN1, forward ? HIGH : LOW);
  digitalWrite(L293D_IN2, forward ? LOW  : HIGH);
  ledcWrite(L293D_ENA, speed);
}

void parseMotorCommand(const String& payload) {
  int speedVal = motorSpeed;
  int idx = payload.indexOf("\"speed\":");
  if (idx != -1) speedVal = payload.substring(idx + 8).toInt();
  bool fwd = motorDir;
  if (payload.indexOf("fwd") != -1) fwd = true;
  if (payload.indexOf("rev") != -1) fwd = false;
  motorSet(speedVal, fwd);
}

void activarBomba(int segundos) {
  mqtt.publish(TOPIC_BOMBA, "encendida");
  digitalWrite(PIN_RELE, LOW);
  delay(segundos * 1000);
  digitalWrite(PIN_RELE, HIGH);
  mqtt.publish(TOPIC_BOMBA, "apagada");
}

void apagarBomba() {
  digitalWrite(PIN_RELE, HIGH);
  mqtt.publish(TOPIC_BOMBA, "apagada");
}

void abrirServo() {
  servo1.write(180);
  servoActivo = true;
  servoInicio = millis();
}

void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(SSID, PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(300);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "", t = String(topic);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  if (t == TOPIC_CMD_MOTOR)  parseMotorCommand(msg);
  if (t == TOPIC_CMD_BOMBA)  { if (msg == "ON") activarBomba(3); else if (msg == "OFF") apagarBomba(); }
  if (t == TOPIC_SERVO1)     { if (msg == "1" || msg == "true") abrirServo(); }
  if (t == TOPIC_MOTORES) {
    if (msg == "1" || msg == "true") {
      motoresActivos  = true;
      ultimoEncendido = millis() - INTERVALO_MOTOR;
    } else {
      motoresActivos    = false;
      motoresEncendidos = false;
      digitalWrite(L293D_IN1, LOW);
      digitalWrite(L293D_IN2, LOW);
      analogWrite(L293D_ENA, 0);
    }
  }
}

bool mqttConnect() {
  if (mqtt.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  String clientId = "ESP32S3-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                         TOPIC_STATUS, 0, false, "{\"online\":false}");
  if (ok) {
    mqtt.subscribe(TOPIC_CMD_MOTOR);
    mqtt.subscribe(TOPIC_CMD_BOMBA);
    mqtt.subscribe(TOPIC_SERVO1);
    mqtt.subscribe(TOPIC_MOTORES);
    mqtt.publish(TOPIC_STATUS, "{\"online\":true}", false);
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(SS_1_PIN, OUTPUT); digitalWrite(SS_1_PIN, HIGH);
  pinMode(SS_2_PIN, OUTPUT); digitalWrite(SS_2_PIN, HIGH);
  spiRFID.begin(SCK_PIN, MISO_PIN, MOSI_PIN);
  rfid1.PCD_Init(&spiRFID);
  rfid1.PCD_SetAntennaGain(rfid1.RxGain_max);
  rfid2.PCD_Init(&spiRFID);
  rfid2.PCD_SetAntennaGain(rfid2.RxGain_max);

  ledcAttach(L293D_ENA, PWM_FREQ_HZ, PWM_RESOLUTION);
  pinMode(L293D_IN1, OUTPUT); pinMode(L293D_IN2, OUTPUT);
  motorStop();

  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, HIGH);

  dht.begin();

  servo1.setPeriodHertz(50);
  servo1.attach(SERVO1_PIN, 500, 2400);
  servo1.write(0);

  pinMode(PIR1_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiConnect();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(60);
  mqtt.setBufferSize(512);
  mqttConnect();
}

void loop() {
  unsigned long now = millis();

  if (now - lastWifiCheck > WIFI_RETRY_MS) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) wifiConnect();
  }

  if (!mqtt.connected() && now - lastMqttRetry > MQTT_RETRY_MS) {
    lastMqttRetry = now;
    mqttConnect();
  }
  mqtt.loop();

  if (now - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = now;
    String hb = "{\"online\":true,\"rssi\":" + String(WiFi.RSSI()) +
                ",\"motor\":" + String(motorSpeed) + "}";
    mqtt.publish(TOPIC_STATUS, hb.c_str(), false);
  }

  if (now - ultimaLectura >= INTERVALO_SENSOR) {
    ultimaLectura = now;
    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();
    if (!isnan(hum) && !isnan(temp)) {
      char bufTemp[8], bufHum[8];
      dtostrf(temp, 4, 1, bufTemp);
      dtostrf(hum,  4, 1, bufHum);
      mqtt.publish(TOPIC_TEMP, bufTemp);
      mqtt.publish(TOPIC_HUM,  bufHum);
      if (temp > 26 && !techoCerrado) {
        mqtt.publish(TOPIC_TECHO, "cerrado");
        activarBomba(3);
        techoCerrado = true;
      } else if (temp <= 24 && hum < 70 && techoCerrado) {
        mqtt.publish(TOPIC_TECHO, "abierto");
        apagarBomba();
        techoCerrado = false;
      }
      if (temp > 26 && hum < 70) activarBomba(3);
    }
  }

  if (servoActivo && now - servoInicio >= SERVO_MS) {
    servo1.write(0);
    servoActivo = false;
  }

  digitalWrite(SS_2_PIN, HIGH);
  if (rfid1.PICC_IsNewCardPresent() && rfid1.PICC_ReadCardSerial()) {
    String uid = uidToString(rfid1);
    if (uid != lastUID1 || now - lastTime1 > DEBOUNCE_MS) {
      lastUID1 = uid; lastTime1 = now;
      mqtt.publish(TOPIC_RFID1, uid.c_str());
      mqtt.publish(TOPIC_ACCESO1, "detectado");
      abrirServo();
    }
    rfid1.PICC_HaltA(); rfid1.PCD_StopCrypto1();
  }

  digitalWrite(SS_1_PIN, HIGH);
  if (rfid2.PICC_IsNewCardPresent() && rfid2.PICC_ReadCardSerial()) {
    String uid = uidToString(rfid2);
    if (uid != lastUID2 || now - lastTime2 > DEBOUNCE_MS) {
      lastUID2 = uid; lastTime2 = now;
      mqtt.publish(TOPIC_RFID2, uid.c_str());
      mqtt.publish(TOPIC_ACCESO2, "detectado");
      abrirServo();
    }
    rfid2.PICC_HaltA(); rfid2.PCD_StopCrypto1();
  }

  int pir = digitalRead(PIR1_PIN);
  if (pir == HIGH && pirAnterior == LOW) mqtt.publish(TOPIC_MOVIMIENTO1, "1");
  pirAnterior = pir;

  if (motoresActivos) {
    if (!motoresEncendidos && now - ultimoEncendido >= INTERVALO_MOTOR) {
      digitalWrite(L293D_IN1, HIGH);
      digitalWrite(L293D_IN2, LOW);
      analogWrite(L293D_ENA, VELOCIDAD);
      motoresEncendidos = true;
      ultimoEncendido   = now;
    }
    if (motoresEncendidos && now - ultimoEncendido >= DURACION_MOTOR) {
      digitalWrite(L293D_IN1, LOW);
      digitalWrite(L293D_IN2, LOW);
      analogWrite(L293D_ENA, 0);
      motoresEncendidos = false;
      ultimoEncendido   = now;
    }
  }

  delay(50);
}