#include "InvasionSpawnManager.h"

#include "LivingWorldInvasions.h"
#include "Log.h"

namespace lwi
{
InvasionSpawnManager& InvasionSpawnManager::Instance()
{
    static InvasionSpawnManager instance;
    return instance;
}

void InvasionSpawnManager::Reset()
{
    _runtimeCreatures.clear();
}

bool InvasionSpawnManager::SpawnGroup(uint64 runtimeId, uint32 spawnGroupId)
{
    SpawnGroupDefinition const* group = sInvasionMgr.GetSpawnGroup(spawnGroupId);
    if (!group)
    {
        LOG_ERROR("server.loading", "[LWI Spawn] Missing spawn group {}.", spawnGroupId);
        return false;
    }

    auto const* members = sInvasionMgr.GetSpawnMembers(spawnGroupId);
    if (!members || members->empty())
    {
        LOG_ERROR("server.loading", "[LWI Spawn] Spawn group {} has no members.", spawnGroupId);
        return false;
    }

    // Creature creation is intentionally added as the next layer. This first pass
    // validates data flow and ownership tracking.
    LOG_INFO("server.loading", "[LWI Spawn] Runtime #{} executing spawn group {} ({}).",
        runtimeId, spawnGroupId, group->Name);

    return true;
}

void InvasionSpawnManager::CleanupRuntime(uint64 runtimeId)
{
    _runtimeCreatures.erase(runtimeId);
    LOG_INFO("server.loading", "[LWI Spawn] Cleaned runtime #{} spawn tracking.", runtimeId);
}
}
