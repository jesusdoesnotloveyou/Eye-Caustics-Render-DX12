#pragma once
#include "LockThreadQueue.h"
#include <filesystem>

class LogService
{
    PEPEngine::Utils::LockThreadQueue<std::wstring> queue;

public:
    void PushMessage(const std::wstring& message);

    bool WriteAllLog(const std::filesystem::path& filePath);
};
