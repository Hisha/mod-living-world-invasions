#include "MovementController.h"

#include "Creature.h"
#include "AnnouncementManager.h"
#include "DialogueManager.h"
#include "IEntityProvider.h"
#include "LivingWorldInvasions.h"
#include "InvasionRuntimeManager.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "RuntimeEntityGroup.h"
#include "RuntimeSignalManager.h"
#include "SoundManager.h"
#include "Timer.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace lwi
{
namespace
{
constexpr uint32 MovementUpdateIntervalMs = 250;
constexpr float ArrivalTolerance = 2.0f;
constexpr float FormationArrivalTolerance = 25.0f;
constexpr uint32 FormationArrivalPercent = 75;

// Large invasion groups should march as a compact road column instead of allowing
// role-slot offsets to expand indefinitely sideways. Six abreast keeps a 100-creature
// force narrow enough for normal roads while allowing the formation to grow backward.
constexpr uint32 MarchingColumns = 6;
constexpr float MarchingColumnSpacing = 2.4f;
constexpr float MarchingRowSpacing = 2.4f;

struct FormationOffset
{
    float Forward = 0.0f;
    float Right = 0.0f;
};

uint8 GetRoleSortOrder(uint8 tacticalRole)
{
    switch (static_cast<TacticalRole>(tacticalRole))
    {
        case TacticalRole::Commander: return 0;
        case TacticalRole::Protector: return 1;
        case TacticalRole::MeleeDps:  return 2;
        case TacticalRole::RangedDps: return 3;
        case TacticalRole::Healer:    return 4;
        case TacticalRole::Support:   return 5;
        case TacticalRole::Default:
        default:                      return 6;
    }
}

FormationOffset GetFormationOffset(uint8 tacticalRole, uint32 formationSlot)
{
    TacticalRole const role = static_cast<TacticalRole>(tacticalRole);

    // The commander owns the front-center position. Any additional commanders
    // will also remain near the front rather than being pushed to the rear.
    if (role == TacticalRole::Commander)
    {
        return { 0.0f, 0.0f };
    }

    uint32 const column = formationSlot % MarchingColumns;
    uint32 const row = formationSlot / MarchingColumns + 1U;

    float const centeredColumn =
        static_cast<float>(column) - (static_cast<float>(MarchingColumns) - 1.0f) * 0.5f;

    return
    {
        -static_cast<float>(row) * MarchingRowSpacing,
        centeredColumn * MarchingColumnSpacing
    };
}

void BuildFormationDestination(
    MovementNodeDefinition const& node,
    float formationOrientation,
    uint8 tacticalRole,
    uint32 formationSlot,
    float& x,
    float& y,
    float& z)
{
    FormationOffset const offset = GetFormationOffset(tacticalRole, formationSlot);

    float const forwardX = std::cos(formationOrientation);
    float const forwardY = std::sin(formationOrientation);
    float const rightX = -forwardY;
    float const rightY = forwardX;

    x = node.X + forwardX * offset.Forward + rightX * offset.Right;
    y = node.Y + forwardY * offset.Forward + rightY * offset.Right;
    z = node.Z;
}

void ExecuteNodeActions(ActiveRuntimeMovement const& movement, RuntimeEntityGroup const& group, MovementNodeDefinition const& node)
{
    auto const* actions = sInvasionMgr.GetMovementNodeActions(node.Id);
    if (!actions || actions->empty())
    {
        return;
    }

    InvasionRuntime const* runtime = sInvasionRuntimeMgr.GetRuntime(movement.RuntimeId);

    for (MovementNodeActionDefinition const& action : *actions)
    {
        bool success = false;

        switch (action.ActionType)
        {
            case 1: // Dialogue: target_id=dialogue, parameter1=speaker member id.
                success = sDialogueManager.Execute(
                    movement.RuntimeId,
                    group.SpawnGroupId,
                    action.TargetId,
                    action.Parameter1);
                break;

            case 2: // Announcement: target_id=announcement, p1=scope, p2=scope id, p3=faction.
                if (!runtime)
                {
                    LOG_ERROR("server.loading",
                        "[LWI Movement] Runtime entity group #{} cannot execute announcement node action {} because runtime #{} was not found.",
                        movement.RuntimeGroupId, action.Id, movement.RuntimeId);
                    continue;
                }

                success = sAnnouncementManager.Execute(
                    movement.RuntimeId,
                    runtime->GetInvasionId(),
                    action.TargetId,
                    action.Parameter1,
                    action.Parameter2,
                    action.Parameter3);
                break;

            case 3: // Sound: target_id=sound, parameter1=source member id, parameter2=playback mode.
                success = sSoundManager.Execute(
                    movement.RuntimeId,
                    group.SpawnGroupId,
                    action.TargetId,
                    action.Parameter1,
                    action.Parameter2);
                break;

            default:
                LOG_ERROR("server.loading",
                    "[LWI Movement] Runtime entity group #{} encountered unsupported node action type {} for action {}.",
                    movement.RuntimeGroupId, action.ActionType, action.Id);
                continue;
        }

        if (!success)
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} failed node action {} type {} at path {} node {}.",
                movement.RuntimeGroupId, action.Id, action.ActionType, movement.PathId, node.NodeOrder);
            continue;
        }

        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} executed node action {} type {} at path {} node {}.",
            movement.RuntimeGroupId, action.Id, action.ActionType, movement.PathId, node.NodeOrder);
    }
}
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

bool MovementController::StartPath(uint64 runtimeGroupId, uint32 pathId, uint32 profileId, uint32 completionSignalId, MovementDirection direction)
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

    if (completionSignalId != 0 && !sInvasionMgr.GetRuntimeSignal(completionSignalId))
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} requested missing completion signal {}.",
            runtimeGroupId, completionSignalId);
        return false;
    }

    ActiveRuntimeMovement movement;
    movement.RuntimeGroupId = runtimeGroupId;
    movement.RuntimeId = group->RuntimeId;
    movement.PathId = pathId;
    movement.ProfileId = profileId;
    movement.CompletionSignalId = completionSignalId;
    movement.Direction = direction;
    movement.NodeIndex = direction == MovementDirection::Forward ? 0 : nodes->size() - 1;
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


bool MovementController::StartRouteSegment(
    uint64 runtimeGroupId,
    uint32 routeSegmentId,
    uint32 fromNodeId,
    uint32 profileId,
    uint32 completionSignalId)
{
    RouteSegmentDefinition const* segment = sInvasionMgr.GetRouteSegment(routeSegmentId);
    if (!segment)
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} requested missing route segment {}.",
            runtimeGroupId, routeSegmentId);
        return false;
    }

    MovementDirection direction;
    if (fromNodeId == segment->StartNodeId)
    {
        direction = MovementDirection::Forward;
    }
    else if (fromNodeId == segment->EndNodeId)
    {
        direction = MovementDirection::Reverse;
    }
    else
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} cannot enter route segment {} from route node {}; "
            "expected start node {} or end node {}.",
            runtimeGroupId,
            routeSegmentId,
            fromNodeId,
            segment->StartNodeId,
            segment->EndNodeId);
        return false;
    }

    return StartPath(runtimeGroupId, segment->MovementPathId, profileId, completionSignalId, direction);
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

        uint32 livingCreatures = 0;
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
            if (creature && creature->IsAlive())
            {
                ++livingCreatures;
            }
        }

        if (livingCreatures == 0)
        {
            LOG_INFO("server.loading",
                "[LWI Movement] Runtime entity group #{} was defeated while following path {}. "
                "Cancelling movement for this group without failing runtime #{}.",
                runtimeGroupId,
                movement.PathId,
                movement.RuntimeId);

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

        for (RuntimeMovementDestination& destination : movement.Destinations)
        {
            Map* map = sMapMgr->FindMap(destination.MapId, 0);
            if (!map)
            {
                continue;
            }

            Creature* creature = map->GetCreature(destination.Guid);
            if (creature && creature->IsAlive() && creature->IsInCombat())
            {
                destination.WasInCombat = true;
            }
        }

        ResumeInterruptedCreatures(movement);

        if (!HasGroupReachedCurrentNode(movement, nowMs))
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

        RuntimeEntityGroup* arrivedGroup = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
        if (arrivedGroup)
        {
            ExecuteNodeActions(movement, *arrivedGroup, node);
        }

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

    // Align the marching column with the route itself, not with whatever facing
    // happened to be recorded on the waypoint. For normal nodes use the incoming
    // segment; for the first node use the outgoing segment. This keeps the column
    // centered on and trailing along the road.
    float formationOrientation = node.Orientation;
    if (movement.Direction == MovementDirection::Forward)
    {
        if (movement.NodeIndex > 0)
        {
            MovementNodeDefinition const& previous = (*nodes)[movement.NodeIndex - 1];
            formationOrientation = std::atan2(node.Y - previous.Y, node.X - previous.X);
        }
        else if (nodes->size() > 1)
        {
            MovementNodeDefinition const& next = (*nodes)[1];
            formationOrientation = std::atan2(next.Y - node.Y, next.X - node.X);
        }
    }
    else
    {
        if (movement.NodeIndex + 1 < nodes->size())
        {
            MovementNodeDefinition const& previous = (*nodes)[movement.NodeIndex + 1];
            formationOrientation = std::atan2(node.Y - previous.Y, node.X - previous.X);
        }
        else if (movement.NodeIndex > 0)
        {
            MovementNodeDefinition const& next = (*nodes)[movement.NodeIndex - 1];
            formationOrientation = std::atan2(next.Y - node.Y, next.X - node.X);
        }
    }

    uint32 profileId = node.ProfileOverrideId != 0 ? node.ProfileOverrideId : movement.ProfileId;
    MovementProfileDefinition const* profile = profileId != 0
        ? sInvasionMgr.GetMovementProfile(profileId)
        : nullptr;

    std::vector<RuntimeEntity const*> creatures;
    creatures.reserve(group->Entities.size());

    for (RuntimeEntity const& entity : group->Entities)
    {
        if (entity.EntityType == static_cast<uint8>(EntityProviderType::Creature))
        {
            creatures.push_back(&entity);
        }
    }

    // Put commanders first, then front-line combat roles, then ranged/healing/support
    // roles. The actual slot geometry is one compact six-wide marching column.
    std::stable_sort(creatures.begin(), creatures.end(), [](RuntimeEntity const* left, RuntimeEntity const* right)
    {
        uint8 const leftOrder = GetRoleSortOrder(left->TacticalRole);
        uint8 const rightOrder = GetRoleSortOrder(right->TacticalRole);

        if (leftOrder != rightOrder)
        {
            return leftOrder < rightOrder;
        }

        if (left->MemberId != right->MemberId)
        {
            return left->MemberId < right->MemberId;
        }

        return false;
    });

    uint32 moved = 0;
    uint32 tracked = 0;
    uint32 formationSlot = 0;
    movement.Destinations.clear();
    movement.ArrivalGraceStartedAtMs = 0;

    for (RuntimeEntity const* entityPtr : creatures)
    {
        RuntimeEntity const& entity = *entityPtr;

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

        uint32 slot = formationSlot;
        if (static_cast<TacticalRole>(entity.TacticalRole) != TacticalRole::Commander)
        {
            ++formationSlot;
        }

        float targetX = node.X;
        float targetY = node.Y;
        float targetZ = node.Z;
        BuildFormationDestination(node, formationOrientation, entity.TacticalRole, slot, targetX, targetY, targetZ);

        PathGenerator path(creature);
        bool pathFound = path.CalculatePath(targetX, targetY, targetZ, false);

        // Formation slots near terrain edges can occasionally miss the navmesh.
        // Fall back to the authored node center rather than dropping that creature
        // from the movement controller entirely.
        if (!pathFound || (path.GetPathType() & PATHFIND_NOPATH))
        {
            LOG_INFO("server.loading",
                "[LWI Movement] Runtime entity group #{} creature {} member {} could not path to compact formation slot "
                "for path {} node {}; retrying authored node center.",
                movement.RuntimeGroupId,
                entity.Entry,
                entity.MemberId,
                movement.PathId,
                node.NodeOrder);

            PathGenerator fallback(creature);
            pathFound = fallback.CalculatePath(node.X, node.Y, node.Z, false);

            if (!pathFound || (fallback.GetPathType() & PATHFIND_NOPATH))
            {
                LOG_ERROR("server.loading",
                    "[LWI Movement] Runtime entity group #{} creature {} member {} could not find an MMAP path "
                    "to path {} node {} or its formation slot; creature skipped for this node.",
                    movement.RuntimeGroupId,
                    entity.Entry,
                    entity.MemberId,
                    movement.PathId,
                    node.NodeOrder);
                continue;
            }

            Movement::PointsArray fallbackPoints = fallback.GetPath();
            if (fallbackPoints.size() < 2)
            {
                continue;
            }

            G3D::Vector3 const& actualEnd = fallbackPoints.back();

            RuntimeMovementDestination destination;
            destination.Guid = entity.Guid;
            destination.MapId = entity.MapId;
            destination.X = actualEnd.x;
            destination.Y = actualEnd.y;
            destination.Z = actualEnd.z;
            destination.WasInCombat = creature->IsInCombat() || creature->GetVictim();
            movement.Destinations.push_back(destination);
            ++tracked;

            if (!destination.WasInCombat)
            {
                creature->GetMotionMaster()->MoveSplinePath(&fallbackPoints, FORCED_MOVEMENT_NONE);
                ++moved;
            }

            continue;
        }

        Movement::PointsArray pathPoints = path.GetPath();
        if (pathPoints.size() < 2)
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} creature {} member {} produced an unusable MMAP path "
                "with {} point(s) for path {} node {}; creature skipped for this node.",
                movement.RuntimeGroupId,
                entity.Entry,
                entity.MemberId,
                pathPoints.size(),
                movement.PathId,
                node.NodeOrder);
            continue;
        }

        G3D::Vector3 const& actualEnd = pathPoints.back();

        RuntimeMovementDestination destination;
        destination.Guid = entity.Guid;
        destination.MapId = entity.MapId;
        destination.X = actualEnd.x;
        destination.Y = actualEnd.y;
        destination.Z = actualEnd.z;
        destination.WasInCombat = creature->IsInCombat() || creature->GetVictim();
        movement.Destinations.push_back(destination);
        ++tracked;

        // Do not stomp chase/combat movement. The destination is still updated to
        // this newest node, so ResumeInterruptedCreatures() sends the creature to
        // the current column position as soon as combat ends instead of back to an
        // obsolete node.
        if (destination.WasInCombat)
        {
            continue;
        }

        creature->GetMotionMaster()->MoveSplinePath(
            &pathPoints,
            FORCED_MOVEMENT_NONE);

        ++moved;
    }

    if (tracked == 0)
    {
        LOG_ERROR("server.loading",
            "[LWI Movement] Runtime entity group #{} has no living trackable creature entities for path {} node {}.",
            movement.RuntimeGroupId, movement.PathId, node.NodeOrder);
        return false;
    }

    movement.State = RuntimeMovementState::Moving;
    movement.WaitEndsAtMs = 0;

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} tracking {} creature(s), immediately moving {} creature(s) "
        "in a compact {}-wide marching column to path {} node {} ({:.2f}, {:.2f}, {:.2f}).",
        movement.RuntimeGroupId,
        tracked,
        moved,
        MarchingColumns,
        movement.PathId,
        node.NodeOrder,
        node.X,
        node.Y,
        node.Z);

    return true;
}

void MovementController::ResumeInterruptedCreatures(ActiveRuntimeMovement& movement)
{
    auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);
    if (!nodes || movement.NodeIndex >= nodes->size())
    {
        return;
    }

    MovementNodeDefinition const& node = (*nodes)[movement.NodeIndex];

    for (RuntimeMovementDestination& destination : movement.Destinations)
    {
        Map* map = sMapMgr->FindMap(destination.MapId, 0);
        if (!map)
        {
            continue;
        }

        Creature* creature = map->GetCreature(destination.Guid);
        if (!creature || !creature->IsAlive())
        {
            continue;
        }

        // Combat/assault owns this creature's movement while it is actively in
        // combat OR still has an assigned victim. Do not replace AI/chase
        // movement with the invasion route until both states are clear.
        if (creature->IsInCombat() || creature->GetVictim())
        {
            continue;
        }

        if (creature->GetDistance(destination.X, destination.Y, destination.Z) <= FormationArrivalTolerance)
        {
            destination.WasInCombat = false;
            continue;
        }

        // Only resume a creature that this controller actually observed in combat.
        // This prevents us from continually replacing normal movement while the
        // original MMAP spline is still active.
        if (!destination.WasInCombat)
        {
            continue;
        }

        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} creature {} eligible to resume path {} node {}: "
            "inCombat={}, hasVictim={}, wasInCombat={}, distance={:.2f}.",
            movement.RuntimeGroupId,
            creature->GetEntry(),
            movement.PathId,
            node.NodeOrder,
            creature->IsInCombat(),
            creature->GetVictim() != nullptr,
            destination.WasInCombat,
            creature->GetDistance(destination.X, destination.Y, destination.Z));

        PathGenerator path(creature);
        bool const pathFound = path.CalculatePath(
            destination.X,
            destination.Y,
            destination.Z,
            false);

        if (!pathFound || (path.GetPathType() & PATHFIND_NOPATH))
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} creature {} could not resume MMAP movement "
                "toward path {} node {} after combat.",
                movement.RuntimeGroupId,
                creature->GetEntry(),
                movement.PathId,
                node.NodeOrder);
            destination.WasInCombat = false;
            continue;
        }

        Movement::PointsArray pathPoints = path.GetPath();
        if (pathPoints.size() < 2)
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} creature {} produced an unusable MMAP path "
                "while resuming path {} node {} after combat.",
                movement.RuntimeGroupId,
                creature->GetEntry(),
                movement.PathId,
                node.NodeOrder);
            destination.WasInCombat = false;
            continue;
        }

        G3D::Vector3 const& actualEnd = pathPoints.back();
        destination.X = actualEnd.x;
        destination.Y = actualEnd.y;
        destination.Z = actualEnd.z;
        destination.WasInCombat = false;

        creature->GetMotionMaster()->MoveSplinePath(
            &pathPoints,
            FORCED_MOVEMENT_NONE);

        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} creature {} resumed MMAP movement toward "
            "path {} node {} after combat.",
            movement.RuntimeGroupId,
            creature->GetEntry(),
            movement.PathId,
            node.NodeOrder);
    }
}

bool MovementController::HasGroupReachedCurrentNode(
    ActiveRuntimeMovement& movement,
    uint64 /*nowMs*/)
{
    if (movement.Destinations.empty())
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    auto const* nodes = sInvasionMgr.GetMovementNodes(movement.PathId);
    if (!nodes || movement.NodeIndex >= nodes->size())
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    MovementNodeDefinition const& node = (*nodes)[movement.NodeIndex];
    bool const isFinalNode =
        movement.Direction == MovementDirection::Forward
            ? movement.NodeIndex + 1 >= nodes->size()
            : movement.NodeIndex == 0;

    uint32 living = 0;
    uint32 formationArrivals = 0;

    for (RuntimeMovementDestination const& destination : movement.Destinations)
    {
        Map* map = sMapMgr->FindMap(destination.MapId, 0);
        if (!map)
        {
            continue;
        }

        Creature* creature = map->GetCreature(destination.Guid);
        if (!creature || !creature->IsAlive())
        {
            continue;
        }

        ++living;

        if (creature->GetDistance(destination.X, destination.Y, destination.Z) <= FormationArrivalTolerance)
        {
            ++formationArrivals;
        }
    }

    if (living == 0)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    uint32 const requiredArrivals =
        std::max<uint32>(1, (living * FormationArrivalPercent + 99) / 100);

    // The final node uses the same formation-destination test as intermediate nodes.
    // Large groups can legitimately extend well beyond a fixed radius around the
    // authored node center, so requiring most of the force to crowd inside one
    // objective circle can prevent route completion even when the formation has
    // actually arrived.
    if (isFinalNode)
    {
        if (formationArrivals < requiredArrivals)
        {
            return false;
        }

        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} reached final formation for path {} node {}: "
            "{}/{} surviving creature(s) within {:.1f} yards of their final formation destinations.",
            movement.RuntimeGroupId,
            movement.PathId,
            node.NodeOrder,
            formationArrivals,
            living,
            FormationArrivalTolerance);

        movement.ArrivalGraceStartedAtMs = 0;
        return true;
    }

    // Ordinary road nodes are pass-through waypoints. As soon as the authored
    // threshold of the surviving force reaches the current formation area, issue
    // the next node immediately. There is intentionally no regroup grace and combat
    // among stragglers does not hold the entire column at the old waypoint.
    if (formationArrivals < requiredArrivals)
    {
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} flowing through path {} node {} with "
        "{}/{} surviving creature(s) within {:.1f} yards; advancing without regroup pause.",
        movement.RuntimeGroupId,
        movement.PathId,
        node.NodeOrder,
        formationArrivals,
        living,
        FormationArrivalTolerance);

    movement.ArrivalGraceStartedAtMs = 0;
    return true;
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

    if (movement.Direction == MovementDirection::Forward)
    {
        ++movement.NodeIndex;
        if (movement.NodeIndex >= nodes->size())
        {
            CompleteMovement(runtimeGroupId, movement);
            return;
        }
    }
    else
    {
        if (movement.NodeIndex == 0)
        {
            CompleteMovement(runtimeGroupId, movement);
            return;
        }

        --movement.NodeIndex;
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
    // The final role-aware/MMAP destination becomes each surviving creature's
    // new home position. If combat later pulls an invader away and its AI
    // evades/resets, AzerothCore will return it to the objective it actually
    // reached instead of the group's original spawn/staging position.
    uint32 homesUpdated = 0;

    for (RuntimeMovementDestination const& destination : movement.Destinations)
    {
        Map* map = sMapMgr->FindMap(destination.MapId, 0);
        if (!map)
        {
            continue;
        }

        Creature* creature = map->GetCreature(destination.Guid);
        if (!creature || !creature->IsAlive())
        {
            continue;
        }

        creature->SetHomePosition(
            destination.X,
            destination.Y,
            destination.Z,
            creature->GetOrientation());

        ++homesUpdated;
    }

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} completed movement path {}; "
        "updated {} surviving creature home position(s) to their final formation destinations.",
        runtimeGroupId, movement.PathId, homesUpdated);

    if (movement.CompletionSignalId != 0)
    {
        if (!sRuntimeSignalMgr.EmitSignal(movement.RuntimeId, movement.CompletionSignalId))
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} failed to emit completion signal {} for runtime #{}.",
                runtimeGroupId, movement.CompletionSignalId, movement.RuntimeId);
        }
    }

    movement.State = RuntimeMovementState::Completed;
}
}
