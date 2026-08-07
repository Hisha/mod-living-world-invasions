#include "InvasionSpawnManager.h"

#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Position.h"
#include "TemporarySummon.h"

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

    LOG_INFO("server.loading",
        "[LWI Spawn] Runtime #{} executing spawn group {} ({}).",
        runtimeId,
        spawnGroupId,
        group->Name);


    Map* map = sMapMgr->FindMap(group->MapId, 0);

    if (!map)
    {
        LOG_ERROR("server.loading",
            "[LWI Spawn] Unable to find map {} for spawn group {}.",
            group->MapId,
            spawnGroupId);

        return false;
    }


    Position position;
    position.Relocate(
        group->X,
        group->Y,
        group->Z,
        group->Orientation
    );


    for (auto const& member : *members)
    {
        for (uint32 i = 0; i < member.Count; ++i)
        {
            TempSummon* summon = map->SummonCreature(
                member.CreatureEntry,
                position,
                nullptr,
                0
            );

            if (!summon)
            {
                LOG_ERROR("server.loading",
                    "[LWI Spawn] Failed spawning creature entry {}.",
                    member.CreatureEntry);

                continue;
            }


            _runtimeCreatures[runtimeId].push_back(
                summon->GetGUID()
            );


            LOG_INFO("server.loading",
                "[LWI Spawn] Runtime #{} spawned creature {} GUID {}.",
                runtimeId,
                member.CreatureEntry,
                summon->GetGUID().ToString());
        }
    }

    return true;
}

void InvasionSpawnManager::CleanupRuntime(uint64 runtimeId)
{
    auto itr = _runtimeCreatures.find(runtimeId);

    if (itr == _runtimeCreatures.end())
        return;


    uint32 despawned = 0;

    for (SpawnedCreature const& spawned : itr->second)
    {
        Map* map = sMapMgr->FindMap(spawned.mapId, 0);

        if (!map)
            continue;

        if (Creature* creature = map->GetCreature(spawned.guid))
        {
            creature->DespawnOrUnsummon();
            ++despawned;
        }
    }


    _runtimeCreatures.erase(itr);


    LOG_INFO("server.loading",
        "[LWI Spawn] Cleaned runtime #{}. Despawned {} creature(s).",
        runtimeId,
        despawned);
}
}
