#pragma once
#include <memory>
#include <vector>

#include "States/BenchmarkState.h"

class LogService;



class BenchmarkService final
{
    std::vector<std::shared_ptr<BenchmarkState>> states;
    int currentStateIndex = -1;

    BenchmarkState* CurrentState;

    void SetState(BenchmarkState* state);

public:
    void Start();
    
    template<class T = BenchmarkState, typename... Args>
    inline void AddState(Args&&... args)
    {
        states.emplace_back(std::make_shared<T>(std::forward<Args>(args)...));
    }
    void Tick(float deltaTime);
};
