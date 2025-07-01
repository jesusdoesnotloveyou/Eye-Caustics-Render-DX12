#include "pch.h"
#include "WaitState.h"

void WaitState::OnStatsCalculated(const TimeStats& stats)
{
    currentStatsCalculation++;
    OnStatChanged(stats, (float) currentStatsCalculation/waitTime);
}

bool WaitState::IsCompleted()
{
    return currentStatsCalculation >= waitTime;
}
