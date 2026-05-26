#include "mqtt_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <freertos/semphr.h>
#include "lcd_manager.h"

MQTTManager MQTT;
volatile unsigned long MQTTSessionResetVersion = 0;

static MQTTManager *g_mqttInstance = nullptr;
static const char *pendingLogPath = "/mqtt_pending.txt";
static const char *pendingLogTempPath = "/mqtt_pending.tmp";
static SemaphoreHandle_t pendingLogMutex = nullptr;

static bool isEmptyStr(const char *s)
{
    return (s == nullptr) || (s[0] == '\0');
}

static bool parseTruthy(const String &value)
{
    return value == "true" || value == "1" || value == "on" || value == "start";
}

static String buildActionTopic(const char *deviceId)
{
    if (isEmptyStr(deviceId))
        return "";
    return String("/intilab/iot/act/") + String(deviceId);
}

static void clearSessionCountdown()
{
    if (SPIFFS.exists("/remaining_seconds.txt"))
        SPIFFS.remove("/remaining_seconds.txt");
    if (SPIFFS.exists("/total_shift_seconds.txt"))
        SPIFFS.remove("/total_shift_seconds.txt");
}

void MQTTManager::begin()
{
    auto &cfg = Config.get();
    client.setServer(cfg.host, cfg.port);

    Serial.println("[MQTT] begin() host=" + String(cfg.host) + " port=" + String(cfg.port));

    g_mqttInstance = this;
    if (pendingLogMutex == nullptr)
        pendingLogMutex = xSemaphoreCreateMutex();

    if (!callbackInstalled)
    {
        client.setCallback(MQTTManager::callbackThunk);
        callbackInstalled = true;
    }
}

void MQTTManager::loop()
{
    if (Wifi.justConnected())
        onWifiConnected();

    if (!connectIfNeeded())
        return;

    client.loop();
    flushQueuedLogs();
}

bool MQTTManager::publishLog(String payload)
{
    return queueLog(payload);
}

bool MQTTManager::publishLogNow(const String &payload)
{
    if (!isConnected())
        return false;

    auto &cfg = Config.get();
    String topic = cfg.topic_publish;
    if (topic.isEmpty())
    {
        Serial.println("[MQTT] topic_publish empty");
        return false;
    }

    bool ok = client.publish(topic.c_str(), payload.c_str());
    if (ok)
        Serial.println("[MQTT] Session payload published topic=" + topic);
    return ok;
}

bool MQTTManager::queueLog(String payload)
{
    payload.trim();
    if (payload.length() == 0)
        return false;

    if (pendingLogMutex != nullptr)
        xSemaphoreTake(pendingLogMutex, portMAX_DELAY);

    File f = SD.open(pendingLogPath, FILE_APPEND);
    if (!f)
    {
        if (pendingLogMutex != nullptr)
            xSemaphoreGive(pendingLogMutex);
        Serial.println("[MQTT] Failed to queue session payload");
        return false;
    }

    f.println(payload);
    f.close();

    if (pendingLogMutex != nullptr)
        xSemaphoreGive(pendingLogMutex);

    Serial.println("[MQTT] Session payload queued");
    return true;
}

bool MQTTManager::publish(const char *topic, const char *payload)
{
    if (!isConnected())
        return false;
    if (topic == nullptr || topic[0] == '\0')
        return false;
    if (payload == nullptr)
        return false;

    Serial.println("[MQTT] publish topic=" + String(topic));
    return client.publish(topic, payload);
}

bool MQTTManager::isConnected()
{
    return client.connected();
}

bool MQTTManager::wifiConfigValid() const
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.ssid))
        return false;
    return true;
}

bool MQTTManager::networkReady() const
{
    return Wifi.isConnected();
}

bool MQTTManager::brokerReachable(uint32_t timeoutMs)
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.host) || cfg.port <= 0)
        return false;

    WiFiClient probe;
    probe.setTimeout(timeoutMs / 1000);
    bool ok = probe.connect(cfg.host, (uint16_t)cfg.port);
    probe.stop();
    return ok;
}

void MQTTManager::onWifiConnected()
{
    lastConnectAttemptMs = 0;
}

void MQTTManager::flushQueuedLogs()
{
    if (!isConnected() || !SD.exists(pendingLogPath))
        return;

    if (pendingLogMutex != nullptr)
        xSemaphoreTake(pendingLogMutex, portMAX_DELAY);

    File pending = SD.open(pendingLogPath, FILE_READ);
    if (!pending)
    {
        if (pendingLogMutex != nullptr)
            xSemaphoreGive(pendingLogMutex);
        return;
    }

    if (SD.exists(pendingLogTempPath))
        SD.remove(pendingLogTempPath);

    File remaining = SD.open(pendingLogTempPath, FILE_WRITE);
    if (!remaining)
    {
        pending.close();
        if (pendingLogMutex != nullptr)
            xSemaphoreGive(pendingLogMutex);
        Serial.println("[MQTT] Failed to open pending temp file");
        return;
    }

    uint32_t sent = 0;
    uint32_t kept = 0;
    bool publishFailed = false;

    while (pending.available())
    {
        String payload = pending.readStringUntil('\n');
        payload.trim();
        if (payload.length() == 0)
            continue;

        if (!publishFailed && publishLogNow(payload))
        {
            sent++;
            client.loop();
            delay(10);
            continue;
        }

        publishFailed = true;
        remaining.println(payload);
        kept++;
    }

    pending.close();
    remaining.close();

    SD.remove(pendingLogPath);
    if (kept > 0)
        SD.rename(pendingLogTempPath, pendingLogPath);
    else if (SD.exists(pendingLogTempPath))
        SD.remove(pendingLogTempPath);

    if (pendingLogMutex != nullptr)
        xSemaphoreGive(pendingLogMutex);

    if (sent > 0 || kept > 0)
    {
        Serial.println("[MQTT] Pending session sent=" + String(sent) + " remaining=" + String(kept));
    }
}

bool MQTTManager::connectIfNeeded()
{
    if (!callbackInstalled)
    {
        client.setCallback(MQTTManager::callbackThunk);
        callbackInstalled = true;
        g_mqttInstance = this;
    }

    if (!wifiConfigValid())
    {
        // Serial.println("[MQTT] Skip connect: ssid/password empty");
        if (client.connected())
            client.disconnect();
        return false;
    }

    if (!networkReady())
    {
        // Serial.println("[MQTT] Skip connect: WiFi not connected");
        if (client.connected())
            client.disconnect();
        return false;
    }

    if (client.connected())
        return true;

    unsigned long now = millis();
    if (now - lastConnectAttemptMs < connectIntervalMs)
        return false;
    lastConnectAttemptMs = now;

    if (!brokerReachable(5000))
    {
        Serial.println("[MQTT] Broker not reachable");
        return false;
    }

    auto &cfg = Config.get();
    String clientId = String(cfg.iddev);

    Serial.println("[MQTT] clientId='" + clientId + "' len=" + String(clientId.length()));
    Serial.println("[MQTT] Connecting as " + clientId);
    bool ok = client.connect(clientId.c_str());
    if (!ok)
    {
        Serial.println("[MQTT] Connect failed, state=" + String(client.state()));
        return false;
    }

    Serial.println("[MQTT] Connected");

    String actionTopic = buildActionTopic(cfg.iddev);
    if (!actionTopic.isEmpty())
    {
        Serial.println("[MQTT] Subscribing action topic " + actionTopic);
        client.subscribe(actionTopic.c_str());
    }
    else
    {
        Serial.println("[MQTT] action topic empty");
    }

    String legacyTopic = String(cfg.topic_subscribe);
    if (!legacyTopic.isEmpty() && legacyTopic != actionTopic)
    {
        Serial.println("[MQTT] Subscribing legacy topic " + legacyTopic);
        client.subscribe(legacyTopic.c_str());
    }
    else
    {
        Serial.println("[MQTT] legacy topic_subscribe skipped");
    }

    return true;
}

void MQTTManager::callbackThunk(char *topic, byte *payload, unsigned int length)
{
    if (!g_mqttInstance)
        return;
    g_mqttInstance->handleMessage(topic, payload, length);
}

void MQTTManager::handleMessage(const char *topic, const byte *payload, unsigned int length)
{
    Serial.println("[MQTT] Message arrived topic=" + String(topic) + " len=" + String(length));
    String msg;
    msg.reserve(length + 1);
    for (unsigned int i = 0; i < length; i++)
        msg += (char)payload[i];

    Serial.println("[MQTT] Payload: " + msg);

    handleCommandJson(String(topic), msg);
}

bool MQTTManager::handleCommandJson(const String &topic, const String &message)
{
    auto &cfg = Config.get();
    if (isEmptyStr(cfg.iddev))
    {
        Serial.println("[MQTT] Ignore command: iddev empty");
        return false;
    }

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, message);
    if (err)
    {
        Serial.println("[MQTT] JSON parse failed");
        return false;
    }

    String device = doc["device"] | "";
    if (device != String(cfg.iddev))
    {
        Serial.println("[MQTT] Ignore command for device='" + device + "' expected='" + String(cfg.iddev) + "'");
        return false;
    }

    String cmd = doc["topic"] | "";
    String object = doc["object"] | "";
    String data = doc["data"] | "";

    Serial.println("[MQTT] Command topic='" + topic + "' cmd='" + cmd + "' object='" + object + "' data='" + data + "'");
    if (cmd == "reset")
    {
        SD.remove("/config.bin");
        delay(3000);
        ESP.restart();
    }

    if (cmd != "sound meter")
        return true;

    bool changed = false;

    if (object == "no_sample")
    {
        Serial.println("[MQTT] Processing no_sample payload");
        cfg.is_ready = false;
        clearSessionCountdown();
        MQTTSessionResetVersion++;

        int separatorPos = data.indexOf(',');
        if (separatorPos != -1)
        {
            String sample = data.substring(0, separatorPos);
            String shift = data.substring(separatorPos + 1);

            sample.trim();
            shift.trim();

            strlcpy(cfg.no_sample, sample.c_str(), sizeof(cfg.no_sample));
            strlcpy(cfg.shift, "24", sizeof(cfg.shift));

            Serial.println("[MQTT] Parsed no_sample='" + sample + "' shift='" + shift + "'");
        }
        else
        {
            data.trim();
            strlcpy(cfg.no_sample, data.c_str(), sizeof(cfg.no_sample));
            strlcpy(cfg.shift, "24", sizeof(cfg.shift));
            Serial.println("[MQTT] Parsed no_sample='" + data + "' without shift");
        }

        changed = true;
        Serial.println("[MQTT] no_sample=" + String(cfg.no_sample) + " shift=" + String(cfg.shift));
    }
    else if (object == "start")
    {
        cfg.is_ready = parseTruthy(data);
        changed = true;
        Serial.println("[MQTT] is_ready=" + String(cfg.is_ready ? "true" : "false"));
    }
    else if (object == "stop")
    {
        cfg.is_ready = false;
        cfg.no_sample[0] = '\0';
        strlcpy(cfg.shift, "24", sizeof(cfg.shift));
        clearSessionCountdown();
        MQTTSessionResetVersion++;
        changed = true;
        Serial.println("[MQTT] session stopped, pending queue preserved");
    }

    if (changed)
    {
        if (!Config.save())
            Serial.println("[MQTT] Failed to persist config");
        else
            Serial.println("[MQTT] Config persisted after command");
    }

    return true;
}
