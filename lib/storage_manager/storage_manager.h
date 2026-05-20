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
    String logPathForSample(const char *sample) const;
    bool shiftSectionExists(const char *path, const char *shift) const;
    void writeHeader(File &f, const LogRecord &rec);
};

extern StorageManager Storage;
