/*
統合スケッチ: 腕振り検知 (ADXL345) と複数BLEデバイスのRSSIを WiFi 経由で TCP 送信します。
- BLEスキャンは FreeRTOSタスク でバックグラウンド実行します。
- 腕振りを検知したら TCP送信します。
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// -------------------- ユーザー設定 --------------------
const char* WIFI_SSID = "group5";
const char* WIFI_PASS = "12345678";

const char* SERVER_IP = "192.168.137.1";
const uint16_t SERVER_PORT = 12345;

// 複数デバイスのMACアドレス（小文字で統一）
const char* targetAddresses[] = {
  "58:8c:81:9d:b8:de",
  "aa:bb:cc:dd:ee:ff",
  "11:22:33:44:55:66"
};
const int NUM_TARGETS = sizeof(targetAddresses) / sizeof(targetAddresses[0]);
// -----------------------------------------------------

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

Adafruit_NeoPixel pixels(10, D10, NEO_GRB + NEO_KHZ800);

// 加速度関連
bool isWaving = false;
bool flag = true;

// BLE スキャン結果（複数デバイス分）
volatile bool foundTarget[NUM_TARGETS];
volatile int lastRSSI[NUM_TARGETS];

// スキャン設定
const uint32_t SCAN_DURATION = 4294967295;
const uint32_t SCAN_INTERVAL = 100;
BLEScan* scan;

// BLE スキャンタスクの関数プロトタイプ
void bleScanTask(void *pvParameters);

// --------------------------------------------------
// BLE スキャン結果コールバック
// --------------------------------------------------
class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    std::string advAddr = dev.getAddress().toString();
    for (int i = 0; i < NUM_TARGETS; i++) {
      if (advAddr == targetAddresses[i]) {
        foundTarget[i] = true;
        lastRSSI[i] = dev.getRSSI();
        Serial.printf("[BLE] Found %s, RSSI=%d\n", advAddr.c_str(), lastRSSI[i]);
      }
    }
  }
};

// --------------------------------------------------
// BLE スキャンタスク (FreeRTOSタスク)
// --------------------------------------------------
void bleScanTask(void *pvParameters) {
    for (;;) {
        // スキャン前に結果をリセット
        for (int i = 0; i < NUM_TARGETS; i++) {
          foundTarget[i] = false;
          lastRSSI[i] = -127;
        }

        Serial.println("[BLE Task] Start scan...");
        scan->start(SCAN_DURATION / 1000, false); 
        vTaskDelay(pdMS_TO_TICKS(SCAN_DURATION)); 
        scan->stop();

        for (int i = 0; i < NUM_TARGETS; i++) {
          Serial.printf("[BLE Task] Target %d: Found=%s, RSSI=%d\n", i, foundTarget[i] ? "Yes" : "No", lastRSSI[i]);
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL));
    }
}

// --------------------------------------------------
// 腕振り判定（簡易 Z軸 方式）
// --------------------------------------------------
bool CheckMotion() {
  sensors_event_t event;
  accel.getEvent(&event);

  isWaving = false;
  float z = fabs(event.acceleration.z);

  if (z > 14) {
    isWaving = true;
  }
  return isWaving;
}

// --------------------------------------------------
// TCP送信
// --------------------------------------------------
void sendTCP(bool swing, bool founds[], int rssis[], int num_targets) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, retrying...");
    WiFi.reconnect();
    return;
  }

  WiFiClient client;
  Serial.printf("Connecting to %s:%u...\n", SERVER_IP, SERVER_PORT);
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("TCP connect failed");
    return;
  }
  // JSON配列形式で送信
  //{swing:1,id:1,rssi:-50,found:1}
  for (int i = 0; i < num_targets; i++){
    String json = String("{\"swing\":") + (swing ? 1 : 0)
                        + ",\"id\":";
    if (targetAddresses[i] == "58:8c:81:9d:b8:de"){
      json += 1;
    }
    else if (targetAddresses[i] == "aa:bb:cc:dd:ee:ff"){
      json += 2;
    }
    json += String(",\"rssi\":") + rssis[i]
                 + ",\"found\":" + (founds[i] ? 1 : 0)
                 + "}";
    client.println(json);
  }
  
  Serial.print("Sent: ");
  Serial.println(json);

  unsigned long start = millis();
  String response = "";

  while (millis() - start < 500) {
    if (client.available()) {
      response = client.readStringUntil('\n');
      break;
    }
  }

  int led = response.toInt();
  Serial.printf("Server returned: %d\n", led);

  for(int i = 0;i < 10;i++) {
    if (led == 1) {
      pixels.setPixelColor(i, pixels.Color(0, 255, 0));
    } else if (led == 2) {
      pixels.setPixelColor(i, pixels.Color(255, 255, 0));
    } else if (led == 3){
      pixels.setPixelColor(i, pixels.Color(255, 0, 0));
    }
  }

  if (led == 4) {
    pixels.clear();
  } else if (led == 5) {
    flag = !flag;
  } else if (led != 0 && led != 1 && led != 2){
    Serial.printf("Failed to parse JSON\n");
  }

  pixels.show();

  client.stop();
}

// --------------------------------------------------
// setup()
// --------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  pixels.begin();

  if (!accel.begin()) {
    Serial.println("ADXL345 not detected!");
    while (1);
  }
  accel.setRange(ADXL345_RANGE_16_G);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Connecting WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  BLEDevice::init("");
  scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallback(), true); 
  scan->setActiveScan(true);
  scan->setInterval(90);
  scan->setWindow(45);

  xTaskCreatePinnedToCore(
      bleScanTask,
      "BLEScanTask",
      4096,
      NULL,
      1,
      NULL,
      0
  );
}

// --------------------------------------------------
// loop()
// --------------------------------------------------
void loop() {
  CheckMotion();

  if (isWaving || flag == true) {
    sendTCP(isWaving, foundTarget, lastRSSI, NUM_TARGETS);
    Serial.println(flag);
    delay(250);
  }

  delay(10);
}
