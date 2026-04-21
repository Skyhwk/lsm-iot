#pragma once
#include <Arduino.h>
#include <SD.h>

#define LOG_FILE "/log.bin"

#define MAX_LOG_RECORD 10000

#pragma pack(push, 1)

struct LogHeader
{
    uint32_t writeIndex;
    uint32_t totalWritten;
};

struct LogRecord
{
    uint32_t sequence;
    char iddev[16];
    char no_sampel[16];
    char noise[16];
    char datetime[20]; // YYYY-MM-DD HH:MM:SS
};

#pragma pack(pop)

class StorageManager
{
public:
    bool begin();

    // ===== LOG =====
    bool initLog();
    bool addLog(const LogRecord &rec);
    uint32_t getTotalLogWritten();

private:
    void safeCopy(char *dest, const char *src, size_t len);
};

extern StorageManager Storage;
