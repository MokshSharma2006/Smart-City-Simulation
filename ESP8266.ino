#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Crypto.h>
#include <AES.h>
#include <CBC.h>
#include <SHA256.h>

// Wifi-Connectivity & Crypto Creds

const char* ssid        = "Moksh";
const char* password    = "manas1122";
const char* mqtt_server = "192.168.1.109"; 

const char* key_char = "1234567890123456"; 
const char* iv_char  = "abcdefghijklmnop"; 

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
uint32_t     message_nonce = 0;

// Pin Connection

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define IR1_PIN D5
#define IR2_PIN D6

#define RED1_PIN   D7
#define GREEN1_PIN D8
#define RED2_PIN   D3
#define GREEN2_PIN D4

#define LDR_PIN D0

unsigned long lastTelemetryTime = 0;

// Crypto 

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
}

// Setup

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(IR1_PIN, INPUT);
  pinMode(IR2_PIN, INPUT);
  pinMode(RED1_PIN, OUTPUT);
  pinMode(GREEN1_PIN, OUTPUT);
  pinMode(RED2_PIN, OUTPUT);
  pinMode(GREEN2_PIN, OUTPUT);

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("BOOTING...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  client.setServer(mqtt_server, 1883);
}

// Main Loop

void loop() {
  if (!client.connected()) {
    if (client.connect("ESP8266-ParkingNode")) {
      Serial.println("✅ Connected to MQTT!");
    }
  }
  client.loop();

  // READ SENSORS (Assuming LOW = Car Present)
  int ir1 = digitalRead(IR1_PIN);
  int ir2 = digitalRead(IR2_PIN);
  int ldr = analogRead(LDR_PIN);

  int availableSpots = 0;
  String spot1State = "OCCUPIED";
  String spot2State = "OCCUPIED";

  // --- SPOT 1 LOGIC ---
  if (ir1 == HIGH) { // Empty
    digitalWrite(GREEN1_PIN, HIGH);
    digitalWrite(RED1_PIN, LOW);
    availableSpots++;
    spot1State = "FREE";
  } else { // Occupied
    digitalWrite(GREEN1_PIN, LOW);
    digitalWrite(RED1_PIN, HIGH);
  }

  // --- SPOT 2 LOGIC ---
  if (ir2 == HIGH) { // Empty
    digitalWrite(GREEN2_PIN, HIGH);
    digitalWrite(RED2_PIN, LOW);
    availableSpots++;
    spot2State = "FREE";
  } else { // Occupied
    digitalWrite(GREEN2_PIN, LOW);
    digitalWrite(RED2_PIN, HIGH);
  }

  // --- UPDATE OLED DISPLAY ---
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(10, 0);
  display.println("SMART PARKING DECK");
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  // Large Available Count
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("FREE: ");
  display.print(availableSpots);
  display.print("/2");

  // Detailed Status
  display.setTextSize(1);
  display.setCursor(0, 45);
  display.print("P1: "); display.print(spot1State);
  display.setCursor(0, 55);
  display.print("P2: "); display.print(spot2State);
  
  display.display();

  // --- SEND SECURE TELEMETRY (Every 5 seconds) ---
  if (millis() - lastTelemetryTime > 5000) {
    lastTelemetryTime = millis();
    String payload = "PARKING:L:" + String(ldr) + ":P1:" + spot1State + ":P2:" + spot2State + ":FREE:" + String(availableSpots);
    Serial.println("📡 Beaming: " + payload);
    sendSecureMessage("city/parking/status", payload);
  }
}