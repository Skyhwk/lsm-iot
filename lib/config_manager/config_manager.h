#pragma once
#include <Arduino.h>

struct DeviceConfig
{
    char ssid[32];     // default "INTILAB"
    char password[32]; // default ""

    bool dhcp;        // default true
    char ip[16];      // empty if dhcp
    char gateway[16]; // empty if dhcp
    char subnet[16];  // empty if dhcp

    char host[64];
    int port;

    char iddev[32];

    int offsetday; // default 6

    char topic_subscribe[64];
    char topic_publish[64];
};

class ConfigManager
{
public:
    bool load();
    bool save();

    DeviceConfig &get();
    void setDefaultIfInvalid();

private:
    DeviceConfig config;
};

extern ConfigManager Config;
