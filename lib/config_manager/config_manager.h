#pragma once
#include <Arduino.h>

#pragma pack(push, 1)

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

    char iddev[50];

    int offsetday; // default 6

    char topic_subscribe[64];
    char topic_publish[64];

    bool is_ready;
    char no_sample[32];
    char shift[8];
};

#pragma pack(pop)

static_assert(sizeof(DeviceConfig) == 404, "DeviceConfig layout must stay packed for config.bin compatibility");

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
