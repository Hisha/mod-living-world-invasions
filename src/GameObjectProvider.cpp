#include "GameObjectProvider.h"

#include "GameObject.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "Position.h"
#include "Random.h"

#include <cmath>

namespace lwi
{
namespace
{
Position BuildSpawnPosition(SpawnGroupDefinition const& group)
{
    Position position;
    position.Relocate(group.X, group.Y, group.Z, group.Orientation);

    if (group.SpawnRadius > 0.0f)
    {
        constexpr float TwoPi = 6.28318530717958647692f;
        float angle = frand(0.0f, TwoPi);
        float distance = frand(0.0f, group.SpawnRadius);

        position.m_positionX += std::cos(angle) * distance;
        position.m_positionY += std::sin(angle) * distance;
    }

    return position;
}
}

uint8 GameObjectProvider::GetType() const
{
    return static_cast<uint8>(EntityProviderType::GameObject);
}

bool GameObjectProvider::Spawn(
    uint64 runtimeId,
    SpawnGroupDefinition const& group,
    SpawnMemberDefinition const& member,
    std::vector<RuntimeEntity>& spawnedEntities)
{
    Map* map = sMapMgr->FindMap(group.MapId, 0);
    if (!map)
    {
        LOG_ERROR("server.loading",
            "[LWI GameObject] Could not find map {} for spawn group {}.",
            group.MapId,
            group.Id);
        return false;
    }

    bool spawnedAny = false;

    for (uint32 i = 0; i < member.Count; ++i)
    {
        Position position = BuildSpawnPosition(group);

        GameObject* gameObject = map->SummonGameObject(
            member.EntityEntry,
            position,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0,
            true);

        if (!gameObject)
        {
            LOG_ERROR("server.loading",
                "[LWI GameObject] Runtime #{} failed to spawn gameobject entry {} from member {}.",
                runtimeId,
                member.EntityEntry,
                member.Id);
            continue;
        }

        RuntimeEntity entity;
        entity.EntityType = GetType();
        entity.MapId = group.MapId;
        entity.GroupId = group.Id;
        entity.MemberId = member.Id;
        entity.Entry = member.EntityEntry;
        entity.Guid = gameObject->GetGUID();
        spawnedEntities.push_back(std::move(entity));

        LOG_INFO("server.loading",
            "[LWI GameObject] Runtime #{} spawned gameobject {} from member {} GUID {}.",
            runtimeId,
            member.EntityEntry,
            member.Id,
            gameObject->GetGUID().ToString());

        spawnedAny = true;
    }

    return spawnedAny;
}

bool GameObjectProvider::Cleanup(RuntimeEntity const& entity)
{
    Map* map = sMapMgr->FindMap(entity.MapId, 0);
    if (!map)
    {
        return false;
    }

    GameObject* gameObject = map->GetGameObject(entity.Guid);
    if (!gameObject)
    {
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI GameObject] Deleting gameobject entry {} GUID {}.",
        entity.Entry,
        entity.Guid.ToString());

    gameObject->Delete();

    return true;
}
}
