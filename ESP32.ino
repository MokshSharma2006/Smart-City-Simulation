#include <WiFi.h>
#include <PubSubClient.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

// Wifi-Connectivity

const char* ssid        = "YOUR_WIFI_SSID";
const char* password    = "YOUR_WIFI_PASSKEY";
const char* mqtt_server = "RASPBERRY_PI_IP";

const char* key_char = "1234567890123456"; 
const char* iv_char  = "abcdefghijklmnop"; 

WiFiClient espClient;
PubSubClient client(espClient);

uint32_t message_nonce = 0; 
uint32_t last_received_nonce = 0; 

// Pin Connection

#define SS_PIN 5
#define RST_PIN 22
MFRC522 rfid(SS_PIN, RST_PIN);

Servo gateServo;
#define SERVO_PIN 4

#define TRIG_PIN 13
#define ECHO_PIN 12
#define PIR_PIN 27

// NEW: IR Tripwire Pin
#define GATE_IR_PIN 25

// Timers & States
bool isGateOpen = false;
unsigned long gateOpenTime = 0;
const unsigned long gateDelay = 5000; 

unsigned long lastSensorCheck = 0;
bool carWasPresent = false;
bool motionWasPresent = false;

// NEW: IR Tripwire States
unsigned long lastIrTrigger = 0;
bool irWasTriggered = false;

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

// Security Function

void sendSecureMessage(String topic, String message) {
  message_nonce++;
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const unsigned char*)key_char, 128);
  unsigned char iv[16];
  memcpy(iv, iv_char, 16);
  
  int msgLen = message.length();
  int padLen = 16 - (msgLen % 16);
  int paddedLen = msgLen + padLen;
  unsigned char paddedMsg[paddedLen];
  memcpy(paddedMsg, message.c_str(), msgLen);
  for (int i = msgLen; i < paddedLen; i++) paddedMsg[i] = (unsigned char)padLen;
  
  unsigned char encryptedBytes[paddedLen];
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, paddedMsg, encryptedBytes);
  mbedtls_aes_free(&aes);
  
  unsigned char base64Buffer[128];
  size_t base64Len;
  mbedtls_base64_encode(base64Buffer, sizeof(base64Buffer), &base64Len, encryptedBytes, paddedLen);
  String encryptedData = String((char*)base64Buffer);

  String dataToSign = String(message_nonce) + ":" + encryptedData;
  byte hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key_char, 16);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)dataToSign.c_str(), dataToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  
  String signature = "";
  for(int i=0; i<32; i++){
    if(hmacResult[i] < 16) signature += "0";
    signature += String(hmacResult[i], HEX);
  }

  String finalPayload = dataToSign + ":" + signature;
  client.publish(topic.c_str(), finalPayload.c_str());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String rawMsg = "";
  for (int i = 0; i < length; i++) rawMsg += (char)payload[i];
  
  int firstColon = rawMsg.indexOf(':');
  int secondColon = rawMsg.lastIndexOf(':');
  if (firstColon == -1 || secondColon == -1) return;
  
  uint32_t incomingNonce = rawMsg.substring(0, firstColon).toInt();
  String encData = rawMsg.substring(firstColon + 1, secondColon);

  if (incomingNonce <= last_received_nonce) return; 
  last_received_nonce = incomingNonce;

  unsigned char decodedBytes[128];
  size_t decodedLen;
  mbedtls_base64_decode(decodedBytes, sizeof(decodedBytes), &decodedLen, (const unsigned char*)encData.c_str(), encData.length());

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, (const unsigned char*)key_char, 128);
  unsigned char iv[16];
  memcpy(iv, iv_char, 16);

  unsigned char decryptedBytes[128];
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, decodedLen, iv, decodedBytes, decryptedBytes);
  mbedtls_aes_free(&aes);

  int padLen = decryptedBytes[decodedLen - 1];
  decryptedBytes[decodedLen - padLen] = '\0'; 
  String command = String((char*)decryptedBytes);

  if (command == "CMD:OPEN_GATE") {
    Serial.println("🟢 Access Granted! Opening Gate.");
    gateServo.write(90); 
    isGateOpen = true;
    gateOpenTime = millis(); 
  }
}

// Setup

void setup() {
  Serial.begin(115200);
  
  ESP32PWM::allocateTimer(0);
  gateServo.setPeriodHertz(50);
  gateServo.attach(SERVO_PIN, 500, 2400); 
  gateServo.write(0); 

  // Initialize Sensor Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  
  // NEW: Init IR Tripwire Pin
  pinMode(GATE_IR_PIN, INPUT);

  SPI.begin();       
  rfid.PCD_Init();
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  
  Serial.println("🚗 Gate Node Online (Radar + PIR + IR Active)");
}

// Main Loop

void loop() {
  if (!client.connected()) {
    while (!client.connected()) {
      if (client.connect("ESP32GateNode")) client.subscribe("city/gate/command");
      else delay(5000);
    }
  }
  client.loop();

  // 1. Gate Auto-Close Timer
  if (isGateOpen && (millis() - gateOpenTime >= gateDelay)) {
    gateServo.write(0); 
    isGateOpen = false;
    sendSecureMessage("city/gate/status", "STATUS:GATE_CLOSED");
  }

  if (millis() - lastSensorCheck > 1500) {
    lastSensorCheck = millis();

    // Check PIR (Human Motion)
    bool isMotion = digitalRead(PIR_PIN);
    if (isMotion && !motionWasPresent) {
      Serial.println("🚶 WARNING: Human Motion Detected near Gate!");
      sendSecureMessage("city/gate/status", "ALERT:MOTION_DETECTED");
      motionWasPresent = true;
    } else if (!isMotion) {
      motionWasPresent = false;
    }

    // Check Ultrasonic (Vehicle Presence)
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
    float distance = duration * 0.034 / 2;
    
    // If distance is less than 15cm, a car is at the gate
    bool isCarPresent = (distance > 0 && distance < 15.0); 
    
    if (isCarPresent && !carWasPresent) {
      Serial.println("🚙 Vehicle Arrived at Gate.");
      sendSecureMessage("city/gate/status", "SENSOR:VEHICLE_WAITING");
      carWasPresent = true;
    } else if (!isCarPresent && carWasPresent) {
      Serial.println("🚙 Vehicle Departed.");
      sendSecureMessage("city/gate/status", "SENSOR:VEHICLE_CLEARED");
      carWasPresent = false;
    }
  }

  // NEW: 1.5 SECURITY TRIPWIRE (IR SENSOR) LOGIC
  int ir_state = digitalRead(GATE_IR_PIN);

  // If someone tries to sneak past (IR triggers) AND it's not a car
  if (ir_state == LOW && !irWasTriggered && !carWasPresent) {
    if (millis() - lastIrTrigger > 5000) { // 5-second cooldown
      irWasTriggered = true;
      lastIrTrigger = millis();
      
      Serial.println("🚨 WARNING: SECURITY BREACH! Tripwire activated.");
      sendSecureMessage("city/gate/status", "ALERT! IR TRIPWIRE BREACHED");
    }
  } 
  // If the object moves away
  else if (ir_state == HIGH && irWasTriggered) {
    irWasTriggered = false;
    Serial.println("✅ Tripwire cleared.");
    sendSecureMessage("city/gate/status", "STATUS:TRIPWIRE_CLEARED");
  }

  // 2. RFID Scanning
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  String cardUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    cardUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    cardUID += String(rfid.uid.uidByte[i], HEX);
  }
  cardUID.toUpperCase();
  
  sendSecureMessage("city/gate/status", "SCANNED_UID:" + cardUID);
  rfid.PICC_HaltA();
  delay(1000); 
}
