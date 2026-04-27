#include <WiFi.h>
#include <PubSubClient.h>
#include <Crypto.h>
#include <AES.h>
#include <CBC.h>
#include <SHA256.h>
#include <DHT.h>

// Wifi-Connectivity
const char* ssid        = "YOUR_WIFI_SSID";
const char* password    = "YOUR_WIFI_PASSKEY";
const char* mqtt_server = "RASPBERRY_PI_IP";

const char* key_char = "1234567890123456"; 
const char* iv_char  = "abcdefghijklmnop"; 

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
uint32_t     message_nonce = 0;


#define DHTPIN  6
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define LDR_PIN 2   // ADC1_CH2
#define IR_PIN  3
#define PIR_PIN 4

// ─── ADC AVERAGING ────────────────────────────────────────────────────────
int smoothAnalogRead(int pin, int samples = 16) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin); // ~20µs per conversion, no blocking delay needed
  }
  return (int)(sum / samples);
}

// ─── DHT CACHE ────────────────────────────────────────────────────────────
float cachedTemp = 0.0;
float cachedHum  = 0.0;
unsigned long lastDHTRead = 0;
const unsigned long DHT_READ_INTERVAL = 2500;

unsigned long lastTelemetryTime = 0;

// Crypto Engine

static const char B64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(byte* data, int length) {
  String out = "";
  for (int i = 0; i < length; i += 3) {
    int b = (data[i] & 0xFC) >> 2;
    out += B64_ALPHABET[b];
    b = (data[i] & 0x03) << 4;
    if (i + 1 < length) {
      b |= (data[i + 1] & 0xF0) >> 4;
      out += B64_ALPHABET[b];
      b = (data[i + 1] & 0x0F) << 2;
      if (i + 2 < length) {
        b |= (data[i + 2] & 0xC0) >> 6;
        out += B64_ALPHABET[b];
        b = data[i + 2] & 0x3F;
        out += B64_ALPHABET[b];
      } else { out += B64_ALPHABET[b]; out += '='; }
    } else { out += B64_ALPHABET[b]; out += "=="; }
  }
  return out;
}

void sendSecureMessage(const String& topic, const String& message) {
  message_nonce++;
  
  int msgLen = message.length();
  int padLen = 16 - (msgLen % 16);
  int paddedLen = msgLen + padLen;
  byte paddedMsg[paddedLen];
  memcpy(paddedMsg, message.c_str(), msgLen);
  for (int i = msgLen; i < paddedLen; i++) paddedMsg[i] = (byte)padLen;

  byte encryptedBytes[paddedLen];
  CBC<AES128> cbc;
  cbc.clear();
  cbc.setKey((const uint8_t*)key_char, 16);
  cbc.setIV((const uint8_t*)iv_char, 16);
  cbc.encrypt(encryptedBytes, paddedMsg, paddedLen);

  String encB64 = base64Encode(encryptedBytes, paddedLen);
  String dataToSign = String(message_nonce) + ":" + encB64;

  byte hmacKey[64];
  memset(hmacKey, 0, 64);
  memcpy(hmacKey, key_char, 16);
  byte ipad[64], opad[64];
  for (int i = 0; i < 64; i++) { ipad[i] = hmacKey[i] ^ 0x36; opad[i] = hmacKey[i] ^ 0x5C; }

  SHA256 sha256;
  sha256.reset();
  sha256.update(ipad, 64);
  sha256.update(dataToSign.c_str(), dataToSign.length());
  byte innerHash[32];
  sha256.finalize(innerHash, 32);

  sha256.reset();
  sha256.update(opad, 64);
  sha256.update(innerHash, 32);
  byte finalHash[32];
  sha256.finalize(finalHash, 32);

  String signature = "";
  for (int i = 0; i < 32; i++) {
    if (finalHash[i] < 0x10) signature += '0';
    signature += String(finalHash[i], HEX);
  }
  
  client.publish(topic.c_str(), (dataToSign + ":" + signature).c_str());
  Serial.println("[CRYPT] Payload sent.");
}

// Setup

void setup() {
  Serial.begin(115200);
  delay(4000);
  
  Serial.println("\n=========================================");
  Serial.println("🚀 ESP32-C3: SUPER NODE BOOTING...");
  Serial.println("=========================================");
  Serial.println("[PIN MAP] DHT11→GPIO6 | LDR→GPIO2 | IR→GPIO3 | PIR→GPIO4");

  pinMode(IR_PIN,  INPUT);
  pinMode(PIR_PIN, INPUT);
  analogReadResolution(12);

  dht.begin();
  // Warm-up: throwaway reads so first cached read in loop() is valid
  delay(1500);
  dht.readTemperature();
  dht.readHumidity();
  delay(500);
  Serial.println("[INIT] Sensors Ready.");

  Serial.print("[WIFI] Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(1000); Serial.print(".");
    if (++attempts > 15) { delay(3000); ESP.restart(); }
  }
  Serial.println("\n[WIFI] ✅ IP: " + WiFi.localIP().toString());
  client.setServer(mqtt_server, 1883);
}

// Main Loop
void loop() {
  if (!client.connected()) {
    Serial.println("[MQTT] Reconnecting...");
    if (client.connect("ESP32-C3-ZoneNode")) {
      Serial.println("[MQTT] ✅ Connected!");
    } else {
      Serial.println("[MQTT] ❌ State: " + String(client.state()));
      delay(2000);
      return;
    }
  }
  client.loop();

  // ── DHT on its own 2.5s timer ───────────────────────────────────────────
  if (millis() - lastDHTRead >= DHT_READ_INTERVAL) {
    lastDHTRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && t > -10 && t < 80)   cachedTemp = t;
    else Serial.println("   -> [DHT] Bad temp, keeping: " + String(cachedTemp));
    if (!isnan(h) && h >= 0  && h <= 100) cachedHum  = h;
    else Serial.println("   -> [DHT] Bad hum,  keeping: " + String(cachedHum));
  }

  // ── Telemetry every 3s ──────────────────────────────────────────────────
  if (millis() - lastTelemetryTime > 3000) {
    lastTelemetryTime = millis();

    int ir_val  = digitalRead(IR_PIN);
    int pir_val = digitalRead(PIR_PIN);
    int ldr_val = smoothAnalogRead(LDR_PIN, 16);
    int t = (int)cachedTemp;
    int h = (int)cachedHum;

    Serial.println("\n[SENSORS] T:" + String(t) + "C H:" + String(h) +
                   "% L:" + String(ldr_val) +
                   " IR:" + String(ir_val) + " PIR:" + String(pir_val));

    String payload = "ENV:T:" + String(t) + ":H:" + String(h) +
                     ":L:" + String(ldr_val) +
                     ":IR:" + String(ir_val) +
                     ":PIR:" + String(pir_val);

    sendSecureMessage("city/zone1/status", payload);
  }
}
