#include "MovementController.h"

#include "Creature.h"
#include "IEntityProvider.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "RuntimeEntityGroup.h"
#include "Timer.h"

#include <algorithm>
#include <vector>

namespace lwi
{
namespace
{
constexpr uint32 MovementUpdateIntervalMs = 250;
constexpr float ArrivalTolerance = 2.0f;
constexpr uint32 MovementPointBase = 0x4C570000; // "LW" range; runtime-only point ids.
}

MovementController& MovementController::Instance()
{
    static MovementController instance;
    return instance;
}

void MovementController::Reset()
{
    _activeMovements.clear();
    _updateTimerMs = 0;
}

bool MovementController::StartPath(uint64 runtimeGroupId, uint32 pathId, uint32 profileId)
{
    RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
    if (!group)
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Cannot start path {} because runtime entity group #{} does not exist.",
            pathId, runtimeGroupId);
        return false;
    }

    MovementPathDefinition const* path = sInvasionMgr.GetMovementPath(pathId);
    if (!path)
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} requested missing movement path {}.",
            runtimeGroupId, pathId);
        return false;
    }

    auto const* nodes = sInvasionMgr.GetMovementNodes(pathId);
    if (!nodes || nodes->empty())
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Movement path {} ({}) has no enabled nodes.",
            pathId, path->Name);
        return false;
    }

    if (profileId != 0 && !sInvasionMgr.GetMovementProfile(profileId))
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} requested missing movement profile {}.",
            runtimeGroupId, profileId);
        return false;
    }

    ActiveRuntimeMovement movement;
    movement.RuntimeGroupId = runtimeGroupId;
    movement.RuntimeId = group->RuntimeId;
    movement.PathId = pathId;
    movement.ProfileId = profileId;
    movement.NodeIndex = 0;
    movement.State = RuntimeMovementState::Moving;

    _activeMovements[runtimeGroupId] = movement;

    ActiveRuntimeMovement& active = _activeMovements[runtimeGroupId];
    if (!BeginCurrentNode(active))
    {
        _activeMovements.erase(runtimeGroupId);
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} started path {} ({}) with {} node(s){}.",
        runtimeGroupId,
        pathId,
        path->Name,
        nodes->size(),
        profileId != 0 ? " using a movement profile" : "");

    return true;
}

bool MovementController::CancelGroup(uint64 runtimeGroupId)
{
    auto itr = _activeMovements.find(runtimeGroupId);
    if (itr == _activeMovements.end())
    {
        return false;
    }

    _activeMovements.erase(itr);

    LOG_INFO("server.loading",
        "[LWI Movement] Cancelled movement for runtime entity group #{}.",
        runtimeGroupId);
    return true;
}

void MovementController::CancelRuntime(uint64 runtimeId)
{
    std::vector<uint64> cancelled;

    for (auto const& [runtimeGroupId, movement] : _activeMovements)
    {
        if (movement.RuntimeId == runtimeId)
        {
            cancelled.push_back(runtimeGroupId);
        }
    }

    for (uint64 runtimeGroupId : cancelled)
    {
        _activeMovements.erase(runtimeGroupId);
    }

    if (!cancelled.empty())
    {
        LOG_INFO("server.loading",
            "[LWI Movement] Cancelled {} active movement(s) for runtime #{}.",
            cancelled.size(), runtimeId);
    }
}

bool MovementController::IsGroupMoving(uint64 runtimeGroupId) const
{
    return _activeMovements.find(runtimeGroupId) != _activeMovements.end();
}

void MovementController::Update(uint32 diff)
{
    if (_activeMovements.empty())
    {
        return;
    }

    if (_updateTimerMs > diff)
    {
        _updateTimerMs -= diff;
        return;
    }

    _updateTimerMs = MovementUpdateIntervalMs;
    uint64 const nowMs = static_cast<uint64>(getMSTime());
    std::vector<uint64> completed;

    for (auto& [runtimeGroupId, movement] : _activeMovements)
    {
        RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
        if (!group || group->State != RuntimeEntityGroupState::Active)
        {
            completed.push_back(runtimeGroupId);
            continue;
        }

        if (movement.State == RuntimeMovementState::Completed)
        {
            completed.push_back(runtimeGroupId);
            continue;
        }

        if (movement.State == RuntimeMovementState::Waiting)
        {
            if (nowMs >= movement.WaitEndsAtMs)
            {
                AdvanceOrComplete(runtimeGroupId, movement, nowMs);
                if (movement.State == RuntimeMovementState::Completed)
                {
                    completed.push_back(runtimeGroupId);
                }
            }
            continue;
        }

        if (!HasGroupReachedCurrentNode(movement))
        {
            continue;
        }

        auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);
        if (!nodes || movement.NodeIndex >= nodes->size())
        {
            completed.push_back(runtimeGroupId);
            continue;
        }

        MovementNodeDefinition const& node = (*nodes)[movement.NodeIndex];
        if (node.WaitMs > 0)
        {
            movement.State = RuntimeMovementState::Waiting;
            movement.WaitEndsAtMs = nowMs + node.WaitMs;

            LOG_INFO("server.loading",
                "[LWI Movement] Runtime entity group #{} reached node {} on path {} and will wait {} ms.",
                runtimeGroupId, node.NodeOrder, movement.PathId, node.WaitMs);
        }
        else
        {
            AdvanceOrComplete(runtimeGroupId, movement, nowMs);
            if (movement.State == RuntimeMovementState::Completed)
            {
                completed.push_back(runtimeGroupId);
            }
        }
    }

    for (uint64 runtimeGroupId : completed)
    {
        _activeMovements.erase(runtimeGroupId);
    }
}

bool MovementController::BeginCurrentNode(ActiveRuntimeMovement& movement)
{
    RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(movement.RuntimeGroupId);
    auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);

    if (!group || !nodes || movement.NodeIndex >= nodes->size())
    {
        return false;
    }

    MovementNodeDefinition const& node = (*nodes)[movement.NodeIndex];

    uint32 profileId = node.ProfileOverrideId != 0 ? node.ProfileOverrideId : movement.ProfileId;
    MovementProfileDefinition const* profile = profileId != 0
        ? sInvasionMgr.GetMovementProfile(profileId)
        : nullptr;

    uint32 moved = 0;

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

        if (profile)
        {
            // 0 = provider/default, 1 = walk, 2 = run.
            if (profile->DefaultMode == 1)
            {
                creature->SetWalk(true);
            }
            else if (profile->DefaultMode == 2)
            {
                creature->SetWalk(false);
            }
        }

        uint32 pointId = MovementPointBase
            + (static_cast<uint32>(movement.NodeIndex) & 0xFFFFU);

        creature->GetMotionMaster()->MovePoint(
            pointId,
            node.X,
            node.Y,
            node.Z);

        ++moved;
    }

    if (moved == 0)
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} has no living movable creature entities for path {} node {}.",
            movement.RuntimeGroupId, movement.PathId, node.NodeOrder);
        return false;
    }

    movement.State = RuntimeMovementState::Moving;
    movement.WaitEndsAtMs = 0;

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} moving {} creature(s) to path {} node {} ({:.2f}, {:.2f}, {:.2f}).",
        movement.RuntimeGroupId,
        moved,
        movement.PathId,
        node.NodeOrder,
        node.X,
        node.Y,
        node.Z);

    return true;
}

bool MovementController::HasGroupReachedCurrentNode(ActiveRuntimeMovement const& movement) const
{
    RuntimeEntityGroup const* group = sRuntimeEntityGroupMgr.GetGroup(movement.RuntimeGroupId);
    auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);

    if (!group || !nodes || movement.NodeIndex >= nodes->size())
    {
        return false;
    }

    MovementNodeDefinition const& node = (*nodes)[movement.NodeIndex];
    uint32 movable = 0;

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

        ++movable;

        if (creature->GetDistance(node.X, node.Y, node.Z) > ArrivalTolerance)
        {
            return false;
        }
    }

    return movable > 0;
}

void MovementController::AdvanceOrComplete(
    uint64 runtimeGroupId,
    ActiveRuntimeMovement& movement,
    uint64 /*nowMs*/)
{
    auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);
    if (!nodes)
    {
        movement.State = RuntimeMovementState::Completed;
        return;
    }

    ++movement.NodeIndex;

    if (movement.NodeIndex >= nodes->size())
    {
        CompleteMovement(runtimeGroupId, movement);
        return;
    }

    if (!BeginCurrentNode(movement))
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} failed to begin the next node on path {}.",
            runtimeGroupId, movement.PathId);
        movement.State = RuntimeMovementState::Completed;
    }
}

void MovementController::CompleteMovement(
    uint64 runtimeGroupId,
    ActiveRuntimeMovement& movement)
{
    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} completed movement path {}.",
        runtimeGroupId, movement.PathId);

    movement.State = RuntimeMovementState::Completed;
}
}
