#pragma once
#include "LogService.h"
#include "Benchmark/States/BenchmarkState.h"

class LogService;

namespace Benchmark
{
    static void PrintStats(const TimeStats& stats, LogService* logs)
    {
        const std::wstring staticticStr = L"\nTimeStats\n\tMin FPS:" + std::to_wstring(stats.minFps)
            + L"\n\tMin MSPF:" + std::to_wstring(stats.minMspf)
            + L"\n\tMax FPS:" + std::to_wstring(stats.maxFps)
            + L"\n\tMax MSPF:" + std::to_wstring(stats.maxMspf);
        logs->PushMessage(staticticStr);
    }
}
