#include "wifi_manager.h"
#include "config_manager.h"
#include "lcd_manager.h"

WifiManager Wifi;

bool WifiManager::begin()
{
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    delay(200);

    connect();

    return true;
}

void WifiManager::connect()
{
    auto &cfg = Config.get();
    bool useDhcp = cfg.dhcp;

    Serial.println("=== WIFI CONFIG ===");
    Serial.println("SSID: " + String(cfg.ssid));
    Serial.println("PASSWORD: " + String(cfg.password));
    Serial.println("DHCP: " + String(cfg.dhcp ? "true" : "false"));
    LCD.setInfo1("Connecting...");
    // ===== DHCP / STATIC CONFIG =====
    if (!useDhcp)
    {
        IPAddress ip, gw, sn;

        if (!ip.fromString(cfg.ip) ||
            !gw.fromString(cfg.gateway) ||
            !sn.fromString(cfg.subnet))
        {
            Serial.println("Invalid Static IP Config! Falling back to DHCP");
            useDhcp = true;
        }
        else if (!WiFi.config(ip, gw, sn))
        {
            Serial.println("Failed to apply Static IP, falling back to DHCP");
            useDhcp = true;
        }
        else
        {
            Serial.println("Static IP Applied");
        }
    }

    if (useDhcp)
    {
        // Reset ke DHCP
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
        Serial.println("Using DHCP");
    }

    // ===== START CONNECTION =====
    if (strlen(cfg.password) == 0)
        WiFi.begin(cfg.ssid);
    else
        WiFi.begin(cfg.ssid, cfg.password);
}

bool WifiManager::isConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

bool WifiManager::justConnected()
{
    bool v = _justConnected;
    _justConnected = false;
    return v;
}

void WifiManager::handle()
{
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (connected)
    {
        LCD.setStatus(true);
        LCD.setIP(WiFi.localIP().toString());
        if (!_wasConnected)
        {
            _justConnected = true;
            LCD.setInfo1("");
            LCD.showTemp("Wifi", "Connected", 3000, true);
        }

        _wasConnected = true;
        return;
    }

    _wasConnected = false;

    unsigned long now = millis();

    if (now - lastReconnectAttempt > reconnectInterval)
    {
        lastReconnectAttempt = now;

        Serial.println("WiFi Lost. Reconnecting...");

        WiFi.disconnect(true);
        delay(200);

        connect();
    }
}
