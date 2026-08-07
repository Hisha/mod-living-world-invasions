#include "InvasionSpawnManager.h"

#include "CreatureProvider.h"
#include "GameObjectProvider.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "RuntimeEntityGroup.h"

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
    RegisterProvider(std::make_unique<GameObjectProvider>());
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
    sRuntimeEntityGroupMgr.Reset();
}

bool InvasionSpawnManager::SpawnGroup(uint64 runtimeId, uint32 spawnGroupId, uint64* runtimeGroupId)
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

    RuntimeEntityGroup& runtimeGroup = sRuntimeEntityGroupMgr.CreateGroup(runtimeId, spawnGroupId);

    if (runtimeGroupId)
    {
        *runtimeGroupId = runtimeGroup.Id;
    }

    LOG_INFO("server.loading",
        "[LWI Spawn] Runtime #{} executing spawn group {} ({}) as runtime entity group #{}.",
        runtimeId,
        spawnGroupId,
        group->Name,
        runtimeGroup.Id);

    bool spawnedAny = false;

    for (SpawnMemberDefinition const& member : *members)
    {
        IEntityProvider* provider = GetProvider(member.EntityType);
        if (!provider)
        {
            LOG_ERROR("server.loading",
                "[LWI Spawn] Runtime #{} spawn member {} uses unsupported entity type {}.",
                runtimeId,
                member.Id,
                member.EntityType);
            continue;
        }

        if (provider->Spawn(runtimeId, *group, member, runtimeGroup.Entities))
        {
            spawnedAny = true;
        }
    }

    if (!spawnedAny)
    {
        sRuntimeEntityGroupMgr.RemoveGroup(runtimeGroup.Id);

        if (runtimeGroupId)
        {
            *runtimeGroupId = 0;
        }

        LOG_ERROR("server.loading",
            "[LWI Spawn] Runtime #{} spawn group {} created no entities; runtime entity group removed.",
            runtimeId,
            spawnGroupId);

        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Spawn] Runtime entity group #{} now owns {} entity/entities for runtime #{}.",
        runtimeGroup.Id,
        runtimeGroup.Entities.size(),
        runtimeId);

    return true;
}

void InvasionSpawnManager::CleanupRuntime(uint64 runtimeId)
{
    std::vector<uint64> groupIds = sRuntimeEntityGroupMgr.GetGroupsForRuntime(runtimeId);
    if (groupIds.empty())
    {
        return;
    }

    uint32 cleaned = 0;
    uint32 tracked = 0;
    uint32 groupsCleaned = 0;

    for (uint64 groupId : groupIds)
    {
        RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(groupId);
        if (!group)
        {
            continue;
        }

        group->State = RuntimeEntityGroupState::CleaningUp;
        tracked += static_cast<uint32>(group->Entities.size());

        for (RuntimeEntity const& entity : group->Entities)
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

        group->State = RuntimeEntityGroupState::Completed;
        ++groupsCleaned;
    }

    sRuntimeEntityGroupMgr.RemoveRuntime(runtimeId);

    LOG_INFO("server.loading",
        "[LWI Spawn] Cleaned runtime #{}. Removed {} of {} tracked entity/entities across {} runtime entity group(s).",
        runtimeId,
        cleaned,
        tracked,
        groupsCleaned);
}
}
