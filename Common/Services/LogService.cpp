#include "pch.h"
#include "LogService.h"

#include <fstream>

#include "AssetsLoader.h"

void LogService::PushMessage(const std::wstring& message)
{
    OutputDebugStringW(message.c_str());
    queue.Push(message);
}

bool LogService::WriteAllLog(const std::filesystem::path& filePath)
{
    OutputDebugStringW(filePath.c_str());
    if (std::filesystem::exists(filePath) == false)
    {
        return false;
    }

    std::wofstream fileSteam;
    fileSteam.open(filePath.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        std::wstring line;

        while (queue.Size() > 0)
        {
            while (queue.TryPop(line))
            {
                fileSteam << line;
            }
        }

        fileSteam.flush();
        fileSteam.close();
        return true;
    }
    return false;
}
