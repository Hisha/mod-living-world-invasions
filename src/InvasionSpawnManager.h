#ifndef MOD_LIVING_WORLD_INVASIONS_SPAWN_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_SPAWN_MANAGER_H

#include "Define.h"
#include "IEntityProvider.h"

#include <memory>
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
    InvasionSpawnManager();

    IEntityProvider* GetProvider(uint8 entityType);
    void RegisterProvider(std::unique_ptr<IEntityProvider> provider);

    std::unordered_map<uint8, std::unique_ptr<IEntityProvider>> _providers;
    std::unordered_map<uint64, std::vector<RuntimeEntity>> _runtimeEntities;
};
}

#define sInvasionSpawnMgr lwi::InvasionSpawnManager::Instance()

#endif
