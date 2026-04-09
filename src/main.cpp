#include <FirebaseESP32.h>
#include <WiFi.h>
#include <DHT.h>
#include <BH1750.h>
#include <Wire.h>

// --- KONFIGURASI ---
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

// --- GLOBAL VARIABLES (Shared Data) ---
float globalTemp, globalHum, globalLux;
int globalAirQ;
bool globalMotion, securityMode, exhaustOverride;
byte globalDataLampu = 0;
String globalTime = "00:00";

// Object Global
DHT dht(DHTPIN, DHTTYPE);
BH1750 lightMeter;
FirebaseData fbdt;
FirebaseAuth auth;
FirebaseConfig config;

// --- PROTOTYPE TASK ---
void TaskReadSensors(void *pvParameters);
void TaskFirebaseComm(void *pvParameters);
void TaskSecurityLogic(void *pvParameters);

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

  // Koneksi WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }

  config.host = FB_HOST;
  config.signer.tokens.legacy_token = FB_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // --- MEMBUAT TASK RTOS ---
  
  // Task 1: Baca Sensor (Core 1)
  xTaskCreatePinnedToCore(TaskReadSensors, "SensorTask", 4000, NULL, 1, NULL, 1);

  // Task 2: Komunikasi Firebase (Core 0 - Supaya tidak ganggu pembacaan sensor)
  xTaskCreatePinnedToCore(TaskFirebaseComm, "FBTask", 8000, NULL, 1, NULL, 0);

  // Task 3: Keamanan & Aktuator (Core 1 - Respon Cepat)
  xTaskCreatePinnedToCore(TaskSecurityLogic, "SecurityTask", 2000, NULL, 2, NULL, 1);
}

void loop() {
  // Loop kosong, semua dikerjakan di Task
  vTaskDelete(NULL); 
}

// --- IMPLEMENTASI TASK ---

void TaskReadSensors(void *pvParameters) {
  for (;;) {
    globalTemp = dht.readTemperature();
    globalHum = dht.readHumidity();
    globalAirQ = analogRead(MQ135_PIN);
    globalLux = lightMeter.readLightLevel();
    globalMotion = digitalRead(PIR_PIN);
    
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void TaskSecurityLogic(void *pvParameters) {
  for (;;) {
    // Kontrol Buzzer (Respon instan tanpa nunggu delay Firebase)
    if (securityMode && globalMotion) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    // Kontrol Kipas (Logika lokal)
    if (exhaustOverride || globalTemp > 32.0 || globalAirQ > 1000) {
      digitalWrite(FAN_PIN, HIGH);
    } else {
      digitalWrite(FAN_PIN, LOW);
    }

    // Update Shift Register
    updateShiftRegister(globalDataLampu);

    vTaskDelay(100 / portTICK_PERIOD_MS); // Cek setiap 0.1 detik
  }
}

void TaskFirebaseComm(void *pvParameters) {
  for (;;) {
    if (Firebase.ready()) {
      // 1. Upload Data
      FirebaseJson json;
      json.set("Temperature", globalTemp);
      json.set("AirQuality", globalAirQ);
      json.set("securityMovement", globalMotion);
      Firebase.updateNode(fbdt, "SmartApp/EnvironmentStatus", json);

      // 2. Download Kontrol Lampu
      if (Firebase.getJSON(fbdt, "SmartApp/Lamp")) {
        byte tempLampu = 0;
        FirebaseJson &jsonLampu = fbdt.jsonObject();
        FirebaseJsonData jsonData;
        
        if (jsonLampu.get(jsonData, "Outdoor") && jsonData.boolValue) tempLampu |= (1 << 0);
        if (jsonLampu.get(jsonData, "Bedroom") && jsonData.boolValue) tempLampu |= (1 << 1);
        // ... dst untuk ruangan lain
        
        globalDataLampu = tempLampu;
      }

      // 3. Download Security Mode & Exhaust Status
      if (Firebase.getBool(fbdt, "SmartApp/SecurityMode")) securityMode = fbdt.boolData();
      if (Firebase.getBool(fbdt, "SmartApp/Exhaust")) exhaustOverride = fbdt.boolData();
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS); // Sinkronisasi setiap 3 detik
  }
}

void updateShiftRegister(byte data) {
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, MSBFIRST, data);
  digitalWrite(latchPin, HIGH);
}