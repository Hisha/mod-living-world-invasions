#ifndef MOD_LIVING_WORLD_INVASIONS_ASSAULT_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_ASSAULT_MANAGER_H

#include "Define.h"

#include <unordered_map>

namespace lwi
{
struct ActiveAssault
{
    uint64 RuntimeId = 0;
    uint64 RuntimeGroupId = 0;
    uint32 SpawnGroupId = 0;
    float SearchRadius = 40.0f;
    uint32 ReacquireIntervalMs = 2000;
    uint32 TargetPolicy = 0;
    uint32 ReacquireTimerMs = 0;
};

class AssaultManager
{
public:
    static AssaultManager& Instance();

    void Reset();
    void Update(uint32 diff);

    bool Start(uint64 runtimeId, uint32 spawnGroupId, uint32 radiusYards, uint32 reacquireIntervalMs, uint32 targetPolicy);
    void CancelRuntime(uint64 runtimeId);

private:
    bool TryAcquireTargets(ActiveAssault& assault);

    std::unordered_map<uint64, ActiveAssault> _activeAssaults;
};
}

#define sAssaultManager lwi::AssaultManager::Instance()

#endif
