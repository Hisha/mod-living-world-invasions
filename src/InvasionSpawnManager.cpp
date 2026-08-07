#include "InvasionSpawnManager.h"

#include "CreatureProvider.h"
#include "LivingWorldInvasions.h"
#include "Log.h"

#include <utility>

namespace lwi
{
InvasionSpawnManager& InvasionSpawnManager::Instance()
{
    static InvasionSpawnManager instance;
    return instance;
}

InvasionSpawnManager::InvasionSpawnManager()
{
    RegisterProvider(std::make_unique<CreatureProvider>());
}

void InvasionSpawnManager::RegisterProvider(std::unique_ptr<IEntityProvider> provider)
{
    if (!provider)
    {
        return;
    }

    uint8 type = provider->GetType();
    _providers[type] = std::move(provider);
}

IEntityProvider* InvasionSpawnManager::GetProvider(uint8 entityType)
{
    auto itr = _providers.find(entityType);
    if (itr == _providers.end())
    {
        return nullptr;
    }

    return itr->second.get();
}

void InvasionSpawnManager::Reset()
{
    _runtimeEntities.clear();
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

    LOG_INFO("server.loading",
        "[LWI Spawn] Runtime #{} executing spawn group {} ({}).",
        runtimeId,
        spawnGroupId,
        group->Name);

    // Provider architecture is introduced before the SQL entity-type column.
    // All existing spawn members are creatures, so they are dispatched to the
    // creature provider without changing current database behavior.
    uint8 entityType = static_cast<uint8>(EntityProviderType::Creature);
    IEntityProvider* provider = GetProvider(entityType);
    if (!provider)
    {
        LOG_ERROR("server.loading",
            "[LWI Spawn] No entity provider registered for type {}.",
            entityType);
        return false;
    }

    bool spawnedAny = false;
    std::vector<RuntimeEntity>& runtimeEntities = _runtimeEntities[runtimeId];

    for (SpawnMemberDefinition const& member : *members)
    {
        if (provider->Spawn(runtimeId, *group, member, runtimeEntities))
        {
            spawnedAny = true;
        }
    }

    return spawnedAny;
}

void InvasionSpawnManager::CleanupRuntime(uint64 runtimeId)
{
    auto itr = _runtimeEntities.find(runtimeId);
    if (itr == _runtimeEntities.end())
    {
        return;
    }

    uint32 cleaned = 0;

    for (RuntimeEntity const& entity : itr->second)
    {
        IEntityProvider* provider = GetProvider(entity.EntityType);
        if (!provider)
        {
            LOG_ERROR("server.loading",
                "[LWI Spawn] Runtime #{} cannot clean entity GUID {} because provider type {} is not registered.",
                runtimeId,
                entity.Guid.ToString(),
                entity.EntityType);
            continue;
        }

        if (provider->Cleanup(entity))
        {
            ++cleaned;
        }
    }

    std::size_t tracked = itr->second.size();
    _runtimeEntities.erase(itr);

    LOG_INFO("server.loading",
        "[LWI Spawn] Cleaned runtime #{}. Removed {} of {} tracked entity/entities.",
        runtimeId,
        cleaned,
        tracked);
}
}
