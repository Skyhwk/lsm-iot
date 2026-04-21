// Sound Level Meter ESP32
// LCD 500ms | SD & Spreadsheet 1 detik
// Revisi Final 4/3/2026

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// KONFIGURASI
const char* ssid = "MAN AP";
const char* password = "intiLab68";
const char* scriptUrl = "https://script.google.com/macros/s/AKfycbzxpmlEefn3JZNzoWVYsaeZrgE6cXecYKivTJL4zHGaVAECF19jVCthtBTBhxdmJUQ/exec";

// NTP Server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // GMT+7
const int daylightOffset_sec = 0;

#define SoundSensorPin 34
#define CALIB_PIN 35
#define VREF 3.3

//pin SD Card
#define CS_PIN 5
#define MOSI_PIN 19
#define MISO_PIN 23
#define SCK_PIN 18

//interval
const unsigned long lcdInterval = 500;   // LCD tiap 500 ms
const unsigned long logInterval = 1000;  // SD & HTTP tiap 1 detik

//OLED
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// struct data
typedef struct {
  float kebisingan;
  char timestamp[25];
} SoundData;

QueueHandle_t sdQueue;
QueueHandle_t httpQueue;

unsigned long lastLCD = 0;
unsigned long lastLog = 0;
float Kebisingan = 0;

//Fungsi URL Encode
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else if (c == ' ') {
      encoded += "%20";
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

//TASK: Kirim ke Spreadsheet
void httpTask(void* param) {

  SoundData data;
  for (;;) {
    if (xQueueReceive(httpQueue, &data, portMAX_DELAY)) {

      if (WiFi.status() == WL_CONNECTED) {

        HTTPClient http;

        String encodedTime = urlEncode(String(data.timestamp));
        String url = String(scriptUrl)
                     + "?value=" + String(data.kebisingan, 1)
                     + "&time=" + encodedTime;

        http.begin(url);
        int httpCode = http.GET();

        if (httpCode == 200) {
          Serial.println("HTTP OK");
        } else {
          Serial.printf("HTTP Error: %d\n", httpCode);
        }

        http.end();
      }
    }
  }
}

//TASK: Simpan ke SD
void sdTask(void* param) {

  SoundData data;

  for (;;) {
    if (xQueueReceive(sdQueue, &data, portMAX_DELAY)) {

      File file = SD.open("/SoundLog.txt", FILE_APPEND);

      if (file) {
        file.printf("%s, %.1f dBA\n",
                    data.timestamp,
                    data.kebisingan);
        file.close();
      }
    }
  }
}

// SETUP
void setup() {
  Serial.begin(115200);
  u8g2.begin();

  analogSetAttenuation(ADC_11db);  // Stabilkan ADC
  analogSetPinAttenuation(SoundSensorPin, ADC_11db);
  analogSetPinAttenuation(CALIB_PIN, ADC_11db);  //calib trimpot

  //WiFi
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "Inisialisasi...");
  u8g2.sendBuffer();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Menghubungkan WiFi...");
  }
  Serial.println("WiFi Terhubung!");
  u8g2.clearBuffer();
  u8g2.drawStr(0, 20, "WiFi Terhubung!");
  u8g2.sendBuffer();
  delay(1000);

  // Pin Trimpot
  pinMode(CALIB_PIN, INPUT);
  analogSetPinAttenuation(SoundSensorPin, ADC_11db);  //satu
  analogSetPinAttenuation(CALIB_PIN, ADC_11db);       //duaa

  //NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Gagal sinkron NTP");
  } else {
    Serial.println("Waktu berhasil sinkron");
  }
  //SD Card
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);

  if (!SD.begin(CS_PIN)) {
    Serial.println("SD card gagal.");
  } else {
    Serial.println("SD card OK.");
    // cek apakah file sudah ada
    if (!SD.exists("/SoundLog.txt")) {

      File file = SD.open("/SoundLog.txt", FILE_WRITE);

      if (file) {
        file.println("=== Log Level Suara (dBA) dengan Timestamp ===");
        file.close();
        Serial.println("File log baru dibuat.");
      }

    } else {
      Serial.println("File log sudah ada, melanjutkan logging.");
    }
  }
  //Queue
  httpQueue = xQueueCreate(10, sizeof(SoundData));
  sdQueue = xQueueCreate(10, sizeof(SoundData));

  xTaskCreatePinnedToCore(httpTask, "HTTP Task", 10000, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(sdTask, "SD Task", 6000, NULL, 1, NULL, 1);
}
//LOOP

void loop() {

  // ===== UPDATE LCD tiap 500 ms =====
  if (millis() - lastLCD >= lcdInterval) {
    lastLCD = millis();

    //RMS SAMPLING
    float sumSq = 0;
    int samples = 600;

    for (int i = 0; i < samples; i++) {
      float val = analogRead(SoundSensorPin);
      sumSq += val * val;
      delayMicroseconds(125);
    }

    //Konversi ke Voltage
    float rms = sqrt(sumSq / samples);
    float voltageValue = rms / 4095.0 * VREF;
    Kebisingan = 50.20 * voltageValue + 10.1;

    // TRIMPOT KALIBRASI (dengan averaging)
    int sumCal = 0;
    for (int i = 0; i < 5; i++) {
      sumCal += analogRead(CALIB_PIN);
      delay(2);
    }
    float calibAdjust = ((sumCal / 5.0) / 4095.0 - 0.5) * 20.0;
    Kebisingan = Kebisingan + calibAdjust;
    //Batas pembacaan sensor
    Kebisingan = constrain(Kebisingan, 30.0, 130.0);

    //OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14_tr);
    u8g2.drawStr(0, 12, "Noise");

    u8g2.setFont(u8g2_font_ncenB18_tr);
    char noiseStr[16];
    snprintf(noiseStr, sizeof(noiseStr), "%.1f", Kebisingan);

    int noiseWidth = u8g2.getStrWidth(noiseStr);
    int xNoise = (128 - noiseWidth) / 2;

    u8g2.drawStr(xNoise, 36, noiseStr);
    u8g2.setFont(u8g2_font_7x14_tr);
    u8g2.drawStr(xNoise + noiseWidth + 4, 54, "dBA");
    
    u8g2.sendBuffer();
  }

  if (millis() - lastLog >= logInterval) {

    lastLog = millis();

    // ==== Timestamp ====
    SoundData data;
    data.kebisingan = Kebisingan;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      strftime(data.timestamp, sizeof(data.timestamp),
               "%Y/%m/%d %H:%M:%S", &timeinfo);
    } else {
      strcpy(data.timestamp, "NO_TIME");
    }
    // ==== Kirim ke Queue ====
    xQueueSend(sdQueue, &data, 0);
    xQueueSend(httpQueue, &data, 0);

    Serial.printf("NO_TIME | Level: %.1f dBA\n", Kebisingan);
  }
}