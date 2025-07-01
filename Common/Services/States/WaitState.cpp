#include "pch.h"
#include "WaitState.h"

void WaitState::OnStatsCalculated(const TimeStats& stats)
{
    currentStatsCalculation++;
    OnStatChanged(stats, currentStatsCalculation/waitTime);
}

bool WaitState::IsCompleted()
{
    return currentStatsCalculation <= waitTime;
}
