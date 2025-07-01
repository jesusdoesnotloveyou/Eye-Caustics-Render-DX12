#pragma once
#include <functional>
#include <string>

#include "BenchmarkState.h"
#include "Utils.h"


class LogService;

class WaitState final : public virtual BenchmarkState
{
public:
    WaitState(uint32_t timeInSeconds, const std::function<void()>& OnCompleted, const std::function<void(const TimeStats&, double)>& OnStatChanged) : BenchmarkState(), OnCompleted(OnCompleted), OnStatChanged(OnStatChanged),
                                                                                                                                              waitTime(timeInSeconds)
    {
    }

private:
    void OnStatsCalculated(const TimeStats& stats) override;

    void Exit() override
    {
        BenchmarkState::Exit();
        OnCompleted();
    }

    std::function<void()> OnCompleted;
    std::function<void(const TimeStats&, double)> OnStatChanged;

    bool IsCompleted() override;
    uint32_t waitTime;
    uint32_t currentStatsCalculation = 0;
};
