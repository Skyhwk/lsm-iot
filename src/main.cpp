#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>

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

// ================= TIMING =================
unsigned long bootTimeMs = 0;
unsigned long lastApplyModeMs = 0;

// ================= TASK HANDLES =================
TaskHandle_t taskHandleMQTT;
TaskHandle_t taskHandleTime;
TaskHandle_t taskHandleLCD;

// ================= TASKS =================

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

// ================= INIT FUNCTIONS =================

bool initStorage()
{
    if (!SDCard_init())
        return false;

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
