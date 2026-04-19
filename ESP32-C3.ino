#include <WiFi.h>
#include <PubSubClient.h>
#include <Crypto.h>
#include <AES.h>
#include <CBC.h>
#include <SHA256.h>
#include <DHT.h>

// ==========================================
// 🌐 NETWORK & SECURITY CREDENTIALS
// ==========================================
const char* ssid        = "Moksh";       
const char* password    = "manas1122";   
const char* mqtt_server = "192.168.1.109";          

const char* key_char = "1234567890123456"; 
const char* iv_char  = "abcdefghijklmnop"; 

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
uint32_t     message_nonce = 0;

// ==========================================
// 🔌 HARDWARE PINS (ESP32-C3 SAFE PINS)
// ==========================================
#define DHTPIN  1     // DHT11 Data Pin
#define DHTTYPE DHT11 // Change to DHT22 if needed
DHT dht(DHTPIN, DHTTYPE);

#define LDR_PIN 0     // Analog pin for Light (ADC1_CH0)
#define IR_PIN  3     // Digital pin for IR Tripwire
#define PIR_PIN 4     // Digital pin for PIR Motion

unsigned long lastTelemetryTime = 0;

// ==========================================
// 🛡️ CRYPTO ENGINE
// ==========================================
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
  
  // 1. Padding
  int msgLen = message.length();
  int padLen = 16 - (msgLen % 16);
  int paddedLen = msgLen + padLen;
  byte paddedMsg[paddedLen];
  memcpy(paddedMsg, message.c_str(), msgLen);
  for (int i = msgLen; i < paddedLen; i++) paddedMsg[i] = (byte)padLen;

  // 2. AES-128-CBC Encryption
  byte encryptedBytes[paddedLen];
  CBC<AES128> cbc;
  cbc.clear();
  cbc.setKey((const uint8_t*)key_char, 16);
  cbc.setIV((const uint8_t*)iv_char, 16);
  cbc.encrypt(encryptedBytes, paddedMsg, paddedLen);

  // 3. Base64 Encode
  String encB64 = base64Encode(encryptedBytes, paddedLen);
  String dataToSign = String(message_nonce) + ":" + encB64;

  // 4. HMAC-SHA256 Signature
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
  
  // 5. Final Assembly & Publish
  String finalPayload = dataToSign + ":" + signature;
  Serial.println("[CRYPT] Payload Encrypted & Signed.");
  client.publish(topic.c_str(), finalPayload.c_str());
}

// ==========================================
// 🚀 SYSTEM SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(4000); // Give Windows/USB time to connect to the Serial Monitor
  
  Serial.println("\n\n=========================================");
  Serial.println("🚀 ESP32-C3: SUPER NODE BOOTING...");
  Serial.println("=========================================");

  Serial.println("[INIT] Configuring Sensors...");
  pinMode(IR_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  dht.begin();
  Serial.println("[INIT] Sensors Ready.");

  Serial.print("[WIFI] Connecting to SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) { 
    delay(1000); 
    Serial.print("."); 
    attempts++;
    if(attempts > 15) {
      Serial.println("\n[ERROR] Wi-Fi Timeout! Check SSID/Password. Restarting in 3s...");
      delay(3000);
      ESP.restart();
    }
  }
  
  Serial.println("\n[WIFI] ✅ Success! IP Address: ");
  Serial.println(WiFi.localIP());
  
  Serial.print("[MQTT] Configuring broker at: ");
  Serial.println(mqtt_server);
  client.setServer(mqtt_server, 1883);
}

// ==========================================
// 🔄 MAIN LOOP
// ==========================================
void loop() {
  // Reconnect MQTT if disconnected
  if (!client.connected()) {
    Serial.println("[MQTT] Connection lost. Attempting reconnect...");
    if (client.connect("ESP32-C3-ZoneNode")) {
      Serial.println("[MQTT] ✅ Connected to Control Room!");
    } else {
      Serial.print("[MQTT] ❌ Failed. State: ");
      Serial.println(client.state());
      delay(2000); 
      return; 
    }
  }
  
  client.loop(); // Keep MQTT alive

  // Transmit Telemetry every 3 seconds
  if (millis() - lastTelemetryTime > 3000) {
    lastTelemetryTime = millis();
    
    Serial.println("\n[SENSORS] Reading Telemetry...");
    
    // Read Security
    int ir_val = digitalRead(IR_PIN);
    int pir_val = digitalRead(PIR_PIN);
    
    // Read Environment
    int t = dht.readTemperature();
    int h = dht.readHumidity();
    int ldr_val = analogRead(LDR_PIN);
    
    // Fail-safe for DHT11
    if(isnan(t)) { t = 0; Serial.println("   -> [WARNING] Failed to read DHT Temp!"); }
    if(isnan(h)) { h = 0; Serial.println("   -> [WARNING] Failed to read DHT Humidity!"); }

    Serial.print("   -> Temp: "); Serial.print(t); Serial.println("C");
    Serial.print("   -> Hum:  "); Serial.print(h); Serial.println("%");
    Serial.print("   -> Light:"); Serial.println(ldr_val);
    Serial.print("   -> IR:   "); Serial.println(ir_val);
    Serial.print("   -> PIR:  "); Serial.println(pir_val);

    // Build the super string
    // Format: ENV:T:25:H:60:L:1024:IR:1:PIR:0
    String payload = "ENV:T:" + String(t) + ":H:" + String(h) + ":L:" + String(ldr_val) + ":IR:" + String(ir_val) + ":PIR:" + String(pir_val);
    
    Serial.println("[NET] Transmitting payload: " + payload);
    sendSecureMessage("city/zone1/status", payload);
    Serial.println("[NET] Transmission complete.");
  }
}