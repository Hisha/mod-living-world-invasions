#ifndef MOD_LIVING_WORLD_INVASIONS_ASSAULT_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_ASSAULT_MANAGER_H

#include "Define.h"
#include "ObjectGuid.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

class Creature;

namespace lwi
{
struct TemporaryNpcCombatOverride
{
    ObjectGuid Guid;
    uint32 MapId = 0;
    bool WasImmuneToNpc = false;
    uint32 OriginalFaction = 0;
    uint32 TemporaryFaction = 0;
    bool FactionChanged = false;
    std::unordered_set<uint64> RuntimeIds;
};

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

    bool EnsureWorldDefenderAttackable(uint64 runtimeId, Creature* attacker, Creature* target, float searchRadius);
    void ReleaseRuntimeOverrides(uint64 runtimeId);
    void RestoreAllOverrides();
    void RestoreOverride(TemporaryNpcCombatOverride const& overrideData);

    std::unordered_map<uint64, ActiveAssault> _activeAssaults;
    std::vector<TemporaryNpcCombatOverride> _temporaryNpcOverrides;
};
}

#define sAssaultManager lwi::AssaultManager::Instance()

#endif
