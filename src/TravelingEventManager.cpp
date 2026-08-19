#include "TravelingEventManager.h"

#include "Creature.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameObject.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MovementController.h"
#include "QueryResult.h"
#include "RuntimeEntityGroup.h"
#include "TemporarySummon.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace lwi
{
namespace
{
constexpr uint32 MobileWagonUpdateIntervalMs = 100;
}

TravelingEventManager& TravelingEventManager::Instance()
{
    static TravelingEventManager instance;
    return instance;
}

void TravelingEventManager::Reset()
{
    for (auto& [eventId, runtime] : _active)
    {
        (void)eventId;
        CleanupRuntime(runtime);
    }

    _active.clear();
    _definitions.clear();
}

void TravelingEventManager::LoadDefinitions()
{
    for (auto& [eventId, runtime] : _active)
    {
        (void)eventId;
        CleanupRuntime(runtime);
    }

    _active.clear();
    _definitions.clear();

    QueryResult events = WorldDatabase.Query(
        "SELECT `id`,`name`,`leader_entry`,`wagon_entry`,`merchant_entry`,"
        "`wagon_distance_behind`,`wagon_lateral_offset`,`wagon_vertical_offset`,`enabled` "
        "FROM `lwi_traveling_event` ORDER BY `id`");

    if (events)
    {
        do
        {
            Field* fields = events->Fetch();

            TravelingEventDefinition definition;
            definition.Id = fields[0].Get<uint32>();
            definition.Name = fields[1].Get<std::string>();
            definition.LeaderEntry = fields[2].Get<uint32>();
            definition.WagonGameObjectEntry = fields[3].Get<uint32>();
            definition.MerchantEntry = fields[4].Get<uint32>();
            definition.WagonDistanceBehind = fields[5].Get<float>();
            definition.WagonLateralOffset = fields[6].Get<float>();
            definition.WagonVerticalOffset = fields[7].Get<float>();
            definition.Enabled = fields[8].Get<bool>();

            _definitions.emplace(definition.Id, std::move(definition));
        }
        while (events->NextRow());
    }

    QueryResult stops = WorldDatabase.Query(
        "SELECT `id`,`event_id`,`stop_order`,`route_node_id`,`dwell_seconds`,`arrival_text`,`departure_text` "
        "FROM `lwi_traveling_event_stop` WHERE `enabled` = 1 ORDER BY `event_id`,`stop_order`,`id`");

    if (stops)
    {
        do
        {
            Field* fields = stops->Fetch();
            uint32 const eventId = fields[1].Get<uint32>();

            auto itr = _definitions.find(eventId);
            if (itr == _definitions.end())
                continue;

            TravelingStopDefinition stop;
            stop.Id = fields[0].Get<uint32>();
            stop.EventId = eventId;
            stop.StopOrder = fields[2].Get<uint32>();
            stop.RouteNodeId = fields[3].Get<uint32>();
            stop.DwellSeconds = std::max<uint32>(1, fields[4].Get<uint32>());
            stop.ArrivalText = fields[5].Get<std::string>();
            stop.DepartureText = fields[6].Get<std::string>();

            itr->second.Stops.push_back(std::move(stop));
        }
        while (stops->NextRow());
    }

    QueryResult props = WorldDatabase.Query(
        "SELECT `id`,`event_id`,`gameobject_entry`,`offset_x`,`offset_y`,`offset_z`,`orientation_offset` "
        "FROM `lwi_traveling_event_prop` WHERE `enabled` = 1 ORDER BY `event_id`,`id`");

    if (props)
    {
        do
        {
            Field* fields = props->Fetch();
            uint32 const eventId = fields[1].Get<uint32>();

            auto itr = _definitions.find(eventId);
            if (itr == _definitions.end())
                continue;

            TravelingPropDefinition prop;
            prop.Id = fields[0].Get<uint32>();
            prop.EventId = eventId;
            prop.GameObjectEntry = fields[2].Get<uint32>();
            prop.OffsetX = fields[3].Get<float>();
            prop.OffsetY = fields[4].Get<float>();
            prop.OffsetZ = fields[5].Get<float>();
            prop.OrientationOffset = fields[6].Get<float>();

            itr->second.Props.push_back(std::move(prop));
        }
        while (props->NextRow());
    }

    LOG_INFO("server.loading",
        "[LWI Travel] Loaded {} traveling-world-event definition(s).",
        _definitions.size());
}

TravelingEventDefinition const* TravelingEventManager::GetDefinition(uint32 eventId) const
{
    auto itr = _definitions.find(eventId);
    if (itr == _definitions.end())
        return nullptr;

    return &itr->second;
}

Creature* TravelingEventManager::GetCreature(uint16 mapId, ObjectGuid guid) const
{
    if (guid.IsEmpty())
        return nullptr;

    Map* map = sMapMgr->FindMap(mapId, 0);
    if (!map)
        return nullptr;

    return map->GetCreature(guid);
}

GameObject* TravelingEventManager::GetGameObject(uint16 mapId, ObjectGuid guid) const
{
    if (guid.IsEmpty())
        return nullptr;

    Map* map = sMapMgr->FindMap(mapId, 0);
    if (!map)
        return nullptr;

    return map->GetGameObject(guid);
}

void TravelingEventManager::ApplyProtectedState(Creature* creature) const
{
    if (!creature)
        return;

    creature->CombatStop(true);
    creature->SetReactState(REACT_PASSIVE);
    creature->SetImmuneToAll(true);
    creature->SetUnitFlag(UNIT_FLAG_NON_ATTACKABLE);
}

bool TravelingEventManager::PlaceMobileWagon(
    ActiveTravelingEvent& runtime,
    TravelingEventDefinition const& definition)
{
    Map* map = sMapMgr->FindMap(runtime.MapId, 0);
    Creature* leader = GetCreature(runtime.MapId, runtime.LeaderGuid);
    GameObject* wagon = GetGameObject(runtime.MapId, runtime.WagonGuid);

    if (!map || !leader || !wagon || !leader->IsInWorld() || !wagon->IsInWorld())
        return false;

    float const orientation = leader->GetOrientation();
    float const cosO = std::cos(orientation);
    float const sinO = std::sin(orientation);

    float const x = leader->GetPositionX()
        - cosO * definition.WagonDistanceBehind
        - sinO * definition.WagonLateralOffset;

    float const y = leader->GetPositionY()
        - sinO * definition.WagonDistanceBehind
        + cosO * definition.WagonLateralOffset;

    // Phase-one intentionally follows the leader's Z.  If the wagon visibly
    // floats/buries on slopes, terrain-height sampling is the next isolated
    // refinement rather than mixing it into this proof-of-concept.
    float const z = leader->GetPositionZ() + definition.WagonVerticalOffset;

    map->GameObjectRelocation(wagon, x, y, z, orientation);
    return true;
}

bool TravelingEventManager::UpdateMobileWagon(
    ActiveTravelingEvent& runtime,
    TravelingEventDefinition const& definition)
{
    if (!PlaceMobileWagon(runtime, definition))
    {
        LOG_ERROR("server.loading",
            "[LWI Travel] Event {} lost its route leader or mobile wagon GO {} while updating.",
            definition.Id,
            definition.WagonGameObjectEntry);
        return false;
    }

    return true;
}

bool TravelingEventManager::SpawnRuntime(
    TravelingEventDefinition const& definition,
    ActiveTravelingEvent& runtime,
    std::string* error)
{
    if (definition.Stops.size() < 2)
    {
        if (error)
            *error = "traveling event requires at least two enabled stops";
        return false;
    }

    if (definition.LeaderEntry == 0 || definition.WagonGameObjectEntry == 0)
    {
        if (error)
            *error = "leader_entry and wagon_entry (GameObject entry) must both be configured";
        return false;
    }

    RouteNodeDefinition const* startNode = sInvasionMgr.GetRouteNode(definition.Stops.front().RouteNodeId);
    if (!startNode || !startNode->Enabled)
    {
        if (error)
            *error = "first stop references a missing/disabled route node";
        return false;
    }

    Map* map = sMapMgr->FindMap(startNode->MapId, 0);
    if (!map)
    {
        if (error)
            *error = "start map is not currently available";
        return false;
    }

    Position leaderPosition;
    leaderPosition.Relocate(
        startNode->X,
        startNode->Y,
        startNode->Z,
        startNode->Orientation);

    TempSummon* leader = map->SummonCreature(definition.LeaderEntry, leaderPosition, nullptr, 0);
    if (!leader)
    {
        if (error)
            *error = "failed to summon traveling-event route leader creature";
        return false;
    }

    ApplyProtectedState(leader);

    Position wagonPosition;
    wagonPosition.Relocate(
        startNode->X,
        startNode->Y,
        startNode->Z,
        startNode->Orientation);

    GameObject* wagon = map->SummonGameObject(
        definition.WagonGameObjectEntry,
        wagonPosition,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0,
        true);

    if (!wagon)
    {
        leader->DespawnOrUnsummon();
        if (error)
            *error = "failed to summon mobile wagon GameObject";
        return false;
    }

    ObjectGuid merchantGuid;
    if (definition.MerchantEntry != 0)
    {
        TempSummon* merchant = map->SummonCreature(definition.MerchantEntry, leaderPosition, nullptr, 0);
        if (!merchant)
        {
            wagon->Delete();
            leader->DespawnOrUnsummon();
            if (error)
                *error = "failed to summon merchant creature";
            return false;
        }

        ApplyProtectedState(merchant);
        merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);
        merchantGuid = merchant->GetGUID();
    }

    RuntimeEntityGroup& movementGroup = sRuntimeEntityGroupMgr.CreateGroup(0, 0);

    RuntimeEntity leaderEntity;
    leaderEntity.EntityType = static_cast<uint8>(EntityProviderType::Creature);
    leaderEntity.MapId = startNode->MapId;
    leaderEntity.Entry = definition.LeaderEntry;
    leaderEntity.Guid = leader->GetGUID();
    movementGroup.Entities.push_back(leaderEntity);

    runtime.EventId = definition.Id;
    runtime.RuntimeGroupId = movementGroup.Id;
    runtime.StopIndex = 0;
    runtime.State = TravelingEventState::Camped;
    runtime.StateTimerMs = 1000;
    runtime.WagonUpdateTimerMs = 0;
    runtime.LeaderGuid = leader->GetGUID();
    runtime.WagonGuid = wagon->GetGUID();
    runtime.MerchantGuid = merchantGuid;
    runtime.MapId = startNode->MapId;

    if (!PlaceMobileWagon(runtime, definition))
    {
        CleanupRuntime(runtime);
        if (error)
            *error = "failed to position mobile wagon behind route leader";
        return false;
    }

    LOG_INFO("server.loading",
        "[LWI Travel] Event {} spawned leader creature {} GUID {} with mobile wagon GO {} GUID {}; "
        "wagon offset behind={:.2f}, lateral={:.2f}, vertical={:.2f}; merchant {}.",
        definition.Id,
        definition.LeaderEntry,
        runtime.LeaderGuid.ToString(),
        definition.WagonGameObjectEntry,
        runtime.WagonGuid.ToString(),
        definition.WagonDistanceBehind,
        definition.WagonLateralOffset,
        definition.WagonVerticalOffset,
        definition.MerchantEntry == 0 ? "disabled for phase-one test" : "enabled");

    return true;
}

bool TravelingEventManager::Start(uint32 eventId, std::string* error)
{
    if (_active.find(eventId) != _active.end())
    {
        if (error)
            *error = "event is already active";
        return false;
    }

    TravelingEventDefinition const* definition = GetDefinition(eventId);
    if (!definition || !definition->Enabled)
    {
        if (error)
            *error = "event does not exist or is disabled";
        return false;
    }

    ActiveTravelingEvent runtime;
    if (!SpawnRuntime(*definition, runtime, error))
        return false;

    _active.emplace(eventId, std::move(runtime));

    LOG_INFO("server.loading",
        "[LWI Travel] Started event {} ({}).",
        definition->Id,
        definition->Name);

    return true;
}

void TravelingEventManager::CleanupRuntime(ActiveTravelingEvent& runtime)
{
    if (runtime.RuntimeGroupId != 0)
        sMovementController.CancelGroup(runtime.RuntimeGroupId);

    Map* map = sMapMgr->FindMap(runtime.MapId, 0);
    if (map)
    {
        for (ObjectGuid const& guid : runtime.CampPropGuids)
        {
            if (GameObject* gameObject = map->GetGameObject(guid))
                gameObject->Delete();
        }

        if (GameObject* wagon = map->GetGameObject(runtime.WagonGuid))
            wagon->Delete();
    }

    runtime.CampPropGuids.clear();

    if (Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid))
    {
        if (TempSummon* summon = merchant->ToTempSummon())
            summon->DespawnOrUnsummon();
    }

    if (Creature* leader = GetCreature(runtime.MapId, runtime.LeaderGuid))
    {
        if (TempSummon* summon = leader->ToTempSummon())
            summon->DespawnOrUnsummon();
    }

    if (runtime.RuntimeGroupId != 0)
        sRuntimeEntityGroupMgr.RemoveGroup(runtime.RuntimeGroupId);
}

bool TravelingEventManager::Stop(uint32 eventId, std::string* error)
{
    auto itr = _active.find(eventId);
    if (itr == _active.end())
    {
        if (error)
            *error = "event is not active";
        return false;
    }

    CleanupRuntime(itr->second);
    _active.erase(itr);

    LOG_INFO("server.loading", "[LWI Travel] Stopped event {}.", eventId);
    return true;
}

bool TravelingEventManager::BeginTravel(
    ActiveTravelingEvent& runtime,
    TravelingEventDefinition const& definition)
{
    if (definition.Stops.size() < 2)
        return false;

    uint32 const fromIndex = runtime.StopIndex;
    uint32 const toIndex = (runtime.StopIndex + 1) % definition.Stops.size();

    TravelingStopDefinition const& fromStop = definition.Stops[fromIndex];
    TravelingStopDefinition const& toStop = definition.Stops[toIndex];

    Creature* leader = GetCreature(runtime.MapId, runtime.LeaderGuid);
    GameObject* wagon = GetGameObject(runtime.MapId, runtime.WagonGuid);
    if (!leader || !wagon)
        return false;

    EndCamp(runtime, definition);

    if (Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid))
    {
        merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

        if (!fromStop.DepartureText.empty())
            merchant->Say(fromStop.DepartureText, LANG_UNIVERSAL);
    }

    if (!sMovementController.StartRouteJourney(
            runtime.RuntimeGroupId,
            fromStop.RouteNodeId,
            toStop.RouteNodeId))
    {
        LOG_ERROR("server.loading",
            "[LWI Travel] Event {} could not start route journey {} -> {}.",
            definition.Id,
            fromStop.RouteNodeId,
            toStop.RouteNodeId);
        return false;
    }

    runtime.StopIndex = toIndex;
    runtime.State = TravelingEventState::Traveling;
    runtime.StateTimerMs = 0;
    runtime.WagonUpdateTimerMs = 0;

    LOG_INFO("server.loading",
        "[LWI Travel] Event {} departed stop {} route node {} for stop {} route node {}; "
        "leader {} is route owner and wagon GO {} will follow at 100 ms updates.",
        definition.Id,
        fromIndex,
        fromStop.RouteNodeId,
        toIndex,
        toStop.RouteNodeId,
        definition.LeaderEntry,
        definition.WagonGameObjectEntry);

    return true;
}

void TravelingEventManager::EndCamp(
    ActiveTravelingEvent& runtime,
    TravelingEventDefinition const& /*definition*/)
{
    Map* map = sMapMgr->FindMap(runtime.MapId, 0);
    if (map)
    {
        for (ObjectGuid const& guid : runtime.CampPropGuids)
        {
            if (GameObject* gameObject = map->GetGameObject(guid))
                gameObject->Delete();
        }
    }

    runtime.CampPropGuids.clear();

    if (Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid))
        merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);
}

bool TravelingEventManager::BeginCamp(
    ActiveTravelingEvent& runtime,
    TravelingEventDefinition const& definition)
{
    if (runtime.StopIndex >= definition.Stops.size())
        return false;

    TravelingStopDefinition const& stop = definition.Stops[runtime.StopIndex];
    RouteNodeDefinition const* node = sInvasionMgr.GetRouteNode(stop.RouteNodeId);
    if (!node || !node->Enabled)
        return false;

    runtime.MapId = node->MapId;

    Creature* leader = GetCreature(runtime.MapId, runtime.LeaderGuid);
    GameObject* wagon = GetGameObject(runtime.MapId, runtime.WagonGuid);
    if (!leader || !wagon)
        return false;

    // Snap one final time using the exact leader position reached by the route.
    if (!PlaceMobileWagon(runtime, definition))
        return false;

    Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
    if (merchant)
    {
        float const merchantX = node->X + 2.5f;
        float const merchantY = node->Y + 1.5f;
        merchant->NearTeleportTo(merchantX, merchantY, node->Z, node->Orientation);
        ApplyProtectedState(merchant);
        merchant->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

        if (!stop.ArrivalText.empty())
            merchant->Say(stop.ArrivalText, LANG_UNIVERSAL);
    }

    Map* map = sMapMgr->FindMap(node->MapId, 0);
    if (map)
    {
        for (TravelingPropDefinition const& prop : definition.Props)
        {
            if (prop.GameObjectEntry == 0)
                continue;

            Position propPosition;
            propPosition.Relocate(
                node->X + prop.OffsetX,
                node->Y + prop.OffsetY,
                node->Z + prop.OffsetZ,
                node->Orientation + prop.OrientationOffset);

            GameObject* gameObject = map->SummonGameObject(
                prop.GameObjectEntry,
                propPosition,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                0,
                true);

            if (gameObject)
                runtime.CampPropGuids.push_back(gameObject->GetGUID());
        }
    }

    runtime.State = TravelingEventState::Camped;
    runtime.StateTimerMs = std::max<uint32>(1, stop.DwellSeconds) * IN_MILLISECONDS;

    LOG_INFO("server.loading",
        "[LWI Travel] Event {} camped at route node {} for {} second(s); mobile wagon GO {} parked; "
        "merchant vending={}, {} prop(s) spawned.",
        definition.Id,
        stop.RouteNodeId,
        stop.DwellSeconds,
        definition.WagonGameObjectEntry,
        merchant ? "enabled" : "disabled (phase-one wagon test)",
        runtime.CampPropGuids.size());

    return true;
}

void TravelingEventManager::Update(uint32 diff)
{
    for (auto itr = _active.begin(); itr != _active.end();)
    {
        ActiveTravelingEvent& runtime = itr->second;
        TravelingEventDefinition const* definition = GetDefinition(runtime.EventId);

        if (!definition)
        {
            CleanupRuntime(runtime);
            itr = _active.erase(itr);
            continue;
        }

        Creature* leader = GetCreature(runtime.MapId, runtime.LeaderGuid);
        GameObject* wagon = GetGameObject(runtime.MapId, runtime.WagonGuid);
        Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);

        if (!leader || !wagon || (definition->MerchantEntry != 0 && !merchant))
        {
            LOG_ERROR("server.loading",
                "[LWI Travel] Event {} lost its leader, wagon, or configured merchant; stopping runtime.",
                runtime.EventId);
            CleanupRuntime(runtime);
            itr = _active.erase(itr);
            continue;
        }

        // Protection is deliberately enforced continuously. This prototype is
        // testing caravan lifecycle and route behavior, not combat recovery.
        ApplyProtectedState(leader);
        if (merchant)
            ApplyProtectedState(merchant);

        if (runtime.State == TravelingEventState::Traveling)
        {
            if (merchant)
                merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

            if (runtime.WagonUpdateTimerMs > diff)
                runtime.WagonUpdateTimerMs -= diff;
            else
            {
                runtime.WagonUpdateTimerMs = MobileWagonUpdateIntervalMs;
                if (!UpdateMobileWagon(runtime, *definition))
                {
                    CleanupRuntime(runtime);
                    itr = _active.erase(itr);
                    continue;
                }
            }

            if (!sMovementController.IsGroupMoving(runtime.RuntimeGroupId))
            {
                if (!BeginCamp(runtime, *definition))
                {
                    LOG_ERROR("server.loading",
                        "[LWI Travel] Event {} failed to establish camp; stopping runtime.",
                        runtime.EventId);
                    CleanupRuntime(runtime);
                    itr = _active.erase(itr);
                    continue;
                }
            }
        }
        else
        {
            if (merchant)
                merchant->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

            if (runtime.StateTimerMs > diff)
                runtime.StateTimerMs -= diff;
            else if (!BeginTravel(runtime, *definition))
            {
                LOG_ERROR("server.loading",
                    "[LWI Travel] Event {} failed to depart camp; stopping runtime.",
                    runtime.EventId);
                CleanupRuntime(runtime);
                itr = _active.erase(itr);
                continue;
            }
        }

        ++itr;
    }
}

std::string TravelingEventManager::BuildStatusReport() const
{
    std::ostringstream out;
    out << "\nTraveling world events: " << _active.size() << " active / "
        << _definitions.size() << " loaded\n";

    for (auto const& [eventId, runtime] : _active)
    {
        TravelingEventDefinition const* definition = GetDefinition(eventId);
        out << "  #" << eventId << " "
            << (definition ? definition->Name : "<missing>")
            << " state="
            << (runtime.State == TravelingEventState::Traveling ? "TRAVELING" : "CAMPED")
            << " stopIndex=" << runtime.StopIndex
            << " movementGroup=" << runtime.RuntimeGroupId
            << " leaderGuid=" << runtime.LeaderGuid.ToString()
            << " wagonGuid=" << runtime.WagonGuid.ToString()
            << "\n";
    }

    return out.str();
}

}
