#include "AssaultManager.h"

#include "Creature.h"
#include "CreatureAI.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "RuntimeEntityGroup.h"

#include <algorithm>
#include <vector>

namespace lwi
{
namespace
{
constexpr float DefaultSearchRadius = 40.0f;
constexpr uint32 DefaultReacquireIntervalMs = 2000;
constexpr uint32 MinimumReacquireIntervalMs = 500;

void LogCombatDiagnostics(
    char const* phase,
    uint64 runtimeId,
    RuntimeEntity const& entity,
    Creature* attacker,
    Creature* target,
    bool explicitServiceTarget)
{
    if (!attacker || !target)
    {
        return;
    }

    uint32 const npcFlags = target->GetCreatureTemplate()->npcflag;

    LOG_INFO("server.loading",
        "[LWI AssaultDiag] {} runtime #{} attacker {} member {} -> target {} GUID {} "
        "explicitService={} distance={:.2f} "
        "attackerFaction={} targetFaction={} attackerReaction={} targetReaction={} "
        "validAttackTarget={} canCreatureAttack={} canStartAttack={} targetable={} "
        "attackerImmuneToNpc={} targetImmuneToNpc={} "
        "attackerCombatDisallowed={} targetCombatDisallowed={} "
        "attackerInCombat={} targetInCombat={} attackerVictim={} targetVictim={} "
        "targetNpcFlags={} targetQuestGiver={} targetVendor={} targetFlightMaster={} "
        "targetNonAttackable={} targetNotSelectable={} targetReactState={}.",
        phase,
        runtimeId,
        entity.Entry,
        entity.MemberId,
        target->GetEntry(),
        target->GetGUID().ToString(),
        explicitServiceTarget,
        attacker->GetDistance(target),
        attacker->GetFaction(),
        target->GetFaction(),
        static_cast<uint32>(attacker->GetReactionTo(target)),
        static_cast<uint32>(target->GetReactionTo(attacker)),
        attacker->IsValidAttackTarget(target),
        attacker->CanCreatureAttack(target, true),
        attacker->CanStartAttack(target, true),
        target->isTargetableForAttack(),
        attacker->IsImmuneToNPC(),
        target->IsImmuneToNPC(),
        attacker->IsCombatDisallowed(),
        target->IsCombatDisallowed(),
        attacker->IsInCombat(),
        target->IsInCombat(),
        attacker->GetVictim() ? attacker->GetVictim()->GetEntry() : 0,
        target->GetVictim() ? target->GetVictim()->GetEntry() : 0,
        npcFlags,
        (npcFlags & UNIT_NPC_FLAG_QUESTGIVER) != 0,
        (npcFlags & UNIT_NPC_FLAG_VENDOR) != 0,
        (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0,
        target->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE),
        target->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE),
        static_cast<uint32>(target->GetReactState()));
}
}

AssaultManager& AssaultManager::Instance()
{
    static AssaultManager instance;
    return instance;
}

void AssaultManager::Reset()
{
    RestoreAllOverrides();
    _activeAssaults.clear();
}

bool AssaultManager::EnsureServiceNpcAttackable(uint64 runtimeId, Creature* target)
{
    if (!target)
    {
        return false;
    }

    for (TemporaryNpcCombatOverride& overrideData : _temporaryNpcOverrides)
    {
        if (overrideData.MapId == target->GetMapId() && overrideData.Guid == target->GetGUID())
        {
            overrideData.RuntimeIds.insert(runtimeId);
            return !target->IsImmuneToNPC();
        }
    }

    if (!target->IsImmuneToNPC())
    {
        return true;
    }

    TemporaryNpcCombatOverride overrideData;
    overrideData.Guid = target->GetGUID();
    overrideData.MapId = target->GetMapId();
    overrideData.WasImmuneToNpc = true;
    overrideData.RuntimeIds.insert(runtimeId);

    target->SetImmuneToNPC(false);

    if (target->IsImmuneToNPC())
    {
        LOG_ERROR("server.loading",
            "[LWI Assault] Runtime #{} failed to remove NPC immunity from service target {} GUID {}.",
            runtimeId,
            target->GetEntry(),
            target->GetGUID().ToString());
        return false;
    }

    _temporaryNpcOverrides.push_back(std::move(overrideData));

    LOG_INFO("server.loading",
        "[LWI Assault] Runtime #{} temporarily removed UNIT_FLAG_IMMUNE_TO_NPC from service target {} GUID {}.",
        runtimeId,
        target->GetEntry(),
        target->GetGUID().ToString());

    return true;
}

void AssaultManager::RestoreOverride(TemporaryNpcCombatOverride const& overrideData)
{
    Map* map = sMapMgr->FindMap(overrideData.MapId, 0);
    if (!map)
    {
        return;
    }

    Creature* target = map->GetCreature(overrideData.Guid);
    if (!target)
    {
        return;
    }

    target->CombatStop(true);

    if (overrideData.WasImmuneToNpc)
    {
        target->SetImmuneToNPC(true);
    }

    LOG_INFO("server.loading",
        "[LWI Assault] Restored service target {} GUID {} NPC immunity state to {}.",
        target->GetEntry(),
        target->GetGUID().ToString(),
        overrideData.WasImmuneToNpc);
}

void AssaultManager::ReleaseRuntimeOverrides(uint64 runtimeId)
{
    for (auto itr = _temporaryNpcOverrides.begin(); itr != _temporaryNpcOverrides.end();)
    {
        itr->RuntimeIds.erase(runtimeId);

        if (!itr->RuntimeIds.empty())
        {
            ++itr;
            continue;
        }

        RestoreOverride(*itr);
        itr = _temporaryNpcOverrides.erase(itr);
    }
}

void AssaultManager::RestoreAllOverrides()
{
    for (TemporaryNpcCombatOverride const& overrideData : _temporaryNpcOverrides)
    {
        RestoreOverride(overrideData);
    }

    _temporaryNpcOverrides.clear();
}

bool AssaultManager::Start(
    uint64 runtimeId,
    uint32 spawnGroupId,
    uint32 radiusYards,
    uint32 reacquireIntervalMs,
    uint32 targetPolicy)
{
    RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.FindLatestGroup(runtimeId, spawnGroupId);
    if (!group)
    {
        LOG_ERROR("server.loading",
            "[LWI Assault] Runtime #{} cannot start assault because spawn group {} has no runtime entity group.",
            runtimeId,
            spawnGroupId);
        return false;
    }

    ActiveAssault assault;
    assault.RuntimeId = runtimeId;
    assault.RuntimeGroupId = group->Id;
    assault.SpawnGroupId = spawnGroupId;
    assault.SearchRadius = radiusYards == 0
        ? DefaultSearchRadius
        : static_cast<float>(radiusYards);
    assault.ReacquireIntervalMs = reacquireIntervalMs == 0
        ? DefaultReacquireIntervalMs
        : std::max<uint32>(MinimumReacquireIntervalMs, reacquireIntervalMs);
    assault.ReacquireTimerMs = 0;
    assault.TargetPolicy = targetPolicy;

    _activeAssaults[group->Id] = assault;

    LOG_INFO("server.loading",
        "[LWI Assault] Runtime #{} runtime entity group #{} started assault behavior "
        "with {:.1f} yard search radius, {} ms reacquire interval, target policy {}.",
        runtimeId,
        group->Id,
        assault.SearchRadius,
        assault.ReacquireIntervalMs,
        assault.TargetPolicy);

    TryAcquireTargets(_activeAssaults[group->Id]);
    return true;
}

void AssaultManager::Update(uint32 diff)
{
    std::vector<uint64> finished;

    for (auto& [runtimeGroupId, assault] : _activeAssaults)
    {
        RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
        if (!group || group->State != RuntimeEntityGroupState::Active)
        {
            finished.push_back(runtimeGroupId);
            continue;
        }

        if (assault.ReacquireTimerMs > diff)
        {
            assault.ReacquireTimerMs -= diff;
            continue;
        }

        assault.ReacquireTimerMs = assault.ReacquireIntervalMs;
        TryAcquireTargets(assault);
    }

    for (uint64 runtimeGroupId : finished)
    {
        _activeAssaults.erase(runtimeGroupId);
    }
}

bool AssaultManager::TryAcquireTargets(ActiveAssault& assault)
{
    RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(assault.RuntimeGroupId);
    if (!group)
    {
        return false;
    }

    uint32 living = 0;
    uint32 engaged = 0;
    uint32 newlyEngaged = 0;

    for (RuntimeEntity const& entity : group->Entities)
    {
        if (entity.EntityType != static_cast<uint8>(EntityProviderType::Creature))
        {
            continue;
        }

        Map* map = sMapMgr->FindMap(entity.MapId, 0);
        if (!map)
        {
            continue;
        }

        Creature* creature = map->GetCreature(entity.Guid);
        if (!creature || !creature->IsAlive())
        {
            continue;
        }

        ++living;

        if (creature->IsInCombat() && creature->GetVictim())
        {
            if (Creature* currentVictim = creature->GetVictim()->ToCreature())
            {
                uint32 const currentNpcFlags = currentVictim->GetCreatureTemplate()->npcflag;
                bool const currentIsPolicyTarget =
                    (((assault.TargetPolicy & 1u) != 0) && ((currentNpcFlags & UNIT_NPC_FLAG_QUESTGIVER) != 0)) ||
                    (((assault.TargetPolicy & 2u) != 0) && ((currentNpcFlags & UNIT_NPC_FLAG_VENDOR) != 0)) ||
                    (((assault.TargetPolicy & 4u) != 0) && ((currentNpcFlags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0));

                if (currentIsPolicyTarget)
                {
                    LogCombatDiagnostics(
                        "NEXT-TICK",
                        assault.RuntimeId,
                        entity,
                        creature,
                        currentVictim,
                        true);
                }
            }

            ++engaged;
            continue;
        }

        Creature* target = nullptr;
        float bestDistance = assault.SearchRadius + 1.0f;
        bool targetIsServiceNpc = false;

        // Keep the core's normal hostile acquisition path for defenders that
        // already participate in AzerothCore faction combat.
        if (Unit* hostile = creature->SelectNearestTarget(assault.SearchRadius))
        {
            target = hostile->ToCreature();
            if (target)
            {
                bestDistance = creature->GetDistance(target);
            }
        }

        // Service NPCs such as quest givers/vendors/flight masters can be
        // passive toward the invasion even when they are intended assault
        // targets.  Scan the map's normal spawned-creature store explicitly
        // instead of relying on grid notifier helpers whose API differs across
        // AzerothCore revisions.
        for (auto const& [spawnId, candidate] : map->GetCreatureBySpawnIdStore())
        {
            (void)spawnId;

            if (!candidate || candidate == creature || !candidate->IsAlive())
            {
                continue;
            }

            if (candidate->GetMap() != map)
            {
                continue;
            }

            float const distance = creature->GetDistance(candidate);
            if (distance > assault.SearchRadius)
            {
                continue;
            }

            uint32 const npcFlags = candidate->GetCreatureTemplate()->npcflag;

            // parameter3 is an assault target-policy bitmask:
            //   bit 0 (1) = quest givers
            //   bit 1 (2) = vendors
            //   bit 2 (4) = flight masters
            bool const allowedQuestGiver =
                (assault.TargetPolicy & 1u) != 0 &&
                (npcFlags & UNIT_NPC_FLAG_QUESTGIVER) != 0;
            bool const allowedVendor =
                (assault.TargetPolicy & 2u) != 0 &&
                (npcFlags & UNIT_NPC_FLAG_VENDOR) != 0;
            bool const allowedFlightMaster =
                (assault.TargetPolicy & 4u) != 0 &&
                (npcFlags & UNIT_NPC_FLAG_FLIGHTMASTER) != 0;

            if (!allowedQuestGiver && !allowedVendor && !allowedFlightMaster)
            {
                continue;
            }

            if (candidate->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) ||
                candidate->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
            {
                continue;
            }

            // Explicit service-NPC targets compete normally with ordinary
            // hostile defenders. Whichever valid assault target is nearest wins.
            if (distance >= bestDistance)
            {
                continue;
            }

            target = candidate;
            bestDistance = distance;
            targetIsServiceNpc = true;
        }

        if (!target)
        {
            continue;
        }

        if (creature->AI())
        {
            if (targetIsServiceNpc)
            {
                if (!EnsureServiceNpcAttackable(assault.RuntimeId, target))
                {
                    LOG_WARN("server.loading",
                        "[LWI Assault] Runtime #{} creature {} member {} could not make service target {} GUID {} attackable by NPCs.",
                        assault.RuntimeId,
                        entity.Entry,
                        entity.MemberId,
                        target->GetEntry(),
                        target->GetGUID().ToString());
                    continue;
                }

                LogCombatDiagnostics(
                    "PRE",
                    assault.RuntimeId,
                    entity,
                    creature,
                    target,
                    true);
            }

            creature->AI()->AttackStart(target);

            if (targetIsServiceNpc)
            {
                if (target->AI() && target->CanStartAttack(creature, true))
                {
                    target->AI()->AttackStart(creature);
                }

                LogCombatDiagnostics(
                    "POST-AI-ATTACKSTART",
                    assault.RuntimeId,
                    entity,
                    creature,
                    target,
                    true);
            }

            bool const hasCorrectVictim = creature->GetVictim() == target;

            if (hasCorrectVictim)
            {
                ++engaged;
                ++newlyEngaged;

                LOG_INFO("server.loading",
                    "[LWI Assault] Runtime #{} creature {} member {} SUCCESSFULLY engaged target {} GUID {}{}.",
                    assault.RuntimeId,
                    entity.Entry,
                    entity.MemberId,
                    target->GetEntry(),
                    target->GetGUID().ToString(),
                    targetIsServiceNpc ? " via explicit service-NPC assault policy" : "");
            }
            else
            {
                LOG_WARN("server.loading",
                    "[LWI Assault] Runtime #{} creature {} member {} FAILED to establish target {} GUID {}{}; "
                    "current victim={}, inCombat={}.",
                    assault.RuntimeId,
                    entity.Entry,
                    entity.MemberId,
                    target->GetEntry(),
                    target->GetGUID().ToString(),
                    targetIsServiceNpc ? " via explicit service-NPC assault policy" : "",
                    creature->GetVictim() ? creature->GetVictim()->GetEntry() : 0,
                    creature->IsInCombat());
            }
        }
    }

    if (newlyEngaged != 0)
    {
        LOG_INFO("server.loading",
            "[LWI Assault] Runtime #{} runtime entity group #{} acquired {} new target(s); "
            "{}/{} living invader(s) are engaged.",
            assault.RuntimeId,
            assault.RuntimeGroupId,
            newlyEngaged,
            engaged,
            living);
    }

    return engaged != 0;
}

void AssaultManager::CancelRuntime(uint64 runtimeId)
{
    uint32 removed = 0;

    for (auto itr = _activeAssaults.begin(); itr != _activeAssaults.end();)
    {
        if (itr->second.RuntimeId == runtimeId)
        {
            itr = _activeAssaults.erase(itr);
            ++removed;
        }
        else
        {
            ++itr;
        }
    }

    ReleaseRuntimeOverrides(runtimeId);

    if (removed != 0)
    {
        LOG_INFO("server.loading",
            "[LWI Assault] Cancelled {} active assault behavior(s) for runtime #{}.",
            removed,
            runtimeId);
    }
}
}