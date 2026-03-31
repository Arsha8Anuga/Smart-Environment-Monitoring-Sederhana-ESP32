#include <FirebaseESP32.h>
#include <WiFi.h>
#include <DHT.h>
#include <BH1750.h>
#include <Wire.h>

#define WIFI_SSID ""
#define WIFI_PASS ""
#define FB_HOST ""
#define FB_AUTH ""

#define PIN_DHT 4
#define PIN_PIR 13
#define PIN_BUZZER 12
#define PIN_FAN_MOSFET 14
#define PIN_LATCH 25
#define PIN_CLOCK 26
#define PIN_DATA 27

DHT dht(PIN_DHT, DHT22);
BH1750 lightMeter;
FirebaseData fbdt;
FirebaseAuth auth;
FirebaseConfig config;

float tempThreshold = 30.0;
float lightThreshold = 50.0;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_FAN_MOSFET, OUTPUT);
  pinMode(PIN_LATCH, OUTPUT);
  pinMode(PIN_CLOCK, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);

  dht.begin();
  Wire.begin();
  lightMeter.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  config.host = FB_HOST;
  config.signer.tokens.legacy_token = FB_AUTH;
  Firebase.begin (&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();

  if (isnan(hum) || isnan(temp)) return;

  if (temp > tempThreshold) {
    digitalWrite(PIN_FAN_MOSFET, HIGH);
  } else {
    digitalWrite(PIN_FAN_MOSFET, LOW);
  }
}