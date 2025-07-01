#include "pch.h"
#include "BenchmarkService.h"
#include "../LogService.h"

const std::shared_ptr<BenchmarkState>& BenchmarkService::GetCurrentState() const
{
    return states[currentStateIndex];
}

void BenchmarkService::Start()
{
    currentStateIndex = 0;
    GetCurrentState()->Enter();
}


void BenchmarkService::Tick(float deltaTime)
{
    if (currentStateIndex < 0 || currentStateIndex >= states.size()) return;
    if (const auto& State = GetCurrentState())
    {
        State->Tick(deltaTime);
        if (State->IsCompleted())
        {
            ++currentStateIndex;
        }
    }
}
