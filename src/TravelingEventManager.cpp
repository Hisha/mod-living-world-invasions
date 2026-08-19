#include "TravelingEventManager.h"

#include "Creature.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "GameObject.h"
#include "LivingWorldInvasions.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "MovementController.h"
#include "RuntimeEntityGroup.h"
#include "TemporarySummon.h"

#include <algorithm>
#include <sstream>

namespace lwi
{
namespace
{
constexpr uint32 DefaultDwellSeconds = 120;
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
        "SELECT `id`,`name`,`wagon_entry`,`merchant_entry`,`merchant_seat_id`,`enabled` "
        "FROM `lwi_traveling_event` ORDER BY `id`");

    if (events)
    {
        do
        {
            Field* fields = events->Fetch();

            TravelingEventDefinition definition;
            definition.Id = fields[0].Get<uint32>();
            definition.Name = fields[1].Get<std::string>();
            definition.WagonEntry = fields[2].Get<uint32>();
            definition.MerchantEntry = fields[3].Get<uint32>();
            definition.MerchantSeatId = fields[4].Get<int8>();
            definition.Enabled = fields[5].Get<bool>();

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
    Map* map = sMapMgr->FindMap(mapId, 0);
    if (!map)
        return nullptr;

    return map->GetCreature(guid);
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

    if (definition.WagonEntry == 0 || definition.MerchantEntry == 0)
    {
        if (error)
            *error = "wagon_entry and merchant_entry must both be configured";
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

    Position wagonPosition;
    wagonPosition.Relocate(
        startNode->X,
        startNode->Y,
        startNode->Z,
        startNode->Orientation);

    TempSummon* wagon = map->SummonCreature(definition.WagonEntry, wagonPosition, nullptr, 0);
    if (!wagon)
    {
        if (error)
            *error = "failed to summon wagon creature";
        return false;
    }

    TempSummon* merchant = map->SummonCreature(definition.MerchantEntry, wagonPosition, nullptr, 0);
    if (!merchant)
    {
        wagon->DespawnOrUnsummon();
        if (error)
            *error = "failed to summon merchant creature";
        return false;
    }

    ApplyProtectedState(wagon);
    ApplyProtectedState(merchant);

    // The merchant is never a vendor while the caravan is moving.
    merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

    RuntimeEntityGroup& movementGroup = sRuntimeEntityGroupMgr.CreateGroup(0, 0);

    RuntimeEntity wagonEntity;
    wagonEntity.EntityType = static_cast<uint8>(EntityProviderType::Creature);
    wagonEntity.MapId = startNode->MapId;
    wagonEntity.Entry = definition.WagonEntry;
    wagonEntity.Guid = wagon->GetGUID();
    movementGroup.Entities.push_back(wagonEntity);

    runtime.EventId = definition.Id;
    runtime.RuntimeGroupId = movementGroup.Id;
    runtime.StopIndex = 0;
    runtime.State = TravelingEventState::Camped;
    runtime.StateTimerMs = 1000;
    runtime.WagonGuid = wagon->GetGUID();
    runtime.MerchantGuid = merchant->GetGUID();
    runtime.MapId = startNode->MapId;

    if (wagon->GetVehicleKit())
    {
        merchant->EnterVehicle(wagon, definition.MerchantSeatId);
        LOG_INFO("server.loading",
            "[LWI Travel] Event {} mounted merchant {} on wagon {} seat {}.",
            definition.Id,
            merchant->GetGUID().ToString(),
            wagon->GetGUID().ToString(),
            static_cast<int32>(definition.MerchantSeatId));
    }
    else
    {
        LOG_WARN("server.loading",
            "[LWI Travel] Event {} wagon entry {} has no VehicleKit; merchant {} remains at the start node. "
            "Travel will still run so the wagon candidate can be tested.",
            definition.Id,
            definition.WagonEntry,
            merchant->GetGUID().ToString());
    }

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
    sMovementController.CancelGroup(runtime.RuntimeGroupId);

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
    {
        merchant->ExitVehicle();
        if (TempSummon* summon = merchant->ToTempSummon())
            summon->DespawnOrUnsummon();
    }

    if (Creature* wagon = GetCreature(runtime.MapId, runtime.WagonGuid))
    {
        if (TempSummon* summon = wagon->ToTempSummon())
            summon->DespawnOrUnsummon();
    }

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

    Creature* wagon = GetCreature(runtime.MapId, runtime.WagonGuid);
    Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
    if (!wagon || !merchant)
        return false;

    EndCamp(runtime, definition);

    merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

    if (!merchant->GetVehicle())
    {
        if (wagon->GetVehicleKit())
            merchant->EnterVehicle(wagon, definition.MerchantSeatId);
        else
            merchant->NearTeleportTo(
                wagon->GetPositionX(),
                wagon->GetPositionY(),
                wagon->GetPositionZ(),
                wagon->GetOrientation());
    }

    if (!fromStop.DepartureText.empty())
        merchant->Say(fromStop.DepartureText, LANG_UNIVERSAL);

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

    LOG_INFO("server.loading",
        "[LWI Travel] Event {} departed stop {} route node {} for stop {} route node {}.",
        definition.Id,
        fromIndex,
        fromStop.RouteNodeId,
        toIndex,
        toStop.RouteNodeId);

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

    Creature* wagon = GetCreature(runtime.MapId, runtime.WagonGuid);
    Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
    if (!wagon || !merchant)
        return false;

    merchant->ExitVehicle();

    float const merchantX = node->X + 2.5f;
    float const merchantY = node->Y + 1.5f;
    merchant->NearTeleportTo(merchantX, merchantY, node->Z, node->Orientation);
    ApplyProtectedState(merchant);
    merchant->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

    if (!stop.ArrivalText.empty())
        merchant->Say(stop.ArrivalText, LANG_UNIVERSAL);

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
        "[LWI Travel] Event {} camped at route node {} for {} second(s); merchant vending enabled, {} prop(s) spawned.",
        definition.Id,
        stop.RouteNodeId,
        stop.DwellSeconds,
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

        Creature* wagon = GetCreature(runtime.MapId, runtime.WagonGuid);
        Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
        if (!wagon || !merchant)
        {
            LOG_ERROR("server.loading",
                "[LWI Travel] Event {} lost its wagon or merchant; stopping runtime.",
                runtime.EventId);
            CleanupRuntime(runtime);
            itr = _active.erase(itr);
            continue;
        }

        // Protection is deliberately enforced continuously. This prototype is
        // testing caravan lifecycle and route behavior, not combat recovery.
        ApplyProtectedState(wagon);
        ApplyProtectedState(merchant);

        if (runtime.State == TravelingEventState::Traveling)
        {
            merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

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
            << "\n";
    }

    return out.str();
}

}
