#include "mqtt_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "lcd_manager.h"

MQTTManager MQTT;

static MQTTManager *g_mqttInstance = nullptr;

static bool isEmptyStr(const char *s)
{
    return (s == nullptr) || (s[0] == '\0');
}

void MQTTManager::begin()
{
    auto &cfg = Config.get();
    client.setServer(cfg.host, cfg.port);

    Serial.println("[MQTT] begin() host=" + String(cfg.host) + " port=" + String(cfg.port));

    g_mqttInstance = this;
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
}

bool MQTTManager::publishLog(String payload)
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
    return ok;
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
    if (isEmptyStr(cfg.password))
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

    if (!brokerReachable(1500))
    {
        Serial.println("[MQTT] Broker not reachable");
        return false;
    }

    auto &cfg = Config.get();
    String clientId = String(cfg.iddev);

    Serial.println("[MQTT] Connecting as " + clientId);
    bool ok = client.connect(clientId.c_str());
    if (!ok)
    {
        Serial.println("[MQTT] Connect failed, state=" + String(client.state()));
        return false;
    }

    Serial.println("[MQTT] Connected");

    if (!isEmptyStr(cfg.topic_subscribe))
    {
        Serial.println("[MQTT] Subscribing " + String(cfg.topic_subscribe));
        client.subscribe(cfg.topic_subscribe);
    }
    else
    {
        Serial.println("[MQTT] topic_subscribe empty");
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
        Serial.println("[MQTT] Ignore command for device=" + device);
        return false;
    }

    String cmd = doc["topic"] | "";
    String data = doc["data"] | "";

    Serial.println("[MQTT] Command cmd=" + cmd);
    if (cmd == "reset")
    {
        SD.remove("/config.bin");
        Buzzer.reset();
        delay(3000);
        ESP.restart();
    }

    return false;
}