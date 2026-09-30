#include <WiFi.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <ModbusMaster.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ==========================================
// 1. ตั้งค่าเครือข่าย WiFi
// ==========================================
#define WIFI_SSID "iPhone" 
#define WIFI_PASSWORD "11111111" 

// ==========================================
// 2. ตั้งค่า InfluxDB Cloud 2 (สำหรับส่งข้อมูลเข้า Grafana)
// ==========================================
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "ySs2861MfvA-m7qVWpgbWnpRfXHR_n2XJaGrO5U8XHABvZiZijSfBHbLFHRtITTQ0k1MP88e7U0CIidq9cgtfQ=="
#define INFLUXDB_ORG "zzuull3359@gmail.com"
#define INFLUXDB_BUCKET "soil_data"

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensorData("soil_metrics");

// ==========================================
// 3. ตั้งค่า Real-time MQTT Broker (แก้ปัญหา Delay < 50ms)
// ==========================================
// ค่าเริ่มต้นใช้ broker.hivemq.com (พอร์ต 1883 สำหรับ TCP และ 8884 สำหรับ WebSockets)
// หากใช้ HiveMQ Cloud Private Cluster ให้ใส่ Host, Port 8883, User, Pass ที่นี่
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = ""; 
const char* mqtt_pass = ""; 

const char* topic_cmd = "greenmind_smartfarm_zul/cmd";
const char* topic_status = "greenmind_smartfarm_zul/status";
const char* topic_sensors = "greenmind_smartfarm_zul/sensors";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ==========================================
// 4. ตั้งค่า AI API (Render)
// ==========================================
const char* api_url = "https://greenmind-ai-040a.onrender.com/predict";

// ==========================================
// 5. ตั้งค่าพินฮาร์ดแวร์ Relay & Modbus RS485
// ==========================================
#define RELAY_IN1 33  // ปั๊มน้ำหลัก (Main Pump)
#define RELAY_IN2 14  // ปั๊มวิตามิน A (Relay 3 ใน UI)
#define RELAY_IN3 27  // วาล์วน้ำเปล่า (Relay 2 ใน UI)
#define RELAY_IN4 26  // ปั๊มวิตามิน B (Relay 4 ใน UI)

#define MAX485_RE_NEG   4  
#define RX_PIN          16 
#define TX_PIN          17 

ModbusMaster node;
LiquidCrystal_I2C lcd(0x27, 16, 2);

void preTransmission() { digitalWrite(MAX485_RE_NEG, 1); }
void postTransmission() { Serial2.flush(); digitalWrite(MAX485_RE_NEG, 0); }

// ==========================================
// 6. ตัวแปรสถานะและ Timer (Non-blocking)
// ==========================================
int r1 = 0, r2 = 0, r3 = 0, r4 = 0;
unsigned long r1OffTime = 0;
unsigned long r2OffTime = 0;
unsigned long r3OffTime = 0;
unsigned long r4OffTime = 0;

String sysMode = "manual";
String currentTargetCrop = "ผักบุ้งจีน";

unsigned long lastTelemetryMillis = 0;
const long telemetryInterval = 15000; // ส่ง InfluxDB & เซ็นเซอร์ทุก 15 วิ
unsigned long lastMqttRetry = 0;

// ==========================================
// 7. ฟังก์ชันควบคุม Relay
// ==========================================
void updateRelayPins() {
    // กฎความปลอดภัย: ถ้ามีวาล์วน้ำ หรือปั๊มปุ๋ยเปิด ให้เปิดปั๊มหลักเสมอ
    if (r2 == 1 || r3 == 1 || r4 == 1) {
        r1 = 1;
    }
    digitalWrite(RELAY_IN1, r1 == 1 ? HIGH : LOW);
    digitalWrite(RELAY_IN3, r2 == 1 ? HIGH : LOW);
    digitalWrite(RELAY_IN2, r3 == 1 ? HIGH : LOW);
    digitalWrite(RELAY_IN4, r4 == 1 ? HIGH : LOW);
}

void publishStatus() {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<256> doc;
    doc["online"] = true;
    doc["status"] = "online";
    doc["sys_mode"] = sysMode;
    doc["target_crop"] = currentTargetCrop;
    
    JsonObject relays = doc.createNestedObject("relays");
    relays["r1"] = r1;
    relays["r2"] = r2;
    relays["r3"] = r3;
    relays["r4"] = r4;
    
    char buffer[256];
    serializeJson(doc, buffer);
    mqttClient.publish(topic_status, buffer, true);
}

// ==========================================
// 8. ฟังก์ชัน Callback เมื่อมีคำสั่ง MQTT ส่งมาจากหน้าเว็บ (Instant < 50ms)
// ==========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
        return;
    }

    String cmd = doc["cmd"] | "";
    Serial.print("\n⚡ [MQTT CMD] "); Serial.println(cmd);

    if (cmd == "SET_RELAY") {
        String relay = doc["relay"] | "";
        int state = doc["state"] | 0;
        int duration = doc["duration"] | 0;
        unsigned long offTime = (state == 1 && duration > 0) ? (millis() + (duration * 1000UL)) : 0;

        if (relay == "relay_1") { r1 = state; r1OffTime = offTime; }
        else if (relay == "relay_2") { r2 = state; r2OffTime = offTime; }
        else if (relay == "relay_3") { r3 = state; r3OffTime = offTime; }
        else if (relay == "relay_4") { r4 = state; r4OffTime = offTime; }

        updateRelayPins();
        publishStatus();
        Serial.printf("⚡ Relay Updated -> Pump:%d | Water:%d VitaA:%d VitaB:%d\n", r1, r2, r3, r4);
    }
    else if (cmd == "SET_MODE") {
        sysMode = doc["mode"] | "manual";
        publishStatus();
        Serial.println("System Mode: " + sysMode);
    }
    else if (cmd == "SET_CROP") {
        currentTargetCrop = doc["crop"] | "ผักบุ้งจีน";
        publishStatus();
        Serial.println("Target Crop: " + currentTargetCrop);
    }
    else if (cmd == "STOP_ALL") {
        r1 = 0; r2 = 0; r3 = 0; r4 = 0;
        r1OffTime = 0; r2OffTime = 0; r3OffTime = 0; r4OffTime = 0;
        updateRelayPins();
        publishStatus();
        Serial.println("🛑 Emergency Stop Executed!");
    }
    else if (cmd == "GET_STATUS") {
        publishStatus();
    }
}

// ==========================================
// 9. ฟังก์ชันเชื่อมต่อ MQTT Broker (Non-blocking)
// ==========================================
void reconnectMQTT() {
    if (millis() - lastMqttRetry < 4000) return;
    lastMqttRetry = millis();

    if (WiFi.status() == WL_CONNECTED && !mqttClient.connected()) {
        String clientId = "ESP32_SmartFarm_" + String(random(0xffff), HEX);
        const char* willTopic = topic_status;
        const char* willMsg = "{\"online\":false,\"status\":\"offline\"}";

        bool connected = false;
        if (strlen(mqtt_user) > 0) {
            connected = mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass, willTopic, 1, true, willMsg);
        } else {
            connected = mqttClient.connect(clientId.c_str(), willTopic, 1, true, willMsg);
        }

        if (connected) {
            Serial.println("✅ Connected to MQTT Broker!");
            mqttClient.subscribe(topic_cmd);
            publishStatus();
        } else {
            Serial.print("⚠️ MQTT Connect Failed, rc=");
            Serial.println(mqttClient.state());
        }
    }
}

// ==========================================
// 10. ฟังก์ชันสอบถาม AI Recommender (Render API)
// ==========================================
String getCropRecommendation(float n, float p, float k, float temp, float moisture) {
    String crop_result = "No Data";
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.setTimeout(4000); 
        http.begin(api_url);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> doc;
        doc["N"] = n;
        doc["P"] = p;
        doc["K"] = k;
        doc["temperature"] = temp;
        doc["humidity"] = moisture;
        
        String requestBody;
        serializeJson(doc, requestBody);
        
        int httpResponseCode = http.POST(requestBody);
        if (httpResponseCode == 200) {
            String response = http.getString();
            StaticJsonDocument<512> responseDoc;
            DeserializationError error = deserializeJson(responseDoc, response);
            if (!error && responseDoc.containsKey("recommended_crop")) {
                crop_result = responseDoc["recommended_crop"].as<String>();
            } else {
                crop_result = "Invalid AI Format";
            }
        } else {
            crop_result = "Render API " + String(httpResponseCode);
        }
        http.end(); 
    }
    return crop_result;
}

// ==========================================
// 11. อ่านเซ็นเซอร์ & ส่งข้อมูลเข้า Grafana ผ่าน InfluxDB
// ==========================================
void handleTelemetryAndGrafana() {
    if (WiFi.status() != WL_CONNECTED) return;

    int clearCount = 0;
    while (Serial2.available() && clearCount < 100) {
        Serial2.read();
        clearCount++;
    }

    uint8_t modbus_result = node.readHoldingRegisters(0x0000, 7); 
    if (modbus_result == node.ku8MBSuccess) {
        float moisture = node.getResponseBuffer(0) / 10.0;
        float temp = node.getResponseBuffer(1) / 10.0;
        float ec = node.getResponseBuffer(2);
        float ph = node.getResponseBuffer(3) / 10.0;
        float n = node.getResponseBuffer(4);
        float p = node.getResponseBuffer(5);
        float k = node.getResponseBuffer(6);

        // 📺 1. แสดงผลออกจอ LCD
        lcd.clear();
        lcd.setCursor(0, 0); 
        lcd.print("M:"); lcd.print(moisture, 1); lcd.print("% T:"); lcd.print(temp, 1); lcd.print("C");
        lcd.setCursor(0, 1); 
        lcd.print("N:"); lcd.print(n, 0); lcd.print(" P:"); lcd.print(p, 0); lcd.print(" K:"); lcd.print(k, 0);

        // 🤖 2. ดึงคำแนะนำจากโมเดล AI 15 พืช
        String ai_recommend = getCropRecommendation(n, p, k, temp, moisture);
        Serial.println("AI Recommended: " + ai_recommend);

        // 📊 3. ส่งข้อมูลขึ้น InfluxDB (รักษาให้ Grafana ใช้งานได้ 100%)
        sensorData.clearFields();
        sensorData.addField("moisture", moisture);
        sensorData.addField("temperature", temp);
        sensorData.addField("ec", ec);
        sensorData.addField("ph", ph);
        sensorData.addField("nitrogen", n);
        sensorData.addField("phosphorus", p);
        sensorData.addField("potassium", k);
        sensorData.addField("status_pump", r1);
        sensorData.addField("status_water", r2);
        sensorData.addField("status_vita", r3);
        sensorData.addField("status_vitb", r4);
        sensorData.addField("ai_recommend", ai_recommend);

        if (client.writePoint(sensorData)) {
            Serial.println("✅ InfluxDB Write OK (Grafana Synced)");
        } else {
            Serial.print("❌ InfluxDB Write Failed: ");
            Serial.println(client.getLastErrorMessage());
        }

        // 📡 4. ส่งค่าเซ็นเซอร์ขึ้น MQTT ให้หน้าเว็บ Control Center แสดงผล
        if (mqttClient.connected()) {
            StaticJsonDocument<384> doc;
            doc["moisture"] = moisture;
            doc["temperature"] = temp;
            doc["ec"] = ec;
            doc["ph"] = ph;
            doc["nitrogen"] = n;
            doc["phosphorus"] = p;
            doc["potassium"] = k;
            doc["ai_recommend"] = ai_recommend;

            char buffer[384];
            serializeJson(doc, buffer);
            mqttClient.publish(topic_sensors, buffer);
        }

        Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    } else {
        Serial.printf("Modbus Read Failed! Code: %d\n", modbus_result);
    }
}

// ==========================================
// 12. Setup
// ==========================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n--- Smart Farm V4 (Real-time MQTT + InfluxDB) ---");

    lcd.init(); 
    lcd.backlight();
    lcd.setCursor(0, 0); lcd.print("Smart Farm V4");
    lcd.setCursor(0, 1); lcd.print("Connecting WiFi");

    pinMode(RELAY_IN1, OUTPUT);
    pinMode(RELAY_IN2, OUTPUT);
    pinMode(RELAY_IN3, OUTPUT);
    pinMode(RELAY_IN4, OUTPUT);
    
    digitalWrite(RELAY_IN1, LOW);
    digitalWrite(RELAY_IN2, LOW);
    digitalWrite(RELAY_IN3, LOW);
    digitalWrite(RELAY_IN4, LOW);

    pinMode(MAX485_RE_NEG, OUTPUT);
    digitalWrite(MAX485_RE_NEG, 0);
    Serial2.begin(4800, SERIAL_8N1, RX_PIN, TX_PIN);
    node.begin(1, Serial2);
    node.preTransmission(preTransmission);
    node.postTransmission(postTransmission);

    Serial.print("Connecting WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); 
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
    
    timeSync("ICT-7", "pool.ntp.org", "time.nis.gov");
    client.setInsecure(); 

    if (client.validateConnection()) {
        Serial.println("Connected to InfluxDB Cloud!");
        lcd.clear(); 
        lcd.setCursor(0, 0); lcd.print("Smart Farm V4");
        lcd.setCursor(0, 1); lcd.print("WiFi Connected!");
        delay(1500); 
    }

    // ตั้งค่า MQTT
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    reconnectMQTT();
}

// ==========================================
// 13. Loop (Non-blocking Real-time Loop)
// ==========================================
void loop() {
    // 1. MQTT Event Loop (รับคำสั่งทันที < 50ms)
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqttClient.connected()) {
            reconnectMQTT();
        } else {
            mqttClient.loop();
        }
    } else {
        static unsigned long lastWifiRetry = 0;
        if (millis() - lastWifiRetry > 8000) {
            lastWifiRetry = millis();
            Serial.println("WiFi reconnecting...");
            WiFi.reconnect();
        }
    }

    // 2. ตรวจสอบการนับถอยหลังของ Relay แต่ละช่อง (Safety Timer)
    unsigned long now = millis();
    bool needUpdate = false;

    if (r2 == 1 && r2OffTime > 0 && now >= r2OffTime) { r2 = 0; r2OffTime = 0; needUpdate = true; }
    if (r3 == 1 && r3OffTime > 0 && now >= r3OffTime) { r3 = 0; r3OffTime = 0; needUpdate = true; }
    if (r4 == 1 && r4OffTime > 0 && now >= r4OffTime) { r4 = 0; r4OffTime = 0; needUpdate = true; }
    if (r1 == 1 && r1OffTime > 0 && now >= r1OffTime && r2 == 0 && r3 == 0 && r4 == 0) { r1 = 0; r1OffTime = 0; needUpdate = true; }

    if (needUpdate) {
        updateRelayPins();
        publishStatus();
        Serial.println("⏱️ Timer Expired -> Device auto turned off");
    }

    // 3. งานส่งข้อมูลเข้า InfluxDB สำหรับ Grafana (ทุก 15 วินาที)
    if (now - lastTelemetryMillis >= telemetryInterval) {
        lastTelemetryMillis = now;
        handleTelemetryAndGrafana();
    }

    yield();
}