#include "config_manager.h"
#include <SD.h>

ConfigManager Config;

bool ConfigManager::load()
{
    File f = SD.open("/config.bin");
    if (!f)
    {
        Serial.println("[Config] config.bin not found, creating default...");
        memset(&config, 0, sizeof(DeviceConfig));
        setDefaultIfInvalid();
        if (!save())
        {
            Serial.println("[Config] Failed to create default config.bin!");
            return false;
        }
        Serial.println("[Config] Default config.bin created successfully");
        return true;
    }

    memset(&config, 0, sizeof(DeviceConfig));

    size_t sz = f.size();
    size_t toRead = sz;
    if (toRead > sizeof(DeviceConfig))
        toRead = sizeof(DeviceConfig);

    size_t read = f.read((uint8_t *)&config, toRead);
    f.close();

    Serial.println("[Config] Load filesize=" + String(sz) + " read=" + String(read));

    // migration heuristic: older firmware version may have stored iddev where topic_subscribe is now
    if (config.iddev[0] == '\0' && config.topic_subscribe[0] != '\0')
    {
        String ts = String(config.topic_subscribe);
        if (ts.length() > 0 && ts.length() < (int)sizeof(config.iddev) && ts.indexOf('/') < 0)
        {
            strlcpy(config.iddev, ts.c_str(), sizeof(config.iddev));
            strcpy(config.topic_subscribe, "");
        }
    }

    setDefaultIfInvalid();
    Serial.println("[Config] Loaded iddev='" + String(config.iddev) + "' len=" + String(strlen(config.iddev)));
    Serial.println("[Config] Loaded port=" + String(config.port));

    return true;
}

bool ConfigManager::save()
{
    Serial.println("[Config] Saving iddev='" + String(config.iddev) + "' len=" + String(strlen(config.iddev)));
    Serial.println("[Config] Saving port=" + String(config.port));
    SD.remove("/config.bin");
    File f = SD.open("/config.bin", FILE_WRITE);
    if (!f)
        return false;

    size_t written = f.write((uint8_t *)&config, sizeof(DeviceConfig));
    f.flush(); // Pastikan data benar-benar ditulis ke SD card
    f.close();

    Serial.println("[Config] Save written=" + String(written) + " expected=" + String(sizeof(DeviceConfig)));

    return (written == sizeof(DeviceConfig));
}

DeviceConfig &ConfigManager::get()
{
    return config;
}

void ConfigManager::setDefaultIfInvalid()
{
    // SSID
    if (config.ssid[0] == '\0')
        strcpy(config.ssid, "LSM");

    // Password boleh kosong
    if (config.password[0] == '\0')
        strcpy(config.password, "intiLab68");

    // DHCP
    if (!config.dhcp &&
        (config.ip[0] == '\0' || config.gateway[0] == '\0' || config.subnet[0] == '\0'))
    {
        config.dhcp = true;
    }

    if (config.dhcp)
    {
        strcpy(config.ip, "");
        strcpy(config.gateway, "");
        strcpy(config.subnet, "");
    }

    // Host
    if (config.host[0] == '\0')
        strcpy(config.host, "apps.intilab.com");

    // Port
    if (config.port <= 0 || config.port > 65535)
        config.port = 1111;

    // Device ID
    if (config.iddev[0] == '\0')
    {
        uint32_t chip = static_cast<uint32_t>(ESP.getEfuseMac());
        snprintf(config.iddev, sizeof(config.iddev), "lsm-%06lX", static_cast<unsigned long>(chip & 0xFFFFFFUL));
    }

    // MQTT topics boleh kosong
    if (config.topic_subscribe[0] == '\0')
        strcpy(config.topic_subscribe, "/intilab/iot/act/");
    if (config.topic_publish[0] == '\0')
        strcpy(config.topic_publish, "/intilab/iot");

    if (config.no_sample[0] == '\0')
        strcpy(config.no_sample, "");

    if (config.shift[0] == '\0')
        strcpy(config.shift, "24");

    // Offset day
    if (config.offsetday <= 0)
        config.offsetday = 7;
}
