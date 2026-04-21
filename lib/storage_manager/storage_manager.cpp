#include "storage_manager.h"

StorageManager Storage;

bool StorageManager::begin()
{
    // ===============================
    // LOG FILE
    // ===============================

    if (!SD.exists(LOG_FILE))
    {
        File f = SD.open(LOG_FILE, FILE_WRITE);
        if (!f)
            return false;
        f.close();
        Serial.println("log.bin created");
    }

    return initLog();
}

void StorageManager::safeCopy(char *dest, const char *src, size_t len)
{
    memset(dest, 0, len);
    strncpy(dest, src, len - 1);
}

//
// ====================== LOG ======================
//

bool StorageManager::initLog()
{
    bool needInit = !SD.exists(LOG_FILE);

    if (!needInit)
    {
        File existing = SD.open(LOG_FILE, FILE_READ);
        if (!existing)
            return false;
        needInit = existing.size() < sizeof(LogHeader);
        existing.close();
    }

    if (needInit)
    {
        SD.remove(LOG_FILE);
        File f = SD.open(LOG_FILE, FILE_WRITE);
        if (!f)
            return false;

        LogHeader header = {};
        header.writeIndex = 0;
        header.totalWritten = 0;

        f.write((uint8_t *)&header, sizeof(header));
        f.close();
    }
    return true;
}

bool StorageManager::addLog(const LogRecord &input)
{
    File f = SD.open(LOG_FILE, "r+");
    if (!f)
        return false;

    LogHeader header = {};
    size_t headerRead = f.read((uint8_t *)&header, sizeof(header));
    if (headerRead != sizeof(header))
    {
        f.close();
        if (!initLog())
            return false;

        f = SD.open(LOG_FILE, "r+");
        if (!f)
            return false;

        headerRead = f.read((uint8_t *)&header, sizeof(header));
        if (headerRead != sizeof(header))
        {
            f.close();
            return false;
        }
    }

    uint32_t index = header.writeIndex % MAX_LOG_RECORD;

    LogRecord rec = input;
    rec.sequence = header.totalWritten + 1;

    f.seek(sizeof(LogHeader) + index * sizeof(LogRecord));
    f.write((uint8_t *)&rec, sizeof(rec));

    header.writeIndex++;
    header.totalWritten++;

    f.seek(0);
    f.write((uint8_t *)&header, sizeof(header));

    f.close();
    return true;
}

uint32_t StorageManager::getTotalLogWritten()
{
    File f = SD.open(LOG_FILE, FILE_READ);
    if (!f)
        return 0;

    LogHeader header;
    f.read((uint8_t *)&header, sizeof(header));
    f.close();

    return header.totalWritten;
}
