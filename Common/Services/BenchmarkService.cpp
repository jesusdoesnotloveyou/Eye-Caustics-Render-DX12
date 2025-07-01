#include "pch.h"
#include "BenchmarkService.h"
#include "LogService.h"


void BenchmarkService::SetState(BenchmarkState* state)
{
    if (CurrentState != nullptr)
    {
        CurrentState->Exit();
    }
    CurrentState = state;
    if (CurrentState != nullptr)
    {
        CurrentState->Enter();
    }
}

void BenchmarkService::Start()
{
    currentStateIndex = 0;
    if (!states.empty())
    {
        CurrentState = states[currentStateIndex].get();
    }
}


void BenchmarkService::Tick(float deltaTime)
{
    if (currentStateIndex < 0 || currentStateIndex >= states.size()) return;
    if (CurrentState)
    {
        CurrentState->Tick(deltaTime);
        if (CurrentState->IsCompleted())
        {
            currentStateIndex = (currentStateIndex + 1) % states.size();
            SetState(states[currentStateIndex].get());
        }
    }
}
