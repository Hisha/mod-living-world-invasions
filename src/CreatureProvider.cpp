#include "CreatureProvider.h"

#include "Creature.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "Position.h"
#include "Random.h"
#include "TemporarySummon.h"

#include <cmath>

namespace lwi
{
uint8 CreatureProvider::GetType() const
{
    return static_cast<uint8>(EntityProviderType::Creature);
}

bool CreatureProvider::Spawn(
    uint64 runtimeId,
    SpawnGroupDefinition const& group,
    SpawnMemberDefinition const& member,
    std::vector<RuntimeEntity>& spawnedEntities)
{
    Map* map = sMapMgr->FindMap(group.MapId, 0);
    if (!map)
    {
        LOG_ERROR("server.loading",
            "[LWI Entity] Creature provider could not find map {} for spawn group {}.",
            group.MapId,
            group.Id);
        return false;
    }

    bool spawnedAny = false;

    for (uint32 i = 0; i < member.Count; ++i)
    {
        Position position;
        position.Relocate(
            group.X,
            group.Y,
            group.Z,
            group.Orientation);

        if (group.SpawnRadius > 0.0f)
        {
            constexpr float TwoPi = 6.28318530717958647692f;
            float angle = frand(0.0f, TwoPi);
            float distance = frand(0.0f, group.SpawnRadius);

            position.m_positionX += std::cos(angle) * distance;
            position.m_positionY += std::sin(angle) * distance;
        }

        TempSummon* summon = map->SummonCreature(
            member.CreatureEntry,
            position,
            nullptr,
            0);

        if (!summon)
        {
            LOG_ERROR("server.loading",
                "[LWI Entity] Runtime #{} failed to spawn creature entry {} from member {}.",
                runtimeId,
                member.CreatureEntry,
                member.Id);
            continue;
        }

        RuntimeEntity entity;
        entity.EntityType = GetType();
        entity.MapId = group.MapId;
        entity.GroupId = group.Id;
        entity.MemberId = member.Id;
        entity.Entry = member.CreatureEntry;
        entity.Guid = summon->GetGUID();
        spawnedEntities.push_back(std::move(entity));

        LOG_INFO("server.loading",
            "[LWI Entity] Runtime #{} spawned creature {} from member {} GUID {}.",
            runtimeId,
            member.CreatureEntry,
            member.Id,
            summon->GetGUID().ToString());

        spawnedAny = true;
    }

    return spawnedAny;
}

bool CreatureProvider::Cleanup(RuntimeEntity const& entity)
{
    Map* map = sMapMgr->FindMap(entity.MapId, 0);
    if (!map)
    {
        return false;
    }

    Creature* creature = map->GetCreature(entity.Guid);
    if (!creature)
    {
        return false;
    }

    creature->DespawnOrUnsummon();
    return true;
}
}
