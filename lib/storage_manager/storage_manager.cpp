#include "storage_manager.h"

StorageManager Storage;

bool StorageManager::begin()
{
    return true;
}

String StorageManager::logPathForSample(const char *sample) const
{
    String name = sample;
    name.trim();

    if (name.length() == 0)
        name = "unknown-sample";

    for (size_t i = 0; i < name.length(); i++)
    {
        char c = name[i];
        bool allowed = isAlphaNumeric(c) || c == '-' || c == '_' || c == '.';
        if (c == '/' || c == '\\')
            name.setCharAt(i, '-');
        else if (!allowed)
            name.setCharAt(i, '_');
    }

    if (!name.endsWith(".txt"))
        name += ".txt";

    return "/" + name;
}

bool StorageManager::shiftSectionExists(const char *path, const char *shift) const
{
    File f = SD.open(path, FILE_READ);
    if (!f)
        return false;

    String marker = String("## ") + String(shift);
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line == marker)
        {
            f.close();
            return true;
        }
    }

    f.close();
    return false;
}

void StorageManager::writeHeader(File &f, const LogRecord &rec)
{
    f.println();
    f.println(String("## ") + String(rec.shift));
    f.println("datetime,dBA,LAeq,device,no_sample");
}

bool StorageManager::addLog(const LogRecord &rec)
{
    String path = logPathForSample(rec.no_sampel);
    bool isNewFile = !SD.exists(path.c_str());
    bool needsShiftHeader = isNewFile || !shiftSectionExists(path.c_str(), rec.shift);

    File f = SD.open(path.c_str(), FILE_APPEND);
    if (!f)
        return false;

    if (isNewFile)
    {
        f.println(String("# Sound Meter Log"));
        f.println(String("sample: ") + String(rec.no_sampel));
    }

    if (needsShiftHeader)
        writeHeader(f, rec);

    f.print(rec.datetime);
    f.print(",");
    f.print(rec.noise);
    f.print(",");
    f.print(rec.laeq);
    f.print(",");
    f.print(rec.iddev);
    f.print(",");
    f.println(rec.no_sampel);

    f.close();
    return true;
}

uint32_t StorageManager::getTotalLogWritten()
{
    return 0;
}
