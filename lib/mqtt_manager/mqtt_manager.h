#pragma once
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>

extern volatile unsigned long MQTTSessionResetVersion;

class MQTTManager
{
public:
    void begin();
    void loop();
    bool publishLog(String payload);
    bool queueLog(String payload);

    bool publish(const char *topic, const char *payload);

    bool isConnected();

private:
    bool wifiConfigValid() const;
    bool networkReady() const;
    bool brokerReachable(uint32_t timeoutMs);

    bool connectIfNeeded();
    void onWifiConnected();
    void flushQueuedLogs();
    bool publishLogNow(const String &payload);

    static void callbackThunk(char *topic, byte *payload, unsigned int length);
    void handleMessage(const char *topic, const byte *payload, unsigned int length);

    bool handleCommandJson(const String &topic, const String &message);

    WiFiClient espClient;
    PubSubClient client{espClient};

    unsigned long lastConnectAttemptMs = 0;
    const unsigned long connectIntervalMs = 5000;
    bool callbackInstalled = false;
};

extern MQTTManager MQTT;
