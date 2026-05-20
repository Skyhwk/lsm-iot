#pragma once
#include <Arduino.h>
#include <SD.h>

struct LogRecord
{
    uint32_t sequence;
    char iddev[50];
    char no_sampel[32];
    char shift[8];
    char noise[16];
    char laeq[24];
    char datetime[20]; // YYYY-MM-DDTHH:MM:SS
};

class StorageManager
{
public:
    bool begin();

    // ===== LOG =====
    bool addLog(const LogRecord &rec);
    uint32_t getTotalLogWritten();

private:
    String sampleFolderName(const char *sample) const;
    String logPathForSample(const char *sample) const;
    bool ensureSampleFolder(const char *sample);
    void writeHeader(File &f);
};

extern StorageManager Storage;
