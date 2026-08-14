#include "CreatureProvider.h"

#include "Creature.h"
#include "LivingWorldInvasions.h"
#include "LwiCreatureTemplateManager.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "Position.h"
#include "Random.h"
#include "TemporarySummon.h"

#include <algorithm>
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

char const* TacticalRoleName(TacticalRole role)
{
    switch (role)
    {
        case TacticalRole::Default:
            return "Default";
        case TacticalRole::Commander:
            return "Commander";
        case TacticalRole::Protector:
            return "Protector";
        case TacticalRole::MeleeDps:
            return "MeleeDps";
        case TacticalRole::RangedDps:
            return "RangedDps";
        case TacticalRole::Healer:
            return "Healer";
        case TacticalRole::Support:
            return "Support";
        default:
            return "Unknown";
    }
}
}

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
            "[LWI Creature] Could not find map {} for spawn group {}.",
            group.MapId,
            group.Id);
        return false;
    }

    uint32 creatureEntry = member.EntityEntry;

    if (member.LwiTemplateId != 0)
    {
        creatureEntry = sLwiCreatureTemplateMgr.ResolveEntry(member.LwiTemplateId);
        if (creatureEntry == 0)
        {
            LOG_ERROR("server.loading",
                "[LWI Creature] Runtime #{} spawn member {} references unresolved LWI creature template {}.",
                runtimeId,
                member.Id,
                member.LwiTemplateId);
            return false;
        }
    }

    if (creatureEntry == 0)
    {
        LOG_ERROR("server.loading",
            "[LWI Creature] Runtime #{} spawn member {} has neither a creature entry nor an LWI creature template.",
            runtimeId,
            member.Id);
        return false;
    }

    bool spawnedAny = false;

    for (uint32 i = 0; i < member.Count; ++i)
    {
        Position position = BuildSpawnPosition(group);

        TempSummon* summon = map->SummonCreature(
            creatureEntry,
            position,
            nullptr,
            0);

        if (!summon)
        {
            LOG_ERROR("server.loading",
                "[LWI Creature] Runtime #{} failed to spawn creature entry {} from member {}.",
                runtimeId,
                creatureEntry,
                member.Id);
            continue;
        }

        if (member.LevelOverride > 0)
        {
            uint8 const requestedLevel = static_cast<uint8>(std::min<uint16>(member.LevelOverride, 255));
            summon->SetLevel(requestedLevel);

            // Recalculate level-dependent creature stats after changing the level.
            // Without this, the displayed level can change while health/mana/damage
            // remain based on the original creature-template level.
            summon->UpdateAllStats();
            summon->SetFullHealth();
            if (summon->GetMaxPower(POWER_MANA) > 0)
            {
                summon->SetPower(POWER_MANA, summon->GetMaxPower(POWER_MANA));
            }

            LOG_INFO("server.loading",
                "[LWI Creature] Runtime #{} applied level override {} to creature {} from member {}.",
                runtimeId,
                static_cast<uint32>(requestedLevel),
                creatureEntry,
                member.Id);
        }

        RuntimeEntity entity;
        entity.EntityType = GetType();
        entity.MapId = group.MapId;
        entity.GroupId = group.Id;
        entity.MemberId = member.Id;
        entity.Entry = creatureEntry;
        entity.LwiTemplateId = member.LwiTemplateId;
        entity.TacticalRole = static_cast<uint8>(member.Role);
        entity.Guid = summon->GetGUID();
        spawnedEntities.push_back(std::move(entity));

        LOG_INFO("server.loading",
            "[LWI Creature] Runtime #{} spawned creature {} from member {} role {} ({}) GUID {}.",
            runtimeId,
            creatureEntry,
            member.Id,
            static_cast<uint32>(member.Role),
            TacticalRoleName(member.Role),
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
