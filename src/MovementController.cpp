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
#include <queue>
#include <unordered_map>
#include <vector>

namespace lwi
{
namespace
{
constexpr uint32 MovementUpdateIntervalMs = 250;
constexpr float ArrivalTolerance = 2.0f;
constexpr float RouteArrivalTolerance = 0.75f;
constexpr float FormationArrivalTolerance = 10.0f;
constexpr uint32 FormationArrivalPercent = 75;
constexpr uint32 FormationArrivalGraceMs = 3000;
constexpr float FinalObjectiveArrivalRadius = 20.0f;
constexpr float Pi = 3.14159265358979323846f;

struct FormationOffset
{
    float Forward = 0.0f;
    float Right = 0.0f;
};

FormationOffset GetFormationOffset(uint8 tacticalRole, uint32 roleSlot)
{
    TacticalRole const role = static_cast<TacticalRole>(tacticalRole);
    float const side = (roleSlot % 2U == 0U ? -1.0f : 1.0f);
    float const rank = static_cast<float>(roleSlot / 2U);

    switch (role)
    {
        case TacticalRole::Commander:
            return { 0.0f, side * rank * 1.5f };
        case TacticalRole::Protector:
            return { 2.5f + rank, side * (1.5f + rank * 0.75f) };
        case TacticalRole::MeleeDps:
            return { 4.0f + rank, side * (2.0f + rank * 1.25f) };
        case TacticalRole::RangedDps:
            return { -4.0f - rank, side * (2.5f + rank * 1.5f) };
        case TacticalRole::Healer:
            return { -6.0f - rank, side * (1.5f + rank) };
        case TacticalRole::Support:
            return { -5.0f - rank, side * (4.0f + rank) };
        case TacticalRole::Default:
        default:
            return { 0.0f, 0.0f };
    }
}

void BuildFormationDestination(
    MovementNodeDefinition const& node,
    MovementDirection direction,
    uint8 tacticalRole,
    uint32 roleSlot,
    float& x,
    float& y,
    float& z)
{
    FormationOffset const offset = GetFormationOffset(tacticalRole, roleSlot);

    float const travelOrientation = node.Orientation +
        (direction == MovementDirection::Reverse ? Pi : 0.0f);
    float const forwardX = std::cos(travelOrientation);
    float const forwardY = std::sin(travelOrientation);
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
    _activeRouteJourneys.clear();
    _updateTimerMs = 0;
}

bool MovementController::StartPath(
    uint64 runtimeGroupId,
    uint32 pathId,
    uint32 profileId,
    uint32 completionSignalId,
    MovementDirection direction,
    bool routeMovement)
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
    movement.RouteMovement = routeMovement;
    movement.NodeIndex = direction == MovementDirection::Reverse ? nodes->size() - 1 : 0;
    movement.State = RuntimeMovementState::Moving;

    _activeMovements[runtimeGroupId] = movement;

    ActiveRuntimeMovement& active = _activeMovements[runtimeGroupId];
    if (!BeginCurrentNode(active))
    {
        _activeMovements.erase(runtimeGroupId);
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} started path {} ({}) {} with {} node(s){}.",
        runtimeGroupId,
        pathId,
        path->Name,
        direction == MovementDirection::Reverse ? "in reverse" : "forward",
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
            "[LWI Route] Runtime entity group #{} requested missing route segment {}.",
            runtimeGroupId, routeSegmentId);
        return false;
    }

    MovementDirection direction = MovementDirection::Forward;
    uint32 destinationNodeId = 0;

    if (fromNodeId == segment->StartNodeId)
    {
        direction = MovementDirection::Forward;
        destinationNodeId = segment->EndNodeId;
    }
    else if (fromNodeId == segment->EndNodeId)
    {
        direction = MovementDirection::Reverse;
        destinationNodeId = segment->StartNodeId;
    }
    else
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Runtime entity group #{} cannot start route segment {} ({}) from route node {} because the segment connects nodes {} and {}.",
            runtimeGroupId,
            segment->Id,
            segment->Name,
            fromNodeId,
            segment->StartNodeId,
            segment->EndNodeId);
        return false;
    }

    RouteNodeDefinition const* fromNode = sInvasionMgr.GetRouteNode(fromNodeId);
    RouteNodeDefinition const* destinationNode = sInvasionMgr.GetRouteNode(destinationNodeId);
    if (!fromNode || !destinationNode)
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Route segment {} ({}) references unavailable route node data; movement was not started.",
            segment->Id, segment->Name);
        return false;
    }

    if (!StartPath(
            runtimeGroupId,
            segment->MovementPathId,
            profileId,
            completionSignalId,
            direction,
            true))
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Runtime entity group #{} failed to start route segment {} ({}) from {} to {} using movement path {}.",
            runtimeGroupId,
            segment->Id,
            segment->Name,
            fromNode->Name,
            destinationNode->Name,
            segment->MovementPathId);
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Route] Runtime entity group #{} started route segment {} ({}) from {} to {} using movement path {} {}.",
        runtimeGroupId,
        segment->Id,
        segment->Name,
        fromNode->Name,
        destinationNode->Name,
        segment->MovementPathId,
        direction == MovementDirection::Reverse ? "in reverse" : "forward");

    return true;
}

bool MovementController::BuildRouteJourney(
    uint32 fromNodeId,
    uint32 destinationNodeId,
    std::vector<RouteJourneyStep>& steps) const
{
    steps.clear();

    if (fromNodeId == destinationNodeId)
    {
        return false;
    }

    if (!sInvasionMgr.GetRouteNode(fromNodeId) || !sInvasionMgr.GetRouteNode(destinationNodeId))
    {
        return false;
    }

    struct PreviousHop
    {
        uint32 PreviousNodeId = 0;
        uint32 SegmentId = 0;
    };

    std::queue<uint32> pending;
    std::unordered_map<uint32, PreviousHop> previous;

    previous.emplace(fromNodeId, PreviousHop{});
    pending.push(fromNodeId);

    while (!pending.empty())
    {
        uint32 const currentNodeId = pending.front();
        pending.pop();

        if (currentNodeId == destinationNodeId)
        {
            break;
        }

        for (auto const& [segmentId, segment] : sInvasionMgr.GetRouteSegments())
        {
            uint32 nextNodeId = 0;

            if (segment.StartNodeId == currentNodeId)
            {
                nextNodeId = segment.EndNodeId;
            }
            else if (segment.EndNodeId == currentNodeId)
            {
                nextNodeId = segment.StartNodeId;
            }
            else
            {
                continue;
            }

            if (previous.find(nextNodeId) != previous.end())
            {
                continue;
            }

            previous.emplace(nextNodeId, PreviousHop{ currentNodeId, segmentId });
            pending.push(nextNodeId);
        }
    }

    if (previous.find(destinationNodeId) == previous.end())
    {
        return false;
    }

    uint32 currentNodeId = destinationNodeId;
    while (currentNodeId != fromNodeId)
    {
        auto const previousItr = previous.find(currentNodeId);
        if (previousItr == previous.end() || previousItr->second.SegmentId == 0)
        {
            steps.clear();
            return false;
        }

        RouteJourneyStep step;
        step.SegmentId = previousItr->second.SegmentId;
        step.FromNodeId = previousItr->second.PreviousNodeId;
        step.ToNodeId = currentNodeId;
        steps.push_back(step);

        currentNodeId = previousItr->second.PreviousNodeId;
    }

    std::reverse(steps.begin(), steps.end());
    return !steps.empty();
}

bool MovementController::StartRouteJourney(
    uint64 runtimeGroupId,
    uint32 fromNodeId,
    uint32 destinationNodeId,
    uint32 profileId,
    uint32 completionSignalId)
{
    RuntimeEntityGroup* group = sRuntimeEntityGroupMgr.GetGroup(runtimeGroupId);
    if (!group)
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Cannot start route journey because runtime entity group #{} does not exist.",
            runtimeGroupId);
        return false;
    }

    RouteNodeDefinition const* fromNode = sInvasionMgr.GetRouteNode(fromNodeId);
    RouteNodeDefinition const* destinationNode = sInvasionMgr.GetRouteNode(destinationNodeId);
    if (!fromNode || !destinationNode)
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Runtime entity group #{} requested a route journey using unavailable node(s) {} -> {}.",
            runtimeGroupId, fromNodeId, destinationNodeId);
        return false;
    }

    if (profileId != 0 && !sInvasionMgr.GetMovementProfile(profileId))
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Runtime entity group #{} requested missing movement profile {} for route journey.",
            runtimeGroupId, profileId);
        return false;
    }

    if (completionSignalId != 0 && !sInvasionMgr.GetRuntimeSignal(completionSignalId))
    {
        LOG_ERROR("server.loading",
            "[LWI Route] Runtime entity group #{} requested missing completion signal {} for route journey.",
            runtimeGroupId, completionSignalId);
        return false;
    }

    std::vector<RouteJourneyStep> steps;
    if (!BuildRouteJourney(fromNodeId, destinationNodeId, steps))
    {
        LOG_ERROR("server.loading",
            "[LWI Route] No connected route exists from node {} ({}) to node {} ({}).",
            fromNodeId, fromNode->Name, destinationNodeId, destinationNode->Name);
        return false;
    }

    ActiveRouteJourney journey;
    journey.RuntimeGroupId = runtimeGroupId;
    journey.RuntimeId = group->RuntimeId;
    journey.StartNodeId = fromNodeId;
    journey.DestinationNodeId = destinationNodeId;
    journey.ProfileId = profileId;
    journey.CompletionSignalId = completionSignalId;
    journey.StepIndex = 0;
    journey.Steps = std::move(steps);

    _activeRouteJourneys[runtimeGroupId] = std::move(journey);

    ActiveRouteJourney const& activeJourney = _activeRouteJourneys[runtimeGroupId];
    RouteJourneyStep const& firstStep = activeJourney.Steps.front();
    uint32 const firstCompletionSignalId = activeJourney.Steps.size() == 1
        ? activeJourney.CompletionSignalId
        : 0;

    if (!StartRouteSegment(
            runtimeGroupId,
            firstStep.SegmentId,
            firstStep.FromNodeId,
            activeJourney.ProfileId,
            firstCompletionSignalId))
    {
        _activeRouteJourneys.erase(runtimeGroupId);
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Route] Runtime entity group #{} started route journey from {} ({}) to {} ({}) across {} segment(s).",
        runtimeGroupId,
        fromNode->Name,
        fromNodeId,
        destinationNode->Name,
        destinationNodeId,
        activeJourney.Steps.size());

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
    _activeRouteJourneys.erase(runtimeGroupId);

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
        _activeRouteJourneys.erase(runtimeGroupId);
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

void MovementController::SetRouteDebugEnabled(bool enabled)
{
    _routeDebugEnabled = enabled;

    LOG_INFO("server.loading",
        "[LWI Route Debug] Route movement debug logging {}.",
        enabled ? "enabled" : "disabled");
}

bool MovementController::IsRouteDebugEnabled() const
{
    return _routeDebugEnabled;
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
    std::vector<uint64> defeatedRuntimes;

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
                "No living creature entities remain.",
                runtimeGroupId,
                movement.PathId);

            completed.push_back(runtimeGroupId);
            defeatedRuntimes.push_back(movement.RuntimeId);
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
        _activeRouteJourneys.erase(runtimeGroupId);
    }

    std::sort(defeatedRuntimes.begin(), defeatedRuntimes.end());
    defeatedRuntimes.erase(
        std::unique(defeatedRuntimes.begin(), defeatedRuntimes.end()),
        defeatedRuntimes.end());

    for (uint64 runtimeId : defeatedRuntimes)
    {
        sInvasionRuntimeMgr.FailRuntime(runtimeId, "active movement force defeated");
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
    std::unordered_map<uint8, uint32> roleSlots;
    movement.Destinations.clear();
    movement.ArrivalGraceStartedAtMs = 0;

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

        uint32 const roleSlot = roleSlots[entity.TacticalRole]++;

        float targetX = node.X;
        float targetY = node.Y;
        float targetZ = node.Z;
        BuildFormationDestination(node, movement.Direction, entity.TacticalRole, roleSlot, targetX, targetY, targetZ);

        if (movement.RouteMovement && _routeDebugEnabled)
        {
            LOG_INFO("server.loading",
                "[LWI Route Debug] Group #{} path {} node {} direction={} creature={} member={} "
                "current=({:.3f}, {:.3f}, {:.3f}) recorded=({:.3f}, {:.3f}, {:.3f}) "
                "formationTarget=({:.3f}, {:.3f}, {:.3f}) role={} slot={}.",
                movement.RuntimeGroupId,
                movement.PathId,
                node.NodeOrder,
                movement.Direction == MovementDirection::Reverse ? "REVERSE" : "FORWARD",
                entity.Entry,
                entity.MemberId,
                creature->GetPositionX(),
                creature->GetPositionY(),
                creature->GetPositionZ(),
                node.X,
                node.Y,
                node.Z,
                targetX,
                targetY,
                targetZ,
                entity.TacticalRole,
                roleSlot);
        }

        PathGenerator path(creature);

        // Do not force the destination. We want the navmesh to choose a valid,
        // terrain-aware endpoint rather than extending a failed path directly
        // through terrain to the requested XYZ.
        bool const pathFound = path.CalculatePath(
            targetX,
            targetY,
            targetZ,
            false);

        if (!pathFound || (path.GetPathType() & PATHFIND_NOPATH))
        {
            LOG_ERROR("server.loading",
                "[LWI Movement] Runtime entity group #{} creature {} member {} could not find an MMAP path "
                "to path {} node {} formation destination ({:.2f}, {:.2f}, {:.2f}); creature skipped for this node.",
                movement.RuntimeGroupId,
                entity.Entry,
                entity.MemberId,
                movement.PathId,
                node.NodeOrder,
                targetX,
                targetY,
                targetZ);
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

        if (movement.RouteMovement && _routeDebugEnabled)
        {
            LOG_INFO("server.loading",
                "[LWI Route Debug] Group #{} path {} node {} MMAP generated {} point(s); "
                "requested=({:.3f}, {:.3f}, {:.3f}) actualEnd=({:.3f}, {:.3f}, {:.3f}).",
                movement.RuntimeGroupId,
                movement.PathId,
                node.NodeOrder,
                pathPoints.size(),
                targetX,
                targetY,
                targetZ,
                actualEnd.x,
                actualEnd.y,
                actualEnd.z);

            for (std::size_t pathPointIndex = 0; pathPointIndex < pathPoints.size(); ++pathPointIndex)
            {
                G3D::Vector3 const& pathPoint = pathPoints[pathPointIndex];
                LOG_INFO("server.loading",
                    "[LWI Route Debug]   MMAP[{}] = ({:.3f}, {:.3f}, {:.3f})",
                    pathPointIndex,
                    pathPoint.x,
                    pathPoint.y,
                    pathPoint.z);
            }
        }

        RuntimeMovementDestination destination;
        destination.Guid = entity.Guid;
        destination.MapId = entity.MapId;
        destination.X = actualEnd.x;
        destination.Y = actualEnd.y;
        destination.Z = actualEnd.z;
        movement.Destinations.push_back(destination);

        // MoveSplinePath consumes the terrain-aware point list generated by
        // AzerothCore's PathGenerator/MMAP system. This avoids PointMovement's
        // straight-line fallback when a requested destination cannot be reached.
        creature->GetMotionMaster()->MoveSplinePath(
            &pathPoints,
            FORCED_MOVEMENT_NONE);

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
        "[LWI Movement] Runtime entity group #{} moving {} creature(s) via MMAP in role-aware formation to path {} node {} ({:.2f}, {:.2f}, {:.2f}).",
        movement.RuntimeGroupId,
        moved,
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

        if (creature->GetDistance(destination.X, destination.Y, destination.Z) <= ArrivalTolerance)
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
    uint64 nowMs)
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
    bool const isFinalNode = movement.Direction == MovementDirection::Reverse
        ? movement.NodeIndex == 0
        : movement.NodeIndex + 1 >= nodes->size();

    uint32 living = 0;
    uint32 exactArrivals = 0;
    uint32 formationArrivals = 0;
    uint32 objectiveArrivals = 0;
    bool anyInCombat = false;

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

        if (creature->IsInCombat())
        {
            anyInCombat = true;
        }

        float const formationDistance = creature->GetDistance(
            destination.X,
            destination.Y,
            destination.Z);

        if (formationDistance <= ArrivalTolerance)
        {
            ++exactArrivals;
            ++formationArrivals;
        }
        else if (formationDistance <= FormationArrivalTolerance)
        {
            ++formationArrivals;
        }

        if (creature->GetDistance(node.X, node.Y, node.Z) <= FinalObjectiveArrivalRadius)
        {
            ++objectiveArrivals;
        }
    }

    if (living == 0)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    uint32 const requiredArrivals =
        std::max<uint32>(1, (living * FormationArrivalPercent + 99) / 100);

    // Shared route segments are explicitly authored travel corridors. Unlike
    // normal invasion movement, do not accept the broad formation tolerance or
    // regroup grace here: issuing the next node early can cut a corner between
    // two otherwise-correct authored/MMAP paths. Every surviving route member
    // must reach its own exact formation destination before the route advances.
    if (movement.RouteMovement)
    {
        uint32 strictRouteArrivals = 0;

        for (RuntimeMovementDestination const& destination : movement.Destinations)
        {
            Map* map = sMapMgr->FindMap(destination.MapId, 0);
            if (!map)
                continue;

            Creature* creature = map->GetCreature(destination.Guid);
            if (!creature || !creature->IsAlive())
                continue;

            if (creature->IsInCombat())
            {
                movement.ArrivalGraceStartedAtMs = 0;
                return false;
            }

            if (creature->GetDistance(destination.X, destination.Y, destination.Z) <= RouteArrivalTolerance)
                ++strictRouteArrivals;
        }

        if (strictRouteArrivals == living)
        {
            if (_routeDebugEnabled)
            {
                LOG_INFO("server.loading",
                    "[LWI Route Debug] Group #{} strictly reached path {} node {}: "
                    "{}/{} surviving creature(s) within {:.2f} yd; advancing with no regroup grace.",
                    movement.RuntimeGroupId,
                    movement.PathId,
                    node.NodeOrder,
                    strictRouteArrivals,
                    living,
                    RouteArrivalTolerance);
            }

            movement.ArrivalGraceStartedAtMs = 0;
            return true;
        }

        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    // The final strategic node is an objective area, not another parade-ground
    // formation check. Once enough surviving members reach the objective radius,
    // the route is complete even if they are already fighting defenders.
    if (isFinalNode && objectiveArrivals >= requiredArrivals)
    {
        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} reached final objective for path {} node {}: "
            "{}/{} surviving creature(s) within {:.1f} yards. Combat does not block final arrival.",
            movement.RuntimeGroupId,
            movement.PathId,
            node.NodeOrder,
            objectiveArrivals,
            living,
            FinalObjectiveArrivalRadius);

        movement.ArrivalGraceStartedAtMs = 0;
        return true;
    }

    // Ideal intermediate-node case: every survivor reached its exact endpoint.
    if (!isFinalNode && exactArrivals == living)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return true;
    }

    // Intermediate travel nodes should not advance while survivors are fighting.
    if (!isFinalNode && anyInCombat)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    // Final node has not yet reached the objective threshold.
    if (isFinalNode)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    if (formationArrivals < requiredArrivals)
    {
        movement.ArrivalGraceStartedAtMs = 0;
        return false;
    }

    if (movement.ArrivalGraceStartedAtMs == 0)
    {
        movement.ArrivalGraceStartedAtMs = nowMs;

        LOG_INFO("server.loading",
            "[LWI Movement] Runtime entity group #{} has {}/{} surviving creature(s) within {:.1f} yards "
            "of their path {} node formation destinations; starting {} ms regroup grace.",
            movement.RuntimeGroupId,
            formationArrivals,
            living,
            FormationArrivalTolerance,
            movement.PathId,
            FormationArrivalGraceMs);

        return false;
    }

    if (nowMs - movement.ArrivalGraceStartedAtMs < FormationArrivalGraceMs)
    {
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Movement] Runtime entity group #{} accepted formation arrival at path {} node with "
        "{}/{} surviving creature(s) in position after regroup grace.",
        movement.RuntimeGroupId,
        movement.PathId,
        formationArrivals,
        living);

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

    if (movement.Direction == MovementDirection::Reverse)
    {
        if (movement.NodeIndex == 0)
        {
            CompleteMovement(runtimeGroupId, movement);
            return;
        }

        --movement.NodeIndex;
    }
    else
    {
        ++movement.NodeIndex;

        if (movement.NodeIndex >= nodes->size())
        {
            CompleteMovement(runtimeGroupId, movement);
            return;
        }
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

    auto journeyItr = _activeRouteJourneys.find(runtimeGroupId);
    if (journeyItr != _activeRouteJourneys.end())
    {
        ActiveRouteJourney& journey = journeyItr->second;

        if (journey.StepIndex + 1 < journey.Steps.size())
        {
            ++journey.StepIndex;
            RouteJourneyStep const nextStep = journey.Steps[journey.StepIndex];
            uint32 const nextCompletionSignalId = journey.StepIndex + 1 == journey.Steps.size()
                ? journey.CompletionSignalId
                : 0;

            if (StartRouteSegment(
                    runtimeGroupId,
                    nextStep.SegmentId,
                    nextStep.FromNodeId,
                    journey.ProfileId,
                    nextCompletionSignalId))
            {
                LOG_INFO("server.loading",
                    "[LWI Route] Runtime entity group #{} advanced to route journey segment {}/{} (segment {}, node {} -> {}).",
                    runtimeGroupId,
                    journey.StepIndex + 1,
                    journey.Steps.size(),
                    nextStep.SegmentId,
                    nextStep.FromNodeId,
                    nextStep.ToNodeId);
                return;
            }

            LOG_ERROR("server.loading",
                "[LWI Route] Runtime entity group #{} failed to continue route journey on segment {} from node {}; journey aborted.",
                runtimeGroupId, nextStep.SegmentId, nextStep.FromNodeId);
            _activeRouteJourneys.erase(journeyItr);
            movement.State = RuntimeMovementState::Completed;
            return;
        }

        LOG_INFO("server.loading",
            "[LWI Route] Runtime entity group #{} completed route journey from node {} to node {} across {} segment(s).",
            runtimeGroupId,
            journey.StartNodeId,
            journey.DestinationNodeId,
            journey.Steps.size());
        _activeRouteJourneys.erase(journeyItr);
    }

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
