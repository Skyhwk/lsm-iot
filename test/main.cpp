#include <Arduino.h>
#include <ArduinoJson.h>
#include "SSD1306.h"
#include <ESPAsyncWebServer.h>
#include <ESP32Time.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include "DBManager.h"

// =========================PIN DEFINITIONS=======================
#define SDA 21
#define SCL 22
// 5, 18, 19, 23 pins are reserved for micro SD (don't use them)
#define SOUND_METER 34

// =========================CONSTANTS=======================
// Sound meter calibration constants
#define PTP_94_MIN 100.0
#define PTP_94_MAX 250.0
#define PTP_114_MIN 990.0
#define PTP_114_MAX 1100.0
#define DB_94 94.0
#define DB_114 114.0
#define DB_MIN 30.0
#define DB_MAX 140.0
#define SAMPLE_WINDOW 125

// Time constants
const char *NTP_SERVER = "pool.ntp.org";
const long GMT_OFFSET_SEC = 3600;
const int DAYLIGHT_OFFSET_SEC = 3600;

// Network configuration
const char *DEFAULT_AP_SSID = "Device Intilab";
const char *DEFAULT_AP_PASSWORD = "intiLab6868";

// =========================GLOBAL VARIABLES=======================
// Database manager
DBManager db("/database.db");

int totalShiftSeconds = 0; // total waktu dalam detik
int remainingSeconds = 0;  // countdown yang berjalan
int saveInterval = 4;      // tiap 5 detik simpan data
int dataSavedThisHour = 0; // hitung jumlah data yang disimpan dalam 1 jam
bool countdownInitialized = false;

// Display variables
String keterangan_atas = "";
String keterangan_bawah = "";
String connection = "Offline";
String Iplocal = "";
String currentTime = "";
String datetime = "";
int counter = 1;

// Device configuration
String no_sample = "";
String is_ready = "";
String shift = "";
String iddev = "";
String pub_topic_connect = "/intilab/iot";
String sub_topic_connect = "/intilab/iot/act/";
float OffsetDay = 1;

String ssid = "";
String pass = "";
String dhcp = "";
String ip = "";
String gateway = "";
String host = "apps.intilab.com";
String port = "";

String tableSensor = "sound_meter";

// Network configuration
IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Sound meter variables
unsigned int signalMax = 0;
unsigned int signalMin = 4095;

// Object instantiation
SSD1306 display(0x3c, SDA, SCL);
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
ESP32Time rtc;
TimerHandle_t clearKeteranganTimer;

// =========================DATABASE FUNCTIONS=======================
void createDatabase()
{
    db.createTable("CREATE TABLE IF NOT EXISTS config_device ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                   "is_ready VARCHAR(25) NULL, "
                   "no_sample VARCHAR(25) NULL, "
                   "ssid VARCHAR(25) NULL, "
                   "pass VARCHAR(25) NULL, "
                   "dhcp VARCHAR(25) NULL, "
                   "ip VARCHAR(25) NULL, "
                   "gateway VARCHAR(25) NULL, "
                   "host VARCHAR(25) NULL, "
                   "port VARCHAR(25) NULL, "
                   "iddev VARCHAR(25) NULL, "
                   "shift VARCHAR(25) NULL, "
                   "pub_topic_connect VARCHAR(25) NULL, "
                   "sub_topic_connect VARCHAR(25) NULL, "
                   "OffsetDay VARCHAR(25) NULL);");
    // Periksa apakah data dengan id=1 sudah ada
    String existingData = db.singleData("SELECT * FROM config_device WHERE id = 1;");
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, existingData);

    // Jika data belum ada, lakukan insert
    if (error || doc.size() == 0)
    {
        db.insertData("INSERT INTO config_device (ssid, pass, dhcp, host, port, OffsetDay) VALUES ('INTILAB', '',  'true', 'apps.intilab.com', '1111', '6');");
    }
}

void loadConfigFromDB()
{
    String data = "{}";
    bool dataLoaded = false;
    int retryCount = 0;
    const int maxRetries = 5;

    while (!dataLoaded && retryCount < maxRetries)
    {
        try
        {
            data = db.singleData("SELECT * FROM config_device WHERE id = 1;");
            if (data != "{}" && data.length() > 2)
            {
                dataLoaded = true;
                Serial.println("Data berhasil dimuat dari database");
            }
            else
            {
                Serial.println("Data kosong, mencoba lagi... (" + String(retryCount + 1) + "/" + String(maxRetries) + ")");
                retryCount++;
                delay(500);
            }
        }
        catch (const std::exception &e)
        {
            Serial.println("Database error: " + String(e.what()) + " - mencoba lagi... (" + String(retryCount + 1) + "/" + String(maxRetries) + ")");
            retryCount++;
            delay(500);
        }
    }

    if (!dataLoaded)
    {
        Serial.println("Gagal memuat data setelah " + String(maxRetries) + " percobaan, menggunakan data default");
    }

    StaticJsonDocument<1024> configJson;
    deserializeJson(configJson, data);

    shift = configJson["shift"].as<String>();
    no_sample = configJson["no_sample"].as<String>();
    iddev = configJson["iddev"].as<String>();
    is_ready = configJson["is_ready"].as<String>();
    String pubTopicFromDb = configJson["pub_topic_connect"].as<String>();
    if (pubTopicFromDb != NULL && pubTopicFromDb != "null" && pubTopicFromDb != "" && pubTopicFromDb.length() > 0)
    {
        pub_topic_connect = pubTopicFromDb;
    }
    String subTopicFromDb = configJson["sub_topic_connect"].as<String>();
    if (subTopicFromDb != NULL && subTopicFromDb != "null" && subTopicFromDb != "" && subTopicFromDb.length() > 0)
    {
        sub_topic_connect = subTopicFromDb;
    }
    ssid = configJson["ssid"].as<String>();
    pass = configJson["pass"].as<String>();
    dhcp = configJson["dhcp"].as<String>();
    ip = configJson["ip"].as<String>();
    gateway = configJson["gateway"].as<String>();
    host = configJson["host"].as<String>();
    port = configJson["port"].as<String>();
    OffsetDay = configJson["OffsetDay"].as<float>();

    Serial.println("data dari database : " + configJson.as<String>());
}

void stopSensor()
{
    is_ready = "false";
    no_sample = "";

    String sql = "UPDATE config_device SET is_ready = '" + is_ready + "', no_sample = '', shift = '1' WHERE id = 1;";
    db.updateData(sql.c_str());
    Serial.println("Set is_ready to: " + is_ready);

    totalShiftSeconds = 0;
    remainingSeconds = 0;
    dataSavedThisHour = 0;

    // Hapus file remaining_seconds.txt
    if (SPIFFS.exists("/remaining_seconds.txt"))
    {
        if (SPIFFS.remove("/remaining_seconds.txt"))
        {
            Serial.println("File remaining_seconds.txt berhasil dihapus");
        }
        else
        {
            Serial.println("Gagal menghapus file remaining_seconds.txt");
        }
    }
    else
    {
        Serial.println("File remaining_seconds.txt tidak ditemukan");
    }
    delay(1000);
    countdownInitialized = false;
}
// =========================DISPLAY FUNCTIONS=======================
void drawProgressBar()
{
    for (int i = 1; i <= 100; i++)
    {
        display.clear();
        display.drawProgressBar(0, 32, 120, 10, i);
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 15, String(i) + "%");
        display.display();
        delay(100);
    }
}

void initLcd()
{
    display.init();
    display.flipScreenVertically();
    drawProgressBar();
}

void clearKeterangan(TimerHandle_t xTimer)
{
    keterangan_atas = "";
    keterangan_bawah = "";
    Serial.println("Clear Keterangan");
}

void setKeterangan(String keteranganAtas, String keteranganBawah)
{
    keterangan_atas = keteranganAtas;
    keterangan_bawah = keteranganBawah;
}

void updateDisplay()
{
    String Date_ = rtc.getTime("%F");
    currentTime = rtc.getTime();
    datetime = Date_ + "T" + currentTime;

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, currentTime);
    display.drawString(85, 0, connection);

    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 15, keterangan_atas);
    display.drawString(0, 30, keterangan_bawah);

    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 53, no_sample);
    display.display();
}

void displayTask(void *pvParameters)
{
    while (true)
    {
        updateDisplay();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// =========================WEBSERVER FUNCTIONS=======================
String processor(const String &var)
{
    // Variabel-variabel konfigurasi
    String cleaned_sub_topic_connect = sub_topic_connect;
    cleaned_sub_topic_connect.replace(iddev, "");

    static const struct
    {
        const char *name;
        const String &value;
    } vars[] = {
        {"is_ready", is_ready},
        {"no_sample", no_sample},
        {"ssid", ssid},
        {"pass", pass},
        {"dhcp", dhcp},
        {"ip", ip},
        {"gateway", gateway},
        {"host", host},
        {"port", port},
        {"iddev", iddev},
        {"connection", connection},
        {"OffsetDay", String(OffsetDay)},
        {"iplocal", Iplocal},
        {"shift", shift},
        {"pub_topic_connect", pub_topic_connect},
        {"sub_topic_connect", cleaned_sub_topic_connect}};

    // Cari variabel yang cocok
    for (const auto &item : vars)
    {
        if (var == item.name)
        {
            return item.value;
        }
    }

    return String();
}

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

void setupServerRoutes()
{

    Serial.println("Setting up server routes");
    // Basic routes
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/index.html", "text/html", false, processor); });

    server.on("/Konfigurasi-Perangkat", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/Konfigurasi-Perangkat.html", "text/html", false, processor); });

    // Save config route
    server.on("/save-config", HTTP_POST, [](AsyncWebServerRequest *request)
              {
        int params = request->params();
        StaticJsonDocument<1024> configJson;
        
        // Collect parameters
        for (int i = 0; i < params; i++) {
            AsyncWebParameter *p = request->getParam(i);
            if (p->isPost()) {
                const String& name = p->name();
                const String& value = p->value();
                
                if (name == "ssid" || name == "pass" || name == "ip" || 
                    name == "gateway" || name == "host" || name == "iddev" || 
                    name == "dhcp" || name == "port" || name == "OffsetDay" || 
                    name == "shift" || name == "pub_topic_connect" || 
                    name == "sub_topic_connect") {
                    configJson[name] = value;
                }
            }
        }
        
        // Build SQL update statement
        String sql = "UPDATE config_device SET";
        sql += " ssid = '" + configJson["ssid"].as<String>() + "',";
        sql += " pass = '" + configJson["pass"].as<String>() + "',";
        sql += " dhcp = '" + configJson["dhcp"].as<String>() + "',";
        sql += " ip = '" + configJson["ip"].as<String>() + "',";
        sql += " gateway = '" + configJson["gateway"].as<String>() + "',";
        sql += " host = '" + configJson["host"].as<String>() + "',";
        sql += " port = '" + configJson["port"].as<String>() + "',";
        sql += " iddev = '" + configJson["iddev"].as<String>() + "',";
        sql += " shift = '" + configJson["shift"].as<String>() + "',";
        sql += " OffsetDay = '" + configJson["OffsetDay"].as<String>() + "',";
        sql += " pub_topic_connect = '" + configJson["pub_topic_connect"].as<String>() + "',";
        sql += " sub_topic_connect = '" + configJson["sub_topic_connect"].as<String>() + "'";
        sql += " WHERE id = 1;";
        
        db.updateData(sql.c_str());
        
        request->send(200, "application/json", "{\"message\":\"data has been saved\"}");
        delay(3000);
        ESP.restart(); });

    // API routes
    server.on("/downloadDB", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        String dbPath = "/database.db";
        String dbFullPath = String("/sd") + String(dbPath);
        
        if (SD.exists(dbFullPath)) {
            File dbFile = SD.open(dbFullPath, FILE_READ);
            if (dbFile) {
                AsyncWebServerResponse *response = request->beginResponse(SD, dbFullPath, "application/octet-stream");
                response->addHeader("Content-Disposition", "attachment; filename=database.db");
                request->send(response);
                dbFile.close();
            } else {
                request->send(404, "text/plain", "File database tidak dapat dibuka");
            }
        } else {
            request->send(404, "text/plain", "File database tidak ditemukan");
        } });

    server.on("/getDataOffline", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        String sql = "SELECT * FROM " + tableSensor;
        String jsonData = db.selectData(sql.c_str());
        // db.close();
        request->send(200, "application/json", jsonData); });

    server.on("/downloadDataOffline", HTTP_GET, [](AsyncWebServerRequest *request)
              {                  
        String sql = "SELECT * FROM " + tableSensor;
        String jsonData = db.selectData(sql.c_str());
        
        // String csvFilePath = "/data_offline.csv";
        String csvFilePath = "/sd/data_offline.csv";
        
        // Hapus file lama jika ada
        if (SD.exists(csvFilePath)) {
            SD.remove(csvFilePath);
        }
        File file = SD.open(csvFilePath, FILE_WRITE);
        if (file) {
            DynamicJsonDocument doc(10240);
            deserializeJson(doc, jsonData);
            JsonArray array = doc.as<JsonArray>();
            
            if (array.size() > 0) {
                JsonObject firstObj = array[0];
                bool firstCol = true;
                for (JsonPair kv : firstObj) {
                    if (!firstCol) file.print(",");
                    file.print(kv.key().c_str());
                    firstCol = false;
                }
                file.println();
                
                for (JsonObject obj : array) {
                    bool firstCol = true;
                    for (JsonPair kv : obj) {
                        if (!firstCol) file.print(",");
                        file.print(kv.value().as<String>());
                        firstCol = false;
                    }
                    file.println();
                }
            }
            file.close();
            
            AsyncWebServerResponse *response = request->beginResponse(SD, csvFilePath, "text/csv");
            response->addHeader("Content-Disposition", "attachment; filename=data_offline.csv");
            request->send(response);
        } else {
            request->send(401, "text/plain", "Gagal membuat file CSV di SD Card");
        } });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              { ESP.restart(); });

    server.onNotFound(notFound);
    server.begin();
    Serial.println("Server routes set up successfully");
}

// =========================NETWORK FUNCTIONS=======================
void startWiFiManager()
{
    if (!WiFi.softAPgetStationNum())
    {
        if (WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD))
        {
            Serial.println("SoftAP started successfully with IP address: " + WiFi.softAPIP().toString());
        }
        setupServerRoutes();
    }
    else
    {
        Serial.println("SoftAP is already running");
    }
}

bool connectToWiFi(String ssid, String password, String useDHCP, String staticIP = "", String gateway = "")
{
    if (useDHCP == "true")
    {
        Serial.println("Connecting to " + ssid + " using DHCP");
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    else
    {
        Serial.println("Connecting to " + ssid + " using static IP");
        localIP.fromString(staticIP);
        localGateway.fromString(gateway);

        if (!WiFi.config(localIP, localGateway, subnet, primaryDNS, secondaryDNS))
        {
            Serial.println("STA Failed to configure");
            return false;
        }
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000)
    {
        delay(100);
    }

    return WiFi.status() == WL_CONNECTED;
}

void taskWiFi(void *parameter)
{
    Serial.println("Task WiFi started");

    connectToWiFi(ssid, pass, dhcp, ip, gateway);

    setupServerRoutes();

    while (true)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            connection = "Wifi Loss";
            delay(5000);

            // Try to reconnect
            connectToWiFi(ssid, pass, dhcp, ip, gateway);
        }
        else if (connection != "Online")
        {
            connection = "Online";
            Serial.print("Connected to WiFi network with IP Address: ");
            Serial.println(WiFi.localIP());
            Iplocal = WiFi.localIP().toString();

            // Sync time with NTP server
            configTime(GMT_OFFSET_SEC * OffsetDay, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
            struct tm timeinfo;
            if (getLocalTime(&timeinfo))
            {
                rtc.setTimeStruct(timeinfo);
            }
        }
        delay(1000);
    }
}

// =========================MQTT FUNCTIONS=======================
void callback(char *topic, byte *payload, unsigned int length)
{
    // Convert payload to string
    String message;
    for (unsigned int i = 0; i < length; i++)
    {
        message += (char)payload[i];
    }

    Serial.println("Received topic: " + String(topic));
    Serial.println("Expected topic: " + sub_topic_connect);
    Serial.println("Message: " + message);

    if (String(topic) == sub_topic_connect)
    {
        Serial.println("Topic matched! Processing message...");
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, message);

        if (error)
        {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        String receivedTopic = doc["topic"];
        String device = doc["device"];
        String object = doc["object"];
        String data = doc["data"];

        Serial.println("receivedTopic: " + receivedTopic);
        Serial.println("device: " + device);
        Serial.println("iddev: " + iddev);
        Serial.println("object: " + object);
        Serial.println("data: " + data);

        if (iddev == device && receivedTopic == "sound meter")
        {
            Serial.println("Device and topic match, processing command...");
            String sql;

            if (object == "no_sample")
            {

                Serial.println("Memproses data: " + data);
                // Memecah data untuk mendapatkan shift dan no_sample
                int separatorPos = data.indexOf(',');
                if (separatorPos != -1)
                {
                    no_sample = data.substring(0, separatorPos);
                    shift = data.substring(separatorPos + 1);
                    Serial.println("Set no_sample to: " + no_sample);
                    Serial.println("Set shift to: " + shift);
                }
                else
                {
                    Serial.println("Format data tidak valid, menggunakan data sebagai no_sample");
                    no_sample = data;
                }
                sql = "UPDATE config_device SET no_sample = '" + no_sample + "', shift = '" + shift + "' WHERE id = 1;";
                db.updateData(sql.c_str());
                Serial.println("Set no_sample to: " + no_sample);
            }
            else if (object == "start")
            {
                is_ready = data;
                sql = "UPDATE config_device SET is_ready = '" + is_ready + "' WHERE id = 1;";
                db.updateData(sql.c_str());
                Serial.println("Set is_ready to: " + is_ready);
            }
            else if (object == "stop")
            {
                stopSensor();
            }
        }
        else
        {
            Serial.println("Device or topic doesn't match");
        }
    }
    else
    {
        Serial.println("Topic doesn't match with expected");
    }
}

void taskMQTT(void *parameter)
{
    client.setServer(host.c_str(), port.toInt());
    client.setCallback(callback);

    while (true)
    {
        if (WiFi.status() == WL_CONNECTED) // Hanya jalankan ketika online
        {
            if (!client.connected())
            {
                if (client.connect(iddev.c_str()))
                {
                    Serial.println("Connected to MQTT broker");
                    setKeterangan("Connected", "to server");
                    xTimerStart(clearKeteranganTimer, 0);

                    String subscribe_topic = sub_topic_connect;
                    Serial.println("Trying to subscribe to: " + subscribe_topic);

                    if (client.subscribe(subscribe_topic.c_str()))
                    {
                        Serial.println("Subscription successful");
                    }
                    else
                    {
                        Serial.println("Subscription failed");
                    }
                }
                else
                {
                    Serial.print("Failed to connect to MQTT broker, rc=");
                    Serial.println(client.state());

                    if (no_sample != "")
                    {
                        setKeterangan("Can't", "Connect Server");
                    }
                    else
                    {
                        setKeterangan("Device", "Not Configured.!");
                    }
                }
            }
            else
            {
                client.loop();
            }
        }
        else
        {
            setKeterangan("WiFi", "Tidak Terhubung");
        }
        delay(1000);
    }
}

void sendData(const char *payload)
{
    if (strlen(payload) == 0)
    {
        Serial.println("No payload to send.");
        return;
    }

    String jsonPayload = "{\"topic\": \"sound meter\", \"device\": \"" + iddev + "\", \"data\": \"";
    jsonPayload += payload;
    jsonPayload += "\"}";

    if (client.connected())
    {
        client.publish(pub_topic_connect.c_str(), jsonPayload.c_str());
        Serial.print("Published: ");
        Serial.println(jsonPayload);
    }
}

// =========================SHIFT FUNCTIONS=======================
void saveCountdownToSPIFFS()
{
    if (!SPIFFS.exists("/remaining_seconds.txt"))
    {
        Serial.println("File remaining_seconds.txt tidak ada, membuat file baru");
    }

    if (!SPIFFS.exists("/data_per_hour.txt"))
    {
        Serial.println("File data_per_hour.txt tidak ada, membuat file baru");
    }

    if (!SPIFFS.exists("/total_shift_seconds.txt"))
    {
        Serial.println("File total_shift_seconds.txt tidak ada, membuat file baru");
    }

    File file = SPIFFS.open("/remaining_seconds.txt", "w");
    file.println(String(remainingSeconds));
    file.close();

    File file2 = SPIFFS.open("/data_per_hour.txt", "w");
    file2.println(String(dataSavedThisHour));
    file2.close();

    File file3 = SPIFFS.open("/total_shift_seconds.txt", "w");
    file3.println(String(totalShiftSeconds));
    file3.close();
}

void loadCountdownFromSPIFFS()
{
    if (SPIFFS.exists("/remaining_seconds.txt"))
    {
        File file = SPIFFS.open("/remaining_seconds.txt", "r");
        String val = file.readStringUntil('\n');
        remainingSeconds = val.toInt();
        file.close();
    }

    if (SPIFFS.exists("/total_shift_seconds.txt"))
    {
        File file = SPIFFS.open("/total_shift_seconds.txt", "r");
        String val = file.readStringUntil('\n');
        totalShiftSeconds = val.toInt();
        file.close();
    }

    if (SPIFFS.exists("/data_per_hour.txt"))
    {
        File file = SPIFFS.open("/data_per_hour.txt", "r");
        String val = file.readStringUntil('\n');
        dataSavedThisHour = val.toInt();
        file.close();
    }
}

void initCountdown()
{
    Serial.println("shift: " + shift);
    if (shift == "1")
        totalShiftSeconds = 1 * 3600;
    else if (shift == "8")
        totalShiftSeconds = 8 * 3600;
    else if (shift == "24")
        totalShiftSeconds = 24 * 3600;
    else
        totalShiftSeconds = 0;

    Serial.println("totalShiftSeconds: " + String(totalShiftSeconds));

    loadCountdownFromSPIFFS();

    Serial.println("remainingSeconds after load: " + String(remainingSeconds));

    if (remainingSeconds <= 0 || remainingSeconds > totalShiftSeconds)
    {
        remainingSeconds = totalShiftSeconds;
        saveCountdownToSPIFFS();
    }

    countdownInitialized = true;
}

// =========================SENSOR FUNCTIONS=======================
void initSensor()
{
    pinMode(SOUND_METER, INPUT);
    Serial.println("Sound meter sensor initialized on pin 34");
}

void saveOfflineData(const char *payloadData)
{
    String payloadStr = String(payloadData);
    int firstComma = payloadStr.indexOf(',');
    int secondComma = payloadStr.indexOf(',', firstComma + 1);
    int thirdComma = payloadStr.indexOf(',', secondComma + 1);
    int fourthComma = payloadStr.indexOf(',', thirdComma + 1);

    String sampleNo = payloadStr.substring(0, firstComma);
    String datetime = payloadStr.substring(firstComma + 1, secondComma);
    String shiftValue = payloadStr.substring(secondComma + 1, thirdComma);
    String dbValue = payloadStr.substring(thirdComma + 1, fourthComma);
    String LAeqValue = payloadStr.substring(fourthComma + 1);

    db.createTable("CREATE TABLE IF NOT EXISTS sound_meter ("
                   "id INTEGER PRIMARY KEY, "
                   "no_sampel VARCHAR(25), "
                   "datetime VARCHAR(25), "
                   "shift VARCHAR(25), "
                   "db REAL, "
                   "LAeq REAL);");

    String sql = "INSERT INTO sound_meter (no_sampel, datetime, shift, db, LAeq) VALUES ('" +
                 sampleNo + "', '" +
                 datetime + "', '" +
                 shiftValue + "', " +
                 dbValue + ", " +
                 LAeqValue + ");";

    db.insertData(sql.c_str());

    // Send data to MQTT server
    sendData(payloadData);
}

void readSensor()
{
    static unsigned long lastSecond = 0;
    if (millis() - lastSecond < 1000)
        return; // run every 1s
    lastSecond = millis();

    if (remainingSeconds <= 0)
    {
        Serial.println("Countdown selesai. Sensor dimatikan.");
        stopSensor();
        return;
    }

    // --- Baca sensor dan tampilkan ke LCD setiap detik (RMS + kalibrasi vrms -> dB)
    unsigned long startMillis = millis();
    uint64_t sumOfSquares = 0;
    unsigned int sampleCount = 0;
    static int midpoint = 2048;

    while (millis() - startMillis < SAMPLE_WINDOW)
    {
        int adc = analogRead(SOUND_METER);
        int centered = adc - midpoint;
        sumOfSquares += (int64_t)centered * (int64_t)centered;
        sampleCount++;
    }

    float meanSquare = (float)sumOfSquares / max(1u, sampleCount);
    float rmsAdc = sqrt(meanSquare);
    float vrmsValue = rmsAdc * (3.3f / 4095.0f); // VREF lokal

    float db = 0.0f;
    if (vrmsValue >= 0.001f && vrmsValue < 0.056f)
        db = 827.3f * vrmsValue + 22.76f;
    else if (vrmsValue >= 0.056f && vrmsValue < 0.103f)
        db = -1462.0f * vrmsValue * vrmsValue + 350.0f * vrmsValue + 54.08f;
    else if (vrmsValue >= 0.103f && vrmsValue < 0.197f)
        db = -397.8f * vrmsValue * vrmsValue + 175.3f * vrmsValue + 60.79f;
    else if (vrmsValue >= 0.197f && vrmsValue < 0.374f)
        db = -67.71f * vrmsValue * vrmsValue + 74.33f * vrmsValue + 67.87f;
    else if (vrmsValue >= 0.374f && vrmsValue < 0.592f)
        db = 13.39f * vrmsValue + 83.19f;
    else if (vrmsValue >= 0.592f && vrmsValue < 1.077f)
        db = 6.350f * vrmsValue + 87.36f;
    else if (vrmsValue >= 1.077f && vrmsValue < 1.468f)
        db = 4.859f * vrmsValue + 87.86f;
    else if (vrmsValue >= 1.468f)
        db = 528.0f * vrmsValue * vrmsValue - 1533.0f * vrmsValue + 1209.0f;

    float LAeq = pow(10.0f, db * 0.1f);

    // setKeterangan("Desibel: " + String(db) + " dB", "LAeq: " + String(LAeq));
    setKeterangan(String(db) + " dB", "");

    String publish_for_android = "/intilab/iot/sound meter/" + iddev;
    String jsonPayload = "{\"no_sample\": \"" + String(no_sample) + "\", \"desibel\": \"" + String(db) + "\", \"LAeq\": \"" + String(LAeq) + "\"}";
    if (client.connected())
    {
        client.publish(publish_for_android.c_str(), jsonPayload.c_str());
    }

    // --- Simpan data setiap 5 detik, max 120 data per jam
    if (remainingSeconds % saveInterval == 0)
    {
        int secondsThisHour = totalShiftSeconds - remainingSeconds;
        if ((secondsThisHour % 3600) == 0)
        {
            dataSavedThisHour = 0;
        }

        if (dataSavedThisHour < 120)
        {
            String currentShift = "L" + String((totalShiftSeconds - remainingSeconds) / 3600 + 1);
            String payload = no_sample + "," + String(datetime) + "," + currentShift + "," + String(db) + "," + String(LAeq);
            saveOfflineData(payload.c_str());
            Serial.println("Simpan data: " + payload);
            dataSavedThisHour++;
        }
        else
        {
            Serial.println("Batas penyimpanan 120 data per jam tercapai.");
            if (remainingSeconds < 3600)
            {
                // baca jika jam terakhir dan sudah di ambil datanya maka pembacaan sensor dihentikan
                Serial.println("Data sudah diambil, sensor dimatikan");
                stopSensor();
            }
        }
    }

    // --- Kurangi countdown dan simpan ke SPIFFS
    remainingSeconds--;
    saveCountdownToSPIFFS();
}

void sensorTask(void *pvParameters)
{
    while (true)
    {
        if (no_sample != "")
        {
            if (!countdownInitialized)
            {
                initCountdown();
            }

            if (is_ready == "true")
            {
                Serial.println("is_ready: " + is_ready + " remainingSeconds: " + remainingSeconds);
                if (remainingSeconds > 0)
                {
                    readSensor(); // sensor baca + countdown
                }
                else
                {
                    setKeterangan("Selesai", "Waktu Habis");
                }
            }
            else
            {
                setKeterangan("Ready", "to Start");
            }
        }
        else
        {
            setKeterangan("Device", "Not Configured.!");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// =========================SETUP & LOOP=======================
void setup()
{
    // Initialize serial
    Serial.begin(115200);
    Wire.begin();

    // Initialize database
    if (!db.begin())
    {
        Serial.println("Failed to initialize database");
        ESP.restart();
    }

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS Mount Failed");
        ESP.restart();
    }

    server.serveStatic("/", SPIFFS, "/");

    // Initialize components
    createDatabase();
    loadConfigFromDB();
    delay(1000);
    initLcd();

    // Start WiFi access point
    startWiFiManager();
    initSensor();

    // Create timers
    clearKeteranganTimer = xTimerCreate(
        "Clear Keterangan Timer",
        pdMS_TO_TICKS(3000),
        pdFALSE,
        (void *)0,
        clearKeterangan);

    // Create tasks
    xTaskCreate(displayTask, "displayTask", 4096, NULL, 1, NULL);
    xTaskCreate(taskWiFi, "TaskWiFi", 4096, NULL, 1, NULL);
    xTaskCreate(taskMQTT, "TaskMQTT", 4096, NULL, 1, NULL);
    xTaskCreate(sensorTask, "SensorTask", 4096, NULL, 1, NULL);

    // db.close();
}

void loop()
{
    // Empty - tasks handle all functionality
}