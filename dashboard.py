import paho.mqtt.client as mqtt
from Crypto.Cipher import AES
import base64
import hmac
import hashlib
from flask import Flask, render_template
from flask_socketio import SocketIO

# Flask & SocketIO Setup
app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

@app.route('/')
def index():
    return render_template('index.html')

# Cryptographic Constants
SECRET_KEY = b'1234567890123456'
IV =         b'abcdefghijklmnop'
AUTHORIZED_UID = "YOUR_AUTHORIZED_UID_HERE"  # Replace with actual authorized UID for gate access

# Anti-Replay Attack Tracking
node_nonces = {
    "city/gate/status": 0,
    "city/zone1/status": 0,
    "city/traffic/status": 0,
    "city/parking/status": 0
}

# MQTT Callbacks
def on_connect(client, userdata, flags, reason_code, properties):
    print("✅ CENTRAL CONTROL ONLINE - Subscribed to city/+/status")
    client.subscribe("city/+/status") 

def on_message(client, userdata, msg):
    global node_nonces
    topic = msg.topic
    
    try:
        raw_payload = msg.payload.decode('utf-8')
        parts = raw_payload.split(":")
        
        # Ensure payload has Nonce:Data:HMAC
        if len(parts) != 3: return
        incoming_nonce, encrypted_data, received_hmac = int(parts[0]), parts[1], parts[2]
        
        # 1. VERIFY HMAC (Tamper Check)
        data_to_verify = f"{incoming_nonce}:{encrypted_data}".encode('utf-8')
        calculated_hmac = hmac.new(SECRET_KEY, data_to_verify, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(calculated_hmac, received_hmac): 
            print(f"⚠️ TAMPER ALERT on {topic}!")
            return
            
        # 2. VERIFY NONCE (Anti-Replay Attack Check)
        if topic not in node_nonces: node_nonces[topic] = 0 
        if incoming_nonce <= node_nonces[topic]: 
            print(f"⚠️ REPLAY ATTACK BLOCKED on {topic}!")
            return
        node_nonces[topic] = incoming_nonce
        
        # 3. DECRYPT AES-128-CBC
        encrypted_bytes = base64.b64decode(encrypted_data)
        cipher = AES.new(SECRET_KEY, AES.MODE_CBC, IV)
        decrypted_padded = cipher.decrypt(encrypted_bytes)
        # Remove padding
        plaintext = decrypted_padded[:-decrypted_padded[-1]].decode('utf-8')
        
        # Data Routing Based on Topic
        
        # --- 🚪 NODE 1: GATE CHECKPOINT ---
        if topic == "city/gate/status":
            if AUTHORIZED_UID in plaintext:
                socketio.emit('gate_data', {'msg': f"ACCESS GRANTED: {plaintext}"})
            elif "UID" in plaintext or "RFID" in plaintext or len(plaintext) <= 12:
                socketio.emit('gate_data', {'msg': f"ALERT! ACCESS DENIED: {plaintext}"})
            else:
                socketio.emit('gate_data', {'msg': plaintext})
                
        # --- 🌳 NODE 2: PERIMETER ZONE (SUPER NODE) ---
        elif topic == "city/zone1/status":
            if plaintext.startswith("ENV:"):
                # Expected format: ENV:T:25:H:60:L:1024:IR:1:PIR:0
                p = plaintext.split(":") 
                if len(p) >= 11:
                    ir_status = "TRIGGERED" if p[8] == "0" else "CLEAR"
                    pir_status = "MOTION DETECTED" if p[10] == "1" else "STILL"
                    
                    socketio.emit('zone_data', {
                        'temp': p[2], 'hum': p[4], 'light': p[6],
                        'ir': ir_status, 'pir': pir_status
                    })
            else:
                socketio.emit('zone_alert', {'msg': plaintext})
                
        # --- 🚦 NODE 3: TRAFFIC JUNCTION ---
        elif topic == "city/traffic/status":
            if plaintext.startswith("ENV:"):
                # Expected format: ENV:L:450:IR1:1:IR2:0
                p = plaintext.split(":") 
                ir1 = "CLEAR" if p[4] == "1" else "WAITING"
                ir2 = "CLEAR" if p[6] == "1" else "WAITING"
                socketio.emit('traffic_data', {'light': p[2], 'lane1': ir1, 'lane2': ir2})
            else:
                socketio.emit('traffic_alert', {'msg': plaintext})
                
        # --- 🅿️ NODE 4: SMART PARKING ---
        elif topic == "city/parking/status":
            if plaintext.startswith("PARKING:"):
                # Expected format: PARKING:L:1024:P1:FREE:P2:OCCUPIED:FREE:1
                p = plaintext.split(":") 
                socketio.emit('parking_data', {'light': p[2], 'p1': p[4], 'p2': p[6], 'free': p[8]})

    except Exception as e:
        print(f"Decode Error on {topic}: {e}")


# System Initialization
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

if __name__ == '__main__':
    print("🏙️ BOOTING SMART CITY COMMAND CENTER...")
    try:
        mqtt_client.connect("localhost", 1883, 60)
        mqtt_client.loop_start() 
        socketio.run(app, host='0.0.0.0', port=5000, debug=False)
    except KeyboardInterrupt:
        print("\n🛑 SHUTTING DOWN COMMAND CENTER...")
        mqtt_client.loop_stop()
