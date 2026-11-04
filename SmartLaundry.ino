#define BLYNK_TEMPLATE_ID "TMPL63RxQ8d0R"
#define BLYNK_TEMPLATE_NAME "Phơi Đồ Thông Minh"
#define BLYNK_AUTH_TOKEN "Vg7YiI2lSyOc_JUuigMMy60jMsIuppoO"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

// ====== WiFi ======
char ssid[] = "GDU-Core";
char pass[] = "GDU2024@371NK@";

// ====== Biến điều khiển ======
bool autoMode = true;
float targetTemp = 40.0;
float targetHum = 60.0;
bool manualFanState = false;
bool manualServoState = false;

// ====== Timer ======
BlynkTimer timer;

// ====== Pin ======
#define RAIN_SENSOR_ANALOG A0
#define RAIN_SENSOR_DIGITAL 12 // D6
#define DHT_PIN 15 // D8
#define RELAY_PIN 14 // D5
#define SERVO_PIN 2  // D4

// ====== DHT ======
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// ====== LCD vật lý ======
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ====== LCD ảo ======
WidgetLCD blynkLcd(V10);

// ====== Servo ======
Servo servoMotor;

// ====== Ngưỡng mưa ======
const int rainWetThreshold = 500;  // ngưỡng ướt
const int rainDryThreshold = 600;  // ngưỡng khô
const unsigned long rainDelay = 5000; // 5 giây chống nhiễu

// ====== Biến servo ======
static int lastServoAngle = 90; // mặc định thu vào
unsigned long lastServoMove = 0;

// ====== Biến mưa ======
bool isRaining = false;
unsigned long lastRainTime = 0;

// ====== Blynk Handlers ======
BLYNK_WRITE(V0) { autoMode = param.asInt(); }
BLYNK_WRITE(V1) { targetTemp = param.asFloat(); }
BLYNK_WRITE(V2) { targetHum = param.asFloat(); }
BLYNK_WRITE(V3) { manualFanState = param.asInt(); }
BLYNK_WRITE(V4) { manualServoState = param.asInt(); }

// ====== Hàm lấy trung bình 5 mẫu analog cảm biến mưa ======
int readRainSensorAverage() {
  long sum = 0;
  const int samples = 5;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(RAIN_SENSOR_ANALOG);
    delay(10);
  }
  return sum / samples;
}

// ====== Gửi dữ liệu lên Blynk ======
void sendBlynkData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int rainA = readRainSensorAverage();

  Blynk.virtualWrite(V5, t);
  Blynk.virtualWrite(V6, h);
  Blynk.virtualWrite(V7, digitalRead(RELAY_PIN));
  Blynk.virtualWrite(V8, lastServoAngle);
  Blynk.virtualWrite(V9, isRaining ? "Mua" : "Khong");
}

// ====== Kết nối WiFi ======
void connectWiFi() {
  WiFi.begin(ssid, pass);
  Serial.print("Dang ket noi WiFi");
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi da ket noi!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nKet noi WiFi that bai!");
  }
}

// ====== Hàm quay servo từ từ ======
void moveServoGradual(int targetAngle) {
  if (targetAngle != lastServoAngle && millis() - lastServoMove > 500) {
    servoMotor.attach(SERVO_PIN, 600, 2300);
    int step = (targetAngle > lastServoAngle) ? 2 : -2;
    for (int pos = lastServoAngle; pos != targetAngle; pos += step) {
      servoMotor.write(pos);
      delay(20);
    }
    servoMotor.write(targetAngle);
    delay(200);
    servoMotor.detach();
    lastServoAngle = targetAngle;
    lastServoMove = millis();
  }
}

// ====== Điều khiển ======
void controlSystem() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  int rainAnalog = readRainSensorAverage();
  int rainDigital = digitalRead(RAIN_SENSOR_DIGITAL);

  // Cập nhật trạng thái mưa với hysteresis + chống nhiễu
  if (rainDigital == LOW || rainAnalog < rainWetThreshold) {
    isRaining = true;
    lastRainTime = millis();
  } else if (rainAnalog > rainDryThreshold && millis() - lastRainTime > rainDelay) {
    isRaining = false;
  }

  // LCD vật lý
  lcd.setCursor(0, 0);
  lcd.print("Nhiet:");
  lcd.print(isnan(temperature) ? 0 : temperature, 1);
  lcd.print("C ");

  lcd.setCursor(0, 1);
  lcd.print("Do am:");
  lcd.print(isnan(humidity) ? 0 : humidity, 1);
  lcd.print("% ");

  // ====== Chế độ TỰ ĐỘNG ======
  if (autoMode) {
    int newAngle = (isRaining || humidity > targetHum || temperature > targetTemp) ? 90 : 180;

    moveServoGradual(newAngle);
    digitalWrite(RELAY_PIN, (newAngle == 90) ? HIGH : LOW);

    lcd.setCursor(10, 0); lcd.print((newAngle == 90) ? "THU" : "MO ");
    lcd.setCursor(10, 1); lcd.print((newAngle == 90) ? "QUATON" : "QUATOFF");

    blynkLcd.clear();
    blynkLcd.print(0, 0, "Nhiet:" + String(isnan(temperature) ? 0 : temperature, 1) + "C");
    blynkLcd.print(0, 1, "Do am:" + String(isnan(humidity) ? 0 : humidity, 1) + "%");
    blynkLcd.print(10, 0, (newAngle == 90) ? "THU" : "MO");
    blynkLcd.print(10, 1, (newAngle == 90) ? "QUATON" : "QUATOFF");
  }
  // ====== Chế độ THỦ CÔNG ======
  else {
    int manualAngle = manualServoState ? 180 : 90;

    moveServoGradual(manualAngle);
    digitalWrite(RELAY_PIN, manualFanState ? HIGH : LOW);

    lcd.setCursor(10, 0); lcd.print(manualServoState ? "MO " : "THU");
    lcd.setCursor(10, 1); lcd.print(manualFanState ? "QUATON" : "QUATOFF");

    blynkLcd.clear();
    blynkLcd.print(0, 0, "Nhiet:" + String(isnan(temperature) ? 0 : temperature, 1) + "C");
    blynkLcd.print(0, 1, "Do am:" + String(isnan(humidity) ? 0 : humidity, 1) + "%");
    blynkLcd.print(10, 0, manualServoState ? "MO" : "THU");
    blynkLcd.print(10, 1, manualFanState ? "QUATON" : "QUATOFF");
  }

  // Debug Serial
  Serial.print("Nhiet do: "); Serial.print(temperature);
  Serial.print(" *C, Do am: "); Serial.print(humidity);
  Serial.print(" %, Mua: "); Serial.print(isRaining ? "Co" : "Khong");
  Serial.print(", analog: "); Serial.println(rainAnalog);
}

// ====== Setup ======
void setup() {
  Serial.begin(115200);
  connectWiFi();

  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    if (Blynk.connect(5000)) {
      Serial.println("Da ket noi Blynk!");
    } else {
      Serial.println("Ket noi Blynk that bai!");
    }
  }

  Wire.begin(5, 4);
  lcd.init();
  lcd.backlight();

  pinMode(RAIN_SENSOR_DIGITAL, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();
  servoMotor.attach(SERVO_PIN, 600, 2300);
  servoMotor.write(lastServoAngle);
  delay(500);
  servoMotor.detach();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Khoi dong...");
  delay(1000);

  timer.setInterval(1000L, sendBlynkData);
}

// ====== Loop ======
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  } else if (!Blynk.connected()) {
    Blynk.connect(5000);
  }

  Blynk.run();
  timer.run();
  controlSystem();
}
