#include <WiFiS3.h>
#include <PubSubClient.h>
#include <Crypto.h>
#include <AES.h>
#include <CBC.h>
#include <SHA256.h>
#include <TM1637Display.h>

// Wifi-Connectivity & Crypto Creds

const char* ssid        = "Moksh";
const char* password    = "manas1122";
const char* mqtt_server = "192.168.1.109";

const char* key_char = "1234567890123456";
const char* iv_char  = "abcdefghijklmnop";

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
uint32_t     message_nonce = 0;

// Pin Connections

#define RED_PIN    2
#define YELLOW_PIN 3
#define GREEN_PIN  4

#define SOUND_PIN  A1    
const int SOUND_THRESHOLD = 500;

#define IR1_PIN    6
#define IR2_PIN    9

#define CLK_PIN    7
#define DIO_PIN    8
TM1637Display display(CLK_PIN, DIO_PIN);

#define LDR_PIN    A0

// Traffic Led [RED=60s, YELLOW=10s, GREEN=60S]

#define RED_DURATION       60000UL
#define YELLOW_DURATION    10000UL   
#define GREEN_DURATION     60000UL
#define EMERGENCY_DURATION 10000UL

// Initial Stage (Starts with Green)

enum TrafficState { GREEN_LIGHT, YELLOW_LIGHT, RED_LIGHT, EMERGENCY };
TrafficState currentState = GREEN_LIGHT;

unsigned long stateStartTime    = 0;
unsigned long stateDuration     = GREEN_DURATION;
int           lastDisplayedSecond = -1;

// Timing Variables

unsigned long lastTelemetryTime  = 0;
unsigned long lastSoundCheckTime = 0;

// Sound Sensor Filters

bool          inEmergencyCooldown    = false;
unsigned long emergencyCooldownStart = 0;


int soundTriggerCount = 0;
const int SOUND_CONFIRM_THRESHOLD    = 1;    // 4 × 50ms = 200ms sustained
const unsigned long EMERGENCY_COOLDOWN_MS = 15000UL;

// Base64 Encoder

static const char B64_ALPHABET[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
      } else {
        out += B64_ALPHABET[b];
        out += '=';
      }
    } else {
      out += B64_ALPHABET[b];
      out += "==";
    }
  }
  return out;
}

// MQTT Using AES-128 + HMAC-SHA256

void sendSecureMessage(const String& topic, const String& message) {
  message_nonce++;

  // PKCS#7 padding
  int msgLen    = message.length();
  int padLen    = 16 - (msgLen % 16);
  int paddedLen = msgLen + padLen;

  byte paddedMsg[paddedLen];
  memcpy(paddedMsg, message.c_str(), msgLen);
  for (int i = msgLen; i < paddedLen; i++) paddedMsg[i] = (byte)padLen;

  // AES-128-CBC encrypt
  byte encryptedBytes[paddedLen];
  CBC<AES128> cbc;
  cbc.clear();
  cbc.setKey((const uint8_t*)key_char, 16);
  cbc.setIV((const uint8_t*)iv_char, 16);
  cbc.encrypt(encryptedBytes, paddedMsg, paddedLen);

  String encB64     = base64Encode(encryptedBytes, paddedLen);
  String dataToSign = String(message_nonce) + ":" + encB64;

  // HMAC-SHA256
  byte hmacKey[64];
  memset(hmacKey, 0, 64);
  memcpy(hmacKey, key_char, 16);

  byte ipad[64], opad[64];
  for (int i = 0; i < 64; i++) {
    ipad[i] = hmacKey[i] ^ 0x36;
    opad[i] = hmacKey[i] ^ 0x5C;
  }

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
  Serial.println("🔐 Sent → " + topic + " | " + message);
}

// MQTT Reconnectivity [If Failed | Interrupted]

void reconnectMQTT() {
  if (!client.connected()) {
    Serial.print("🔌 Connecting to MQTT...");
    if (client.connect("UnoTrafficNode-Crypto")) {
      Serial.println(" ✅ Connected!");
    } else {
      Serial.print(" ❌ Failed (rc=");
      Serial.print(client.state());
      Serial.println(")");
    }
  }
}


// Traffic Led State Changer

void changeState(TrafficState newState, unsigned long duration) {
  currentState        = newState;
  stateDuration       = duration;
  stateStartTime      = millis();
  lastDisplayedSecond = -1;   // force immediate display refresh

  digitalWrite(RED_PIN,    LOW);
  digitalWrite(YELLOW_PIN, LOW);
  digitalWrite(GREEN_PIN,  LOW);

  switch (newState) {
    case RED_LIGHT:
      digitalWrite(RED_PIN, HIGH);
      Serial.println("🚦 STATE → 🔴 RED    (60 s)");
      break;

    case YELLOW_LIGHT:
      digitalWrite(YELLOW_PIN, HIGH);
      Serial.println("🚦 STATE → 🟡 YELLOW (10 s)");
      break;

    case GREEN_LIGHT:
      digitalWrite(GREEN_PIN, HIGH);
      Serial.println("🚦 STATE → 🟢 GREEN  (60 s)");
      break;

    case EMERGENCY:
      digitalWrite(GREEN_PIN, HIGH);
      Serial.println("🚑 STATE → EMERGENCY (10 s)");
      break;
  }
}

// Setup

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n🚦 BOOTING TRAFFIC NODE v2.1...");

  pinMode(RED_PIN,    OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  pinMode(GREEN_PIN,  OUTPUT);

  pinMode(SOUND_PIN, INPUT_PULLUP);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);

  display.setBrightness(7);
  display.showNumberDecEx(8888, 0b01000000, true);
  delay(1000);

  Serial.print("📶 Connecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Wi-Fi Connected! IP: " + WiFi.localIP().toString());

  client.setServer(mqtt_server, 1883);

  // Start on GREEN
  changeState(GREEN_LIGHT, GREEN_DURATION);
}

// Main Loop

void loop() {

  // --- MQTT keep-alive ---
  reconnectMQTT();
  client.loop();

  // ----------------------------------------------------------------
  // A. READ SENSORS ONCE — reused in all sections below
  // ----------------------------------------------------------------
  int lightLevel = analogRead(LDR_PIN);
  int ir1        = digitalRead(IR1_PIN);
  int ir2        = digitalRead(IR2_PIN);

  // ----------------------------------------------------------------
  // B. ADAPTIVE DISPLAY BRIGHTNESS — runs every loop
  // ----------------------------------------------------------------
  display.setBrightness(lightLevel < 300 ? 1 : 7);

  // ----------------------------------------------------------------
  // C. TELEMETRY — every 10 seconds
  // ----------------------------------------------------------------
  if (millis() - lastTelemetryTime >= 10000) {
    lastTelemetryTime = millis();
    String envData = "ENV:L:" + String(lightLevel)
                   + ":IR1:"  + String(ir1)
                   + ":IR2:"  + String(ir2);
    Serial.println("📡 Telemetry: " + envData);
    sendSecureMessage("city/traffic/status", envData);
  }

  // ----------------------------------------------------------------
  // D. EMERGENCY COOLDOWN EXPIRY
  // ----------------------------------------------------------------
  if (inEmergencyCooldown &&
      millis() - emergencyCooldownStart >= EMERGENCY_COOLDOWN_MS) {
    inEmergencyCooldown = false;
    Serial.println("✅ Cooldown expired — siren detection re-armed.");
  }

// ----------------------------------------------------------------
  // E. ANALOG SOUND SENSOR — Reads raw volume (0 to 1023)
  // ----------------------------------------------------------------
  if (millis() - lastSoundCheckTime >= 50) {
    lastSoundCheckTime = millis();

    int currentVolume = analogRead(SOUND_PIN);
    
    // Un-comment the line below if you need to see the raw numbers to calibrate it!
     Serial.println("Volume: " + String(currentVolume)); 

    if (currentVolume > SOUND_THRESHOLD) {
      soundTriggerCount++;          // Volume spiked above threshold
    } else {
      soundTriggerCount = 0;        // Volume is normal
    }
  }

  if (soundTriggerCount >= SOUND_CONFIRM_THRESHOLD &&
      currentState != EMERGENCY &&
      !inEmergencyCooldown) {

    soundTriggerCount = 0;
    Serial.println("🚑 CONFIRMED SIREN — TRIGGERING EMERGENCY!");
    sendSecureMessage("city/traffic/status", "ALERT:EMERGENCY_SIREN");
    changeState(EMERGENCY, EMERGENCY_DURATION);
  }


  // ----------------------------------------------------------------
  // F. COUNTDOWN DISPLAY — updates only when second changes
  // ----------------------------------------------------------------
  unsigned long elapsed        = millis() - stateStartTime;
  int           remainingSec   = (int)((stateDuration - elapsed) / 1000);
  if (remainingSec < 0) remainingSec = 0;

  if (remainingSec != lastDisplayedSecond) {
    lastDisplayedSecond = remainingSec;
    display.showNumberDecEx(remainingSec, 0b01000000, true);
  }

  // ----------------------------------------------------------------
  // G. STATE TRANSITIONS — fires once elapsed >= duration
  //    Cycle: GREEN(60s) → YELLOW(10s) → RED(60s) → GREEN(60s) ...
  // ----------------------------------------------------------------
  if (elapsed >= stateDuration) {
    switch (currentState) {

      case GREEN_LIGHT:
        changeState(YELLOW_LIGHT, YELLOW_DURATION);   // 10 s
        break;

      case YELLOW_LIGHT:
        changeState(RED_LIGHT, RED_DURATION);         // 60 s
        break;

      case RED_LIGHT:
        changeState(GREEN_LIGHT, GREEN_DURATION);     // 60 s
        break;

      case EMERGENCY:
        Serial.println("🚦 Emergency over — cooldown started, returning to RED.");
        sendSecureMessage("city/traffic/status", "TRAFFIC:EMERGENCY_CLEARED");
        inEmergencyCooldown    = true;
        emergencyCooldownStart = millis();
        changeState(RED_LIGHT, RED_DURATION);         
        break;
    }
  }
}
