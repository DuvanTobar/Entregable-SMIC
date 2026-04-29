#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>

const char* ssid        = "A36 de Duvan";
const char* password    = "SarisHermosa";
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;

#define TOPIC_TEMP         "invernadero/temperatura"
#define TOPIC_HUM          "esp32/hum"
#define TOPIC_BOMBA        "esp32/bomb"
#define TOPIC_TECHO        "invernadero/techo/estado"
#define TOPIC_RFID1        "esp32/rfid"
#define TOPIC_RFID2        "esp32/rfid2"
#define TOPIC_ACCESO1      "invernadero/acceso/puerta1"
#define TOPIC_ACCESO2      "invernadero/acceso/puerta2"
#define TOPIC_CMD_BOMBA    "invernadero/cmd/bomba"

#define DHTPIN   25
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

#define PIN_RELE 23

#define SPI_SCK   12
#define SPI_MISO  13
#define SPI_MOSI  11
#define SS_PIN1   10
#define RST_PIN1  9
#define SS_PIN2   14 
#define RST_PIN2  21 

MFRC522 mfrc522_1(SS_PIN1, RST_PIN1);
MFRC522 mfrc522_2(SS_PIN2, RST_PIN2);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long ultimaLectura = 0;
const long intervaloSensor = 3000;

bool techoCerrado = false;

void activarBomba(int segundos) {
  Serial.println("Regando plantas...");
  client.publish(TOPIC_BOMBA, "encendida");

  digitalWrite(PIN_RELE, LOW);
  delay(segundos * 1000);

  digitalWrite(PIN_RELE, HIGH);
  client.publish(TOPIC_BOMBA, "apagada");
}

void apagarBomba() {
  digitalWrite(PIN_RELE, HIGH);
  client.publish(TOPIC_BOMBA, "apagada");
}

String getUID(MFRC522 &lector) {
  String uid = "";
  for (byte i = 0; i < lector.uid.size; i++) {
    if (lector.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(lector.uid.uidByte[i], HEX);
    if (i < lector.uid.size - 1) uid += ":";
  }
  uid.toUpperCase();
  return uid;
}

void detectarTarjeta(MFRC522 &lector, int num, String uid) {
  const char* topicRFID   = (num == 1) ? TOPIC_RFID1   : TOPIC_RFID2;
  const char* topicAcceso = (num == 1) ? TOPIC_ACCESO1 : TOPIC_ACCESO2;

  Serial.print("[Lector "); Serial.print(num);
  Serial.print("] UID: "); Serial.println(uid);

  client.publish(topicRFID, uid.c_str());
  client.publish(topicAcceso, "detectado");

  delay(1500);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String mensaje = "";
  for (unsigned int i = 0; i < length; i++) {
    mensaje += (char)payload[i];
  }

  mensaje.trim();

  if (String(topic) == TOPIC_CMD_BOMBA) {
    if (mensaje == "ON") activarBomba(3);
    if (mensaje == "OFF") apagarBomba();
  }
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32-Invernadero")) {
      client.subscribe(TOPIC_CMD_BOMBA);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELE, OUTPUT);
  digitalWrite(PIN_RELE, HIGH);

  dht.begin();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  mfrc522_1.PCD_Init();
  mfrc522_2.PCD_Init();

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long ahora = millis();

  if (ahora - ultimaLectura >= intervaloSensor) {
    ultimaLectura = ahora;

    float hum  = dht.readHumidity();
    float temp = dht.readTemperature();

    if (!isnan(hum) && !isnan(temp)) {

      char bufTemp[8], bufHum[8];
      dtostrf(temp, 4, 1, bufTemp);
      dtostrf(hum, 4, 1, bufHum);

      client.publish(TOPIC_TEMP, bufTemp);
      client.publish(TOPIC_HUM, bufHum);

      Serial.print("Temp: "); Serial.print(temp);
      Serial.print(" Hum: "); Serial.println(hum);

      if (temp > 24 && !techoCerrado) {
        client.publish(TOPIC_TECHO, "cerrado");
        activarBomba(3);
        techoCerrado = true;

      } else if (temp <= 24 && techoCerrado) {
        client.publish(TOPIC_TECHO, "abierto");
        apagarBomba();
        techoCerrado = false;
      }
    }
  }

  if (mfrc522_1.PICC_IsNewCardPresent() && mfrc522_1.PICC_ReadCardSerial()) {
    detectarTarjeta(mfrc522_1, 1, getUID(mfrc522_1));
    mfrc522_1.PICC_HaltA();
    mfrc522_1.PCD_StopCrypto1();
  }

  if (mfrc522_2.PICC_IsNewCardPresent() && mfrc522_2.PICC_ReadCardSerial()) {
    detectarTarjeta(mfrc522_2, 2, getUID(mfrc522_2));
    mfrc522_2.PICC_HaltA();
    mfrc522_2.PCD_StopCrypto1();
  }

  delay(200);
}
#include <DHT.h>
#include <ESP32Servo.h>
#define DHTPIN 25
#define DHTTYPE DHT11
int pin1 = 18;
int pin2 = 19;
int pin3 = 21;
int pin4 = 22;
int pin5 = 23;
int rele = 17;
Servo s1, s2, s3, s4, s5;
DHT dht(DHTPIN, DHTTYPE);
bool techoCerrado = false;
int posicionActual = 0;
void setup() {
  Serial.begin(115200);
  dht.begin();
  s1.attach(pin1);
  s2.attach(pin2);
  s3.attach(pin3);
  s4.attach(pin4);
  s5.attach(pin5);
  pinMode(rele, OUTPUT);
  digitalWrite(rele, LOW);
}
void moverServos(int destino) {
  if (posicionActual < destino) {
    for (int pos = posicionActual; pos <= destino; pos++) {
      s1.write(pos);
      s2.write(pos);
      s3.write(pos);
      s4.write(pos);
      s5.write(pos);
      delay(20);
    }
  } else {
    for (int pos = posicionActual; pos >= destino; pos--) {
      s1.write(pos);
      s2.write(pos);
      s3.write(pos);
      s4.write(pos);
      s5.write(pos);
      delay(20);
    }
  }
  posicionActual = destino;
}
void abrir_techo() {
  moverServos(90);  
}
void cerrar_techo() {
  moverServos(0);
}
void loop() {
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();
  if (isnan(hum) || isnan(temp)) {
    Serial.println("Error leyendo el sensor");
    delay(2000);
    return;
  }
  Serial.print("Temperatura: ");
  Serial.print(temp);
  Serial.print(" °C  Humedad: ");
  Serial.print(hum);
  Serial.println(" %");
  if (temp > 26 && !techoCerrado || hum>80) {
    Serial.println("Cerrando techo");
    cerrar_techo();
    techoCerrado = true;
    Serial.println("Activando riego");
    digitalWrite(rele, HIGH);
    delay(10000);
    digitalWrite(rele, LOW);
  } 
  else if (temp <= 24 && hum<70 && techoCerrado) {
    Serial.println("Abriendo techo");
    abrir_techo();
    techoCerrado = false;
    Serial.println("Bomba apagada");
    digitalWrite(rele, LOW);
  }
  if(temp>26 && hum<70){
    Serial.println("Regando plantas");
    digitalWrite(rele, HIGH);
    delay(10000);
    digitalWrite(rele,LOW);
  }
  delay(2000);
}