/***********************************************************************************
 * HỆ THỐNG GIÀN PHƠI ĐỒ THÔNG MINH - ESP8266 (SMART CLOTHES DRYING RACK)
 * Nâng cấp chuẩn công nghiệp: MQTT Broker (HiveMQ) + Local REST API + Cảm biến môi trường
 * Pinout chuẩn 100% phần cứng cũ:
 *  - Cảm biến mưa Analog: A0
 *  - Cảm biến mưa Digital: D6 (GPIO 12)
 *  - Cảm biến nhiệt độ & độ ẩm DHT11: D8 (GPIO 15)
 *  - Relay Quạt sấy: D5 (GPIO 14)
 *  - Servo giàn phơi: D4 (GPIO 2)
 *  - Màn hình LCD 1602 I2C: D1 (GPIO 5), D2 (GPIO 4) - Địa chỉ 0x27
 ***********************************************************************************/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

// ===== CẤU HÌNH WIFI =====
const char* ssid = "Huy";
const char* password = "01234567899";

// ===== CẤU HÌNH MQTT =====
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_client_id = "SmartRack_ESP8266_Client";

// MQTT Topics
#define TOPIC_SENSOR       "phoido/sensor"        // Gửi data nhiệt ẩm mưa JSON lên app
#define TOPIC_STATUS       "phoido/status"        // Gửi trạng thái online/offline
#define TOPIC_CMD_MODE     "phoido/control/mode"  // Nhận lệnh "AUTO" / "MANUAL"
#define TOPIC_CMD_RACK     "phoido/control/rack"  // Nhận lệnh "OPEN" / "CLOSE"
#define TOPIC_CMD_FAN      "phoido/control/fan"   // Nhận lệnh "ON" / "OFF"

// ===== ĐỊNH NGHĨA CHÂN PHẦN CỨNG (GIỮ NGUYÊN) =====
#define RAIN_SENSOR_ANALOG  A0
#define RAIN_SENSOR_DIGITAL 12  // D6
#define DHT_PIN             15  // D8
#define RELAY_PIN           14  // D5
#define SERVO_PIN           2   // D4

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servoMotor;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
ESP8266WebServer server(80);

// ===== BIẾN TRẠNG THÁI HỆ THỐNG =====
bool autoMode = true;         // Mặc định chạy tự động
bool isRaining = false;        // Cờ phát hiện mưa
bool fanState = false;         // Trạng thái quạt sấy
int currentServoAngle = 90;    // 90: Thu vào (Đóng) | 180: Mở ra ngoài (Phơi)
int targetServoAngle = 90;

// Ngưỡng cài đặt
float thresholdTemp = 38.0;   // Ngưỡng nhiệt độ cảnh báo nắng gắt
float thresholdHum = 75.0;    // Ngưỡng độ ẩm cao (nguy cơ mưa ẩm)
const int rainWetThreshold = 500;  // Giá trị analog ngưỡng mưa
const int rainDryThreshold = 650;  // Ngưỡng khô ráo

// Bộ đếm thời gian (Non-blocking timing)
unsigned long lastSensorReadTime = 0;
unsigned long lastMqttReconnect = 0;
unsigned long lastRainDetectionTime = 0;
const unsigned long rainDebounceDelay = 4000; // Chống nhiễu mưa 4 giây

// Đọc trung bình mẫu cảm biến mưa analog
int readRainSensorAverage() {
  long sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += analogRead(RAIN_SENSOR_ANALOG);
    delay(5);
  }
  return (int)(sum / 5);
}

// Hàm di chuyển Servo góc từ từ để bảo vệ cơ cấu giàn phơi
void moveServoSmooth(int target) {
  if (target == currentServoAngle) return;
  servoMotor.attach(SERVO_PIN, 600, 2300);
  int step = (target > currentServoAngle) ? 2 : -2;
  for (int pos = currentServoAngle; pos != target; pos += step) {
    servoMotor.write(pos);
    delay(15);
  }
  servoMotor.write(target);
  delay(100);
  servoMotor.detach(); // Ngắt xung sau khi quay xong để chống giật và tiết kiệm điện
  currentServoAngle = target;
}

// Cập nhật màn hình LCD 1602
void updateLCD(float temp, float hum, bool raining, bool rackOpen, bool fan) {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(isnan(temp) ? 0 : (int)temp);
  lcd.print("C H:");
  lcd.print(isnan(hum) ? 0 : (int)hum);
  lcd.print("% ");
  lcd.print(raining ? "MUA " : "KHO ");

  lcd.setCursor(0, 1);
  lcd.print("GIAN:");
  lcd.print(rackOpen ? "MO " : "THU");
  lcd.print(" | Q:");
  lcd.print(fan ? "ON " : "OFF");
  lcd.print(autoMode ? " [A]" : " [M]");
}

// Publish dữ liệu cảm biến JSON lên MQTT
void publishSensorData(float temp, float hum, int rainAnalog, bool raining) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"temp\":%.1f,\"hum\":%.1f,\"rain_analog\":%d,\"is_raining\":%s,\"rack\":\"%s\",\"fan\":\"%s\",\"mode\":\"%s\"}",
    isnan(temp) ? 0.0 : temp,
    isnan(hum) ? 0.0 : hum,
    rainAnalog,
    raining ? "true" : "false",
    (currentServoAngle == 180) ? "OPEN" : "CLOSE",
    fanState ? "ON" : "OFF",
    autoMode ? "AUTO" : "MANUAL"
  );
  mqttClient.publish(TOPIC_SENSOR, payload);
}

// Xử lý callback MQTT khi nhận lệnh từ ứng dụng Mobile
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();
  String top = String(topic);

  Serial.print("MQTT Received [");
  Serial.print(top);
  Serial.print("]: ");
  Serial.println(message);

  // Xử lý chuyển đổi Chế độ Auto / Manual
  if (top == TOPIC_CMD_MODE) {
    if (message == "AUTO") {
      autoMode = true;
      Serial.println("Chuyen sang che do: AUTO");
    } else if (message == "MANUAL") {
      autoMode = false;
      Serial.println("Chuyen sang che do: MANUAL");
    }
  }

  // Xử lý lệnh Mở / Thu giàn phơi (khi Manual)
  if (top == TOPIC_CMD_RACK) {
    if (!autoMode) {
      if (message == "OPEN" || message == "180") {
        moveServoSmooth(180);
        Serial.println("MANUAL: Mo gian phoi");
      } else if (message == "CLOSE" || message == "90") {
        moveServoSmooth(90);
        Serial.println("MANUAL: Thu gian phoi");
      }
    }
  }

  // Xử lý lệnh Bật / Tắt Quạt sấy (khi Manual)
  if (top == TOPIC_CMD_FAN) {
    if (!autoMode) {
      if (message == "ON") {
        fanState = true;
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("MANUAL: Bat quat say");
      } else {
        fanState = false;
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("MANUAL: Tat quat say");
      }
    }
  }
}

// Kết nối lại MQTT Broker
void reconnectMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("Dang ket noi MQTT Broker...");
    if (mqttClient.connect(mqtt_client_id, TOPIC_STATUS, 1, true, "offline")) {
      Serial.println(" Thanh cong!");
      mqttClient.publish(TOPIC_STATUS, "online", true);
      mqttClient.subscribe(TOPIC_CMD_MODE);
      mqttClient.subscribe(TOPIC_CMD_RACK);
      mqttClient.subscribe(TOPIC_CMD_FAN);
    } else {
      Serial.print(" That bai, rc=");
      Serial.println(mqttClient.state());
    }
  }
}

// ===== REST API HANDLERS =====
void handleRestStatus() {
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int rainAnalog = readRainSensorAverage();

  String json = "{";
  json += "\"temp\":" + String(isnan(temp) ? 0 : temp, 1) + ",";
  json += "\"hum\":" + String(isnan(hum) ? 0 : hum, 1) + ",";
  json += "\"rain_analog\":" + String(rainAnalog) + ",";
  json += "\"is_raining\":" + String(isRaining ? "true" : "false") + ",";
  json += "\"rack\":\"" + String(currentServoAngle == 180 ? "OPEN" : "CLOSE") + "\",";
  json += "\"fan\":\"" + String(fanState ? "ON" : "OFF") + "\",";
  json += "\"mode\":\"" + String(autoMode ? "AUTO" : "MANUAL") + "\"";
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleRestControl() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    autoMode = (m.equalsIgnoreCase("auto"));
  }
  if (!autoMode) {
    if (server.hasArg("rack")) {
      String r = server.arg("rack");
      if (r.equalsIgnoreCase("open")) moveServoSmooth(180);
      else if (r.equalsIgnoreCase("close")) moveServoSmooth(90);
    }
    if (server.hasArg("fan")) {
      String f = server.arg("fan");
      fanState = (f.equalsIgnoreCase("on"));
      digitalWrite(RELAY_PIN, fanState ? HIGH : LOW);
    }
  }
  handleRestStatus();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- HE THONG GIAN PHOI THONG MINH DANG KHOI DONG ---");

  // Khởi tạo chân I/O
  pinMode(RAIN_SENSOR_DIGITAL, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Mặc định tắt quạt sấy

  // Khởi tạo I2C và LCD
  Wire.begin(5, 4); // D1=5 (SCL), D2=4 (SDA)
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Gian Phoi IoT");
  lcd.setCursor(0, 1);
  lcd.print("Dang khoi dong..");

  // Khởi tạo DHT11 & Servo
  dht.begin();
  servoMotor.attach(SERVO_PIN, 600, 2300);
  servoMotor.write(90); // Ban đầu vị trí thu vào an toàn
  delay(500);
  servoMotor.detach();

  // Kết nối WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Ket noi WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi da ket noi! IP: " + WiFi.localIP().toString());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(1500);

  // Khởi tạo MQTT & REST API
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  server.on("/status", HTTP_GET, handleRestStatus);
  server.on("/control", HTTP_GET, handleRestControl);
  server.begin();
  Serial.println("REST API Server da san sang tren cong 80!");
}

void loop() {
  // Duy trì kết nối MQTT
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnect > 5000) {
      lastMqttReconnect = now;
      reconnectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // Xử lý request REST API
  server.handleClient();

  // Đọc cảm biến định kỳ mỗi 2 giây
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorReadTime >= 2000) {
    lastSensorReadTime = currentMillis;

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int rainAnalog = readRainSensorAverage();
    int rainDigital = digitalRead(RAIN_SENSOR_DIGITAL);

    // Lọc nhiễu mưa kết hợp Hysteresis
    if (rainDigital == LOW || rainAnalog < rainWetThreshold) {
      isRaining = true;
      lastRainDetectionTime = currentMillis;
    } else if (rainAnalog > rainDryThreshold && (currentMillis - lastRainDetectionTime > rainDebounceDelay)) {
      isRaining = false;
    }

    // LOGIC ĐIỀU KHIỂN TỰ ĐỘNG
    if (autoMode) {
      // Nếu trời mưa HOẶC độ ẩm quá cao -> Tự động THU giàn phơi (góc 90) và BẬT quạt sấy
      if (isRaining || (!isnan(hum) && hum > thresholdHum)) {
        moveServoSmooth(90);
        fanState = true;
        digitalWrite(RELAY_PIN, HIGH);
      }
      // Nếu trời nắng ráo -> Tự động MỞ giàn phơi (góc 180) và TẮT quạt sấy
      else {
        moveServoSmooth(180);
        fanState = false;
        digitalWrite(RELAY_PIN, LOW);
      }
    }

    // Hiển thị LCD & gửi dữ liệu lên App
    updateLCD(temp, hum, isRaining, (currentServoAngle == 180), fanState);
    publishSensorData(temp, hum, rainAnalog, isRaining);

    Serial.printf("T: %.1fC | H: %.1f%% | Mua: %s | Gian: %s | Quat: %s\n",
      temp, hum, isRaining ? "CO" : "KHONG",
      (currentServoAngle == 180) ? "MO" : "THU",
      fanState ? "BAT" : "TAT"
    );
  }
}
