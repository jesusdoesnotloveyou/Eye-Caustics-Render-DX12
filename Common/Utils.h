#pragma once
#include <format>

#include "Services/LogService.h"
#include "Services/States/BenchmarkState.h"

class LogService;

namespace Benchmark
{
    static void PrintStats(const TimeStats& stats, LogService* logs)
    {
        const std::wstring staticticStr = std::format(L"\nTimeStats"
                                                      "\n\tFPS:{:.2f}"
                                                      "\n\tMSPF:{:.2f}"
                                                      "\n\tMin FPS:{:.2f}"
                                                      "\n\tMin MSPF:{:.2f}"
                                                      "\n\tMax FPS:{:.2f}"
                                                      "\n\tMax MSPF:{:.2f}",
            stats.fps, stats.mspf, stats.minFps, stats.minMspf, stats.maxFps, stats.maxMspf);
        logs->PushMessage(staticticStr);
    }
}
