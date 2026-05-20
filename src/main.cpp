#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <math.h>

#include "sdcard.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "lcd_manager.h"
#include "storage_manager.h"
#include "time_global.h"
#include "gravity.h"

// ================= DEVICE STATE =================
enum DeviceState
{
    STATE_BOOT,
    STATE_CONFIG,
    STATE_RUN
};

DeviceState deviceState = STATE_BOOT;

GravityLSM gravitySound;
AsyncWebServer server(80);

// ================= TIMING =================
unsigned long bootTimeMs = 0;
unsigned long lastApplyModeMs = 0;

// ================= TASK HANDLES =================
TaskHandle_t taskHandleMQTT;
TaskHandle_t taskHandleTime;
TaskHandle_t taskHandleLCD;
TaskHandle_t taskHandleSensor;

// ================= SENSOR STATE =================
float latestDBA = 0.0f;
unsigned long lastPublishMs = 0;
unsigned long lastSessionSecondMs = 0;
unsigned long sessionTotalSeconds = 0;
unsigned long sessionRemainingSeconds = 0;
unsigned long lastSessionPayloadSecond = ULONG_MAX;
bool sessionCountdownInitialized = false;
char sessionSampleCache[32] = "";
char sessionShiftCache[8] = "";
bool portalAuthenticated = false;
bool portalStarted = false;
bool spiffsReady = false;

static const unsigned long sensorReadIntervalMs = 300;
static const unsigned long sensorPublishIntervalMs = 1000;
static const char *portalSsid = "Device Intilab";
static const char *portalPassword = "intiLab6868";
static const char *countdownRemainingPath = "/remaining_seconds.txt";
static const char *countdownTotalPath = "/total_shift_seconds.txt";

static unsigned long shiftToSeconds(const char *shift)
{
    (void)shift;
    return 24UL * 3600UL;
}

static bool sessionConfigured(const DeviceConfig &cfg)
{
    return cfg.no_sample[0] != '\0';
}

static bool sessionActive(const DeviceConfig &cfg)
{
    return sessionConfigured(cfg) && cfg.is_ready;
}

static String sessionShiftLabel()
{
    if (sessionTotalSeconds == 0)
        return "L1";

    unsigned long elapsed = (sessionTotalSeconds > sessionRemainingSeconds) ? (sessionTotalSeconds - sessionRemainingSeconds) : 0;
    unsigned long hourIndex = (elapsed / 3600UL) + 1UL;
    return "L" + String(hourIndex);
}

static unsigned long sessionElapsedSeconds()
{
    if (sessionTotalSeconds > sessionRemainingSeconds)
        return sessionTotalSeconds - sessionRemainingSeconds;
    return 0;
}

static bool shouldSendSessionPayload()
{
    if (!sessionCountdownInitialized || sessionTotalSeconds == 0)
        return false;

    unsigned long elapsed = sessionElapsedSeconds();
    unsigned long secondInHour = elapsed % 3600UL;
    if (secondInHour >= 600UL)
        return false;
    if ((secondInHour % 5UL) != 0)
        return false;
    if (lastSessionPayloadSecond == elapsed)
        return false;

    lastSessionPayloadSecond = elapsed;
    return true;
}

static void resetSessionState()
{
    lastSessionSecondMs = 0;
    sessionTotalSeconds = 0;
    sessionRemainingSeconds = 0;
    lastSessionPayloadSecond = ULONG_MAX;
    sessionCountdownInitialized = false;
    sessionSampleCache[0] = '\0';
    sessionShiftCache[0] = '\0';
}

static void writeTextFile(const char *path, const String &value)
{
    if (!spiffsReady)
    {
        Serial.println("[SPIFFS] Skip write, filesystem not ready: " + String(path));
        return;
    }

    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f)
    {
        Serial.println("[SPIFFS] Failed write " + String(path));
        return;
    }
    f.print(value);
    f.close();
}

static unsigned long readULongFile(const char *path)
{
    if (!spiffsReady)
        return 0;

    if (!SPIFFS.exists(path))
        return 0;

    File f = SPIFFS.open(path, FILE_READ);
    if (!f)
        return 0;

    String raw = f.readStringUntil('\n');
    f.close();
    raw.trim();
    return strtoul(raw.c_str(), nullptr, 10);
}

static void saveSessionCountdown()
{
    writeTextFile(countdownRemainingPath, String(sessionRemainingSeconds));
    writeTextFile(countdownTotalPath, String(sessionTotalSeconds));
}

static void clearSessionCountdown()
{
    if (!spiffsReady)
        return;

    if (SPIFFS.exists(countdownRemainingPath))
        SPIFFS.remove(countdownRemainingPath);
    if (SPIFFS.exists(countdownTotalPath))
        SPIFFS.remove(countdownTotalPath);
}

static void loadSessionCountdown()
{
    sessionRemainingSeconds = readULongFile(countdownRemainingPath);
    sessionTotalSeconds = readULongFile(countdownTotalPath);
}

static void stopMeasurementSession(bool clearSample)
{
    auto &cfg = Config.get();
    cfg.is_ready = false;
    if (clearSample)
        cfg.no_sample[0] = '\0';
    strlcpy(cfg.shift, "24", sizeof(cfg.shift));
    if (!Config.save())
        Serial.println("[Session] Failed to save stop state");
    clearSessionCountdown();
    resetSessionState();
}

static void ensureSessionCountdown(DeviceConfig &cfg)
{
    if (strcmp(sessionSampleCache, cfg.no_sample) != 0 || strcmp(sessionShiftCache, cfg.shift) != 0)
    {
        sessionCountdownInitialized = false;
        sessionTotalSeconds = 0;
        sessionRemainingSeconds = 0;
    }

    if (sessionCountdownInitialized)
        return;

    loadSessionCountdown();
    unsigned long configuredTotal = shiftToSeconds(cfg.shift);
    if (sessionTotalSeconds != configuredTotal)
        sessionTotalSeconds = configuredTotal;

    if (sessionRemainingSeconds == 0 || sessionRemainingSeconds > sessionTotalSeconds)
        sessionRemainingSeconds = sessionTotalSeconds;

    sessionCountdownInitialized = true;
    lastSessionSecondMs = millis();
    strlcpy(sessionSampleCache, cfg.no_sample, sizeof(sessionSampleCache));
    strlcpy(sessionShiftCache, cfg.shift, sizeof(sessionShiftCache));
    saveSessionCountdown();

    Serial.println("[Session] no_sample=" + String(cfg.no_sample));
    Serial.println("[Session] shift=" + String(cfg.shift));
    Serial.println("[Session] totalSeconds=" + String(sessionTotalSeconds));
    Serial.println("[Session] remainingSeconds=" + String(sessionRemainingSeconds));
}

static bool requestAuthenticated(AsyncWebServerRequest *request)
{
    if (portalAuthenticated)
        return true;

    request->send(401, "text/plain", "Unauthorized");
    return false;
}

static String boolText(bool value)
{
    return value ? "true" : "false";
}

static void handleConfigSave(AsyncWebServerRequest *request)
{
    auto &cfg = Config.get();

    int params = request->params();
    for (int i = 0; i < params; i++)
    {
        const AsyncWebParameter *p = request->getParam(i);
        if (!p->isPost())
            continue;

        const String name = p->name();
        const String value = p->value();

        if (name == "ssid")
            strlcpy(cfg.ssid, value.c_str(), sizeof(cfg.ssid));
        else if (name == "password")
            strlcpy(cfg.password, value.c_str(), sizeof(cfg.password));
        else if (name == "dhcp")
            cfg.dhcp = (value == "true");
        else if (name == "ip")
            strlcpy(cfg.ip, value.c_str(), sizeof(cfg.ip));
        else if (name == "gateway")
            strlcpy(cfg.gateway, value.c_str(), sizeof(cfg.gateway));
        else if (name == "subnet")
            strlcpy(cfg.subnet, value.c_str(), sizeof(cfg.subnet));
        else if (name == "host")
            strlcpy(cfg.host, value.c_str(), sizeof(cfg.host));
        else if (name == "port")
            cfg.port = value.toInt();
        else if (name == "topic_subscribe")
            strlcpy(cfg.topic_subscribe, value.c_str(), sizeof(cfg.topic_subscribe));
        else if (name == "topic_publish")
            strlcpy(cfg.topic_publish, value.c_str(), sizeof(cfg.topic_publish));
        else if (name == "iddev")
            strlcpy(cfg.iddev, value.c_str(), sizeof(cfg.iddev));
        else if (name == "offsetday")
            cfg.offsetday = value.toInt();
    }

    Config.setDefaultIfInvalid();
    if (!Config.save())
    {
        request->send(500, "text/plain", "Failed to save config");
        return;
    }

    request->send(200, "application/json", "{\"message\":\"saved\"}");
    delay(1000);
    ESP.restart();
}

void taskMQTT(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            MQTT.loop();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void taskTime(void *pv)
{
    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            Time.update();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void taskLCD(void *pv)
{
    for (;;)
    {
        LCD.update();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void taskSensor(void *pv)
{
    unsigned long lastReadMs = 0;

    for (;;)
    {
        if (deviceState == STATE_RUN)
        {
            unsigned long now = millis();
            auto &cfg = Config.get();

            if (now - lastReadMs >= sensorReadIntervalMs)
            {
                lastReadMs = now;
                latestDBA = gravitySound.readDBA();

                LCD.setInfo1("");
                LCD.setInfo2(String(latestDBA, 1) + " dBA");
                LCD.setFooterText(sessionConfigured(cfg) ? String(cfg.no_sample) : String("NOT CONFIGURE"));

            }

            if (!sessionConfigured(cfg))
            {
                resetSessionState();
            }
            else
            {
                ensureSessionCountdown(cfg);
            }

            if (sessionActive(cfg) && sessionCountdownInitialized && sessionRemainingSeconds == 0)
            {
                Serial.println("[Session] Shift complete");
                stopMeasurementSession(true);
                LCD.showTemp("Selesai", "Waktu Habis", 3000);
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (sessionActive(cfg) && sessionCountdownInitialized && now - lastSessionSecondMs >= 1000)
            {
                lastSessionSecondMs = now;
                if (sessionRemainingSeconds > 0)
                    sessionRemainingSeconds--;
                saveSessionCountdown();
            }

            if (sessionActive(cfg) && Time.isSynced() && shouldSendSessionPayload())
            {
                LogRecord rec = {};
                rec.sequence = 0;
                strncpy(rec.iddev, cfg.iddev, sizeof(rec.iddev) - 1);
                strncpy(rec.no_sampel, cfg.no_sample, sizeof(rec.no_sampel) - 1);
                String noiseStr = String(latestDBA, 1);
                String laeqStr = String(powf(10.0f, latestDBA * 0.1f), 1);
                String shiftLabel = sessionShiftLabel();
                String dt = Time.datetime();
                strncpy(rec.shift, shiftLabel.c_str(), sizeof(rec.shift) - 1);
                strncpy(rec.noise, noiseStr.c_str(), sizeof(rec.noise) - 1);
                strncpy(rec.laeq, laeqStr.c_str(), sizeof(rec.laeq) - 1);
                strncpy(rec.datetime, dt.c_str(), sizeof(rec.datetime) - 1);

                if (!Storage.addLog(rec))
                    Serial.println("[Sensor] Failed to write log");

                String payloadData = String(cfg.no_sample) + "," + dt + "," + shiftLabel + "," + noiseStr + "," + laeqStr;
                StaticJsonDocument<256> wrapped;
                wrapped["topic"] = "sound meter";
                wrapped["device"] = cfg.iddev;
                wrapped["data"] = payloadData;

                String wrappedPayload;
                serializeJson(wrapped, wrappedPayload);
                if (!MQTT.publishLog(wrappedPayload))
                    Serial.println("[Sensor] Failed to queue session payload");
            }

            if (sessionActive(cfg) && Time.isSynced() && now - lastPublishMs >= sensorPublishIntervalMs)
            {
                lastPublishMs = now;

                StaticJsonDocument<256> doc;
                doc["no_sample"] = cfg.no_sample;
                doc["desibel"] = String(latestDBA, 1);
                doc["LAeq"] = String(powf(10.0f, latestDBA * 0.1f), 1);

                String payload;
                serializeJson(doc, payload);

                String realtimeTopic = String("/intilab/iot/sound meter/") + String(cfg.iddev);
                if (!MQTT.publish(realtimeTopic.c_str(), payload.c_str()))
                    Serial.println("[Sensor] Realtime publish skipped/failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ================= INIT FUNCTIONS =================

bool initStorage()
{
    if (!SDCard_init())
        return false;

    spiffsReady = SPIFFS.begin(true);
    if (!spiffsReady)
    {
        Serial.println("[SPIFFS] Mount failed");
        return false;
    }

    if (!Storage.begin())
        return false;

    return true;
}

bool initConfig()
{
    if (!Config.load())
        return false;

    Config.setDefaultIfInvalid();

    return true;
}

void startTasks()
{
    xTaskCreate(taskTime, "time", 4096, NULL, 1, &taskHandleTime);
    xTaskCreate(taskMQTT, "mqtt", 4096, NULL, 1, &taskHandleMQTT);
    xTaskCreate(taskSensor, "sensor", 6144, NULL, 1, &taskHandleSensor);
}

// ================= SETUP =================

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting...");

    LCD.begin();
    Time.begin();

    // Jalankan task LCD lebih awal agar bisa dipakai
    xTaskCreate(taskLCD, "lcd", 4096, NULL, 1, &taskHandleLCD);

    if (!initStorage())
    {
        LCD.setInfo1("SD / Storage Error");
        delay(2000);
        ESP.restart();
    }

    if (!initConfig())
    {
        deviceState = STATE_CONFIG;
        LCD.setInfo1("NOT CONFIGURED");
        LCD.setInfo2("Use Portal Mode");
        return;
    }

    deviceState = STATE_RUN;

    LCD.setDeviceName(String(Config.get().iddev));
    LCD.setFooterText(sessionConfigured(Config.get()) ? String(Config.get().no_sample) : String("NOT CONFIGURE"));
    LCD.setInfo1("Initializing");
    LCD.setInfo2("Sensor / Network");

    gravitySound.begin();

    Wifi.begin();
    MQTT.begin();

    startTasks();

    Serial.println("System Ready");
}

// ================= LOOP =================

void loop()
{
    if (deviceState == STATE_RUN)
    {
        Wifi.handle();
    }

    delay(1000);
}
