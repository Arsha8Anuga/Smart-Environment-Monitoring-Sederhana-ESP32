#include <FirebaseESP32.h>
#include <WiFi.h>
#include <DHT.h>
#include <BH1750.h>
#include <Wire.h>

#define WIFI_SSID ""
#define WIFI_PASS ""
#define FB_HOST ""
#define FB_AUTH ""

#define DHTPIN 4
#define DHTTYPE DHT22
#define PIR_PIN 13
#define MQ135_PIN 34
#define BUZZER_PIN 12
#define FAN_PIN 14

const int latchPin = 5;
const int clockPin = 18;
const int dataPin = 19;

float tempThreshold = 32.0;
int airQualityThreshold = 1000;

DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
FirebaseData fbdt;
FirebaseAuth auth;
FirebaseConfig config;

String pathStatus = "SmartApp/EnvironmentStatus/";
String pathLamp = "SmartApp/Lamp/";

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);

  dht.begin();
  Wire.begin();
  lightMeter.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nConnected!");

  config.host = FB_HOST;
  config.signer.tokens.legacy_token = FB_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  // --- BACA SENSOR ---
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int airQ = analogRead(MQ135_PIN);
  float lux = lightMeter.readLightLevel();
  bool motionDetected = digitalRead(PIR_PIN);

  if (isnan(t) || isnan(h)) {
    Serial.println("Gagal baca sensor DHT!");
    return;
  }

  // --- 1. UPDATE DATA SENSOR (UPLOAD) ---
  FirebaseJson json;
  json.set("Temperature", t);
  json.set("Humidity", h);
  json.set("AirQuality", airQ);
  json.set("securityMovement", motionDetected);

  if (Firebase.updateNode(fbdt, "SmartApp/EnvironmentStatus", json)) {
      Serial.println("Data sensor berhasil diupdate!");
  } else {
      Serial.print("Gagal update data: ");
      Serial.println(fbdt.errorReason());
  }

  // --- 2. KONTROL EXHAUST ---
  if (Firebase.getBool(fbdt, "SmartApp/Exhaust")) {
    bool isExhaustOn = fbdt.boolData();
    if (isExhaustOn || t > tempThreshold || airQ > airQualityThreshold) {
      digitalWrite(FAN_PIN, HIGH);
    } else {
      digitalWrite(FAN_PIN, LOW);
    }
  }

  // --- 3. KONTROL LAMPU (DOWNLOAD & SHIFT REGISTER) ---
  byte dataLampu = 0; // Inisialisasi awal (mati semua)
  
  if (Firebase.getJSON(fbdt, "SmartApp/Lamp")) {
    FirebaseJson &jsonLampu = fbdt.jsonObject();
    FirebaseJsonData jsonData;

    // Ambil status (HAPUS KATA 'byte' DI SINI)
    jsonLampu.get(jsonData, "Outdoor");
    if (jsonData.boolValue) dataLampu |= (1 << 0);

    jsonLampu.get(jsonData, "Bedroom");
    if (jsonData.boolValue) dataLampu |= (1 << 1);

    jsonLampu.get(jsonData, "Guestroom");
    if (jsonData.boolValue) dataLampu |= (1 << 2);

    jsonLampu.get(jsonData, "Bathroom");
    if (jsonData.boolValue) dataLampu |= (1 << 3);

    jsonLampu.get(jsonData, "Livingroom");
    if (jsonData.boolValue) dataLampu |= (1 << 4);

    // --- LOGIKA WAKTU OTOMATIS ---
    if (Firebase.getString(fbdt, "SmartApp/EspTime")) {
        String timeStr = fbdt.stringData();
        int jam = timeStr.substring(0, 2).toInt();
        if (jam >= 18 || jam < 5) {
            dataLampu |= (1 << 0); // Paksa Lampu Outdoor nyala malam hari
        }
    }

    // Kirim ke IC Shift Register
    updateShiftRegister(dataLampu);
  }

  // --- 4. MODE KEAMANAN ---
  if (Firebase.getBool(fbdt, "SmartApp/SecurityMode")) {
    if (fbdt.boolData() && motionDetected) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  delay(2000); 
}

void updateShiftRegister(byte data) {
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, data);
  digitalWrite(latchPin, HIGH);
}