#include "storage_manager.h"

StorageManager Storage;

bool StorageManager::begin()
{
    return true;
}

String StorageManager::sampleFolderName(const char *sample) const
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

    return name;
}

String StorageManager::logPathForSample(const char *sample) const
{
    return "/" + sampleFolderName(sample) + "/data.txt";
}

bool StorageManager::ensureSampleFolder(const char *sample)
{
    String folderPath = "/" + sampleFolderName(sample);
    if (SD.exists(folderPath.c_str()))
        return true;

    return SD.mkdir(folderPath.c_str());
}

void StorageManager::writeHeader(File &f)
{
    f.println("datetime;no_sample;shift;dBA;LAeq;device");
}

bool StorageManager::addLog(const LogRecord &rec)
{
    if (!ensureSampleFolder(rec.no_sampel))
        return false;

    String path = logPathForSample(rec.no_sampel);
    bool isNewFile = !SD.exists(path.c_str());

    File f = SD.open(path.c_str(), FILE_APPEND);
    if (!f)
        return false;

    if (isNewFile)
        writeHeader(f);

    f.print(rec.datetime);
    f.print(";");
    f.print(rec.no_sampel);
    f.print(";");
    f.print(rec.shift);
    f.print(";");
    f.print(rec.noise);
    f.print(";");
    f.print(rec.laeq);
    f.print(";");
    f.println(rec.iddev);

    f.close();
    return true;
}

uint32_t StorageManager::getTotalLogWritten()
{
    return 0;
}
