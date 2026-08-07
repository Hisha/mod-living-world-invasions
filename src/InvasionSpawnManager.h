#ifndef MOD_LIVING_WORLD_INVASIONS_SPAWN_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_SPAWN_MANAGER_H

#include "Define.h"
#include "ObjectGuid.h"
#include <unordered_map>
#include <vector>

namespace lwi
{
class InvasionSpawnManager
{
public:
    static InvasionSpawnManager& Instance();

    void Reset();
    bool SpawnGroup(uint64 runtimeId, uint32 spawnGroupId);
    void CleanupRuntime(uint64 runtimeId);

private:
    std::unordered_map<uint64, std::vector<ObjectGuid>> _runtimeCreatures;
};
}

#define sInvasionSpawnMgr lwi::InvasionSpawnManager::Instance()

#endif
