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
#include <sstream>

namespace lwi
{
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

    // The current DB column names are intentionally retained for compatibility
    // with the prototype schema.  leader_entry is now the traveling merchant
    // (and route owner), while wagon_entry is temporarily used as the pack-mule
    // creature entry.  The old merchant_entry/wagon offset fields are ignored.
    QueryResult events = WorldDatabase.Query(
        "SELECT `id`,`name`,`leader_entry`,`wagon_entry`,`enabled` "
        "FROM `lwi_traveling_event` ORDER BY `id`");

    if (events)
    {
        do
        {
            Field* fields = events->Fetch();

            TravelingEventDefinition definition;
            definition.Id = fields[0].Get<uint32>();
            definition.Name = fields[1].Get<std::string>();
            definition.MerchantEntry = fields[2].Get<uint32>();
            definition.PackMuleEntry = fields[3].Get<uint32>();
            definition.PackMuleCount = 2;
            definition.Enabled = fields[4].Get<bool>();

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

    if (definition.MerchantEntry == 0 || definition.PackMuleEntry == 0)
    {
        if (error)
            *error = "leader_entry (merchant) and wagon_entry (pack-mule creature) must both be configured";
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

    Position startPosition;
    startPosition.Relocate(
        startNode->X,
        startNode->Y,
        startNode->Z,
        startNode->Orientation);

    TempSummon* merchant = map->SummonCreature(definition.MerchantEntry, startPosition, nullptr, 0);
    if (!merchant)
    {
        if (error)
            *error = "failed to summon traveling merchant creature";
        return false;
    }

    ApplyProtectedState(merchant);
    merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

    RuntimeEntityGroup& movementGroup = sRuntimeEntityGroupMgr.CreateGroup(0, 0);
    movementGroup.RouteFormation = RouteFormationProfile::TravelingCaravan;

    RuntimeEntity merchantEntity;
    merchantEntity.EntityType = static_cast<uint8>(EntityProviderType::Creature);
    merchantEntity.MapId = startNode->MapId;
    merchantEntity.Entry = definition.MerchantEntry;
    merchantEntity.Guid = merchant->GetGUID();

    // Entity ordering is intentional. Route movement uses entity slot 0 as
    // the route leader. This group is marked TravelingCaravan, so follower
    // slots 1 and 2 become a shallow V behind the merchant rather than using
    // the generic five-wide marching formation.
    movementGroup.Entities.push_back(merchantEntity);

    std::vector<ObjectGuid> muleGuids;
    muleGuids.reserve(definition.PackMuleCount);

    for (uint8 i = 0; i < definition.PackMuleCount; ++i)
    {
        TempSummon* mule = map->SummonCreature(definition.PackMuleEntry, startPosition, nullptr, 0);
        if (!mule)
        {
            for (ObjectGuid const& guid : muleGuids)
            {
                if (Creature* existingMule = GetCreature(startNode->MapId, guid))
                {
                    if (TempSummon* summon = existingMule->ToTempSummon())
                        summon->DespawnOrUnsummon();
                }
            }

            merchant->DespawnOrUnsummon();
            sRuntimeEntityGroupMgr.RemoveGroup(movementGroup.Id);

            if (error)
                *error = "failed to summon one of the traveling pack mules";
            return false;
        }

        ApplyProtectedState(mule);

        RuntimeEntity muleEntity;
        muleEntity.EntityType = static_cast<uint8>(EntityProviderType::Creature);
        muleEntity.MapId = startNode->MapId;
        muleEntity.Entry = definition.PackMuleEntry;
        muleEntity.Guid = mule->GetGUID();
        movementGroup.Entities.push_back(muleEntity);

        muleGuids.push_back(mule->GetGUID());
    }

    runtime.EventId = definition.Id;
    runtime.RuntimeGroupId = movementGroup.Id;
    runtime.StopIndex = 0;
    runtime.State = TravelingEventState::Camped;
    runtime.StateTimerMs = 1000;
    runtime.MerchantGuid = merchant->GetGUID();
    runtime.PackMuleGuids = std::move(muleGuids);
    runtime.MapId = startNode->MapId;

    LOG_INFO("server.loading",
        "[LWI Travel] Event {} spawned merchant creature {} GUID {} as route owner with {} pack mule(s) "
        "using creature entry {}. All caravan members are protected/non-aggro.",
        definition.Id,
        definition.MerchantEntry,
        runtime.MerchantGuid.ToString(),
        runtime.PackMuleGuids.size(),
        definition.PackMuleEntry);

    return true;
}

TravelingEventStartResult TravelingEventManager::Start(uint32 eventId, std::string* error)
{
    if (_active.find(eventId) != _active.end())
    {
        if (error)
            *error = "event is already active";
        LOG_INFO("server.loading", "[LWI Travel] Start event {} refused: event is already active.", eventId);
        return TravelingEventStartResult::AlreadyActive;
    }

    TravelingEventDefinition const* definition = GetDefinition(eventId);
    if (!definition)
    {
        if (error)
            *error = "event is not loaded; check lwi_traveling_event and run .lwi reload";
        LOG_ERROR("server.loading", "[LWI Travel] Start event {} failed: definition is not loaded.", eventId);
        return TravelingEventStartResult::NotFound;
    }

    if (!definition->Enabled)
    {
        if (error)
            *error = "event is loaded but disabled";
        LOG_INFO("server.loading",
            "[LWI Travel] Start event {} ({}) refused: definition is disabled.",
            eventId,
            definition->Name);
        return TravelingEventStartResult::Disabled;
    }

    if (definition->Stops.size() < 2 || definition->MerchantEntry == 0 || definition->PackMuleEntry == 0)
    {
        if (error)
            *error = definition->Stops.size() < 2
                ? "invalid configuration: at least two enabled stops are required"
                : "invalid configuration: leader_entry must be the merchant and wagon_entry must be the pack-mule creature";

        LOG_ERROR("server.loading",
            "[LWI Travel] Start event {} ({}) failed validation: {}.",
            eventId,
            definition->Name,
            error ? *error : "invalid configuration");
        return TravelingEventStartResult::InvalidConfiguration;
    }

    ActiveTravelingEvent runtime;
    std::string spawnError;
    if (!SpawnRuntime(*definition, runtime, &spawnError))
    {
        if (error)
            *error = spawnError;

        LOG_ERROR("server.loading",
            "[LWI Travel] Start event {} ({}) failed while creating runtime: {}.",
            eventId,
            definition->Name,
            spawnError);
        return TravelingEventStartResult::SpawnFailed;
    }

    _active.emplace(eventId, std::move(runtime));

    LOG_INFO("server.loading",
        "[LWI Travel] Started event {} ({}).",
        definition->Id,
        definition->Name);

    return TravelingEventStartResult::Started;
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
    }

    runtime.CampPropGuids.clear();

    for (ObjectGuid const& guid : runtime.PackMuleGuids)
    {
        if (Creature* mule = GetCreature(runtime.MapId, guid))
        {
            if (TempSummon* summon = mule->ToTempSummon())
                summon->DespawnOrUnsummon();
        }
    }

    runtime.PackMuleGuids.clear();

    if (Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid))
    {
        if (TempSummon* summon = merchant->ToTempSummon())
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

    Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
    if (!merchant)
        return false;

    for (ObjectGuid const& guid : runtime.PackMuleGuids)
    {
        if (!GetCreature(runtime.MapId, guid))
            return false;
    }

    EndCamp(runtime, definition);

    merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

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
        "[LWI Travel] Event {} departed stop {} route node {} for stop {} route node {}; "
        "merchant {} owns the route with {} pack-mule follower(s) in the movement group.",
        definition.Id,
        fromIndex,
        fromStop.RouteNodeId,
        toIndex,
        toStop.RouteNodeId,
        definition.MerchantEntry,
        runtime.PackMuleGuids.size());

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

    Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
    if (!merchant)
        return false;

    for (ObjectGuid const& guid : runtime.PackMuleGuids)
    {
        Creature* mule = GetCreature(runtime.MapId, guid);
        if (!mule)
            return false;

        ApplyProtectedState(mule);
    }

    ApplyProtectedState(merchant);
    merchant->SetNpcFlag(UNIT_NPC_FLAG_VENDOR);

    if (!stop.ArrivalText.empty())
        merchant->Say(stop.ArrivalText, LANG_UNIVERSAL);

    // Camp props remain supported but are deliberately optional.  For the
    // current test the prebuilt contains none; this pass is proving that the
    // merchant and both mules can complete the route together.
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
        "[LWI Travel] Event {} stopped at route node {} for {} second(s); merchant vending enabled; "
        "{} pack mule(s) remain with the caravan; {} camp prop(s) spawned.",
        definition.Id,
        stop.RouteNodeId,
        stop.DwellSeconds,
        runtime.PackMuleGuids.size(),
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

        Creature* merchant = GetCreature(runtime.MapId, runtime.MerchantGuid);
        bool missingMule = false;

        for (ObjectGuid const& guid : runtime.PackMuleGuids)
        {
            if (!GetCreature(runtime.MapId, guid))
            {
                missingMule = true;
                break;
            }
        }

        if (!merchant || missingMule || runtime.PackMuleGuids.size() != definition->PackMuleCount)
        {
            LOG_ERROR("server.loading",
                "[LWI Travel] Event {} lost its merchant or one of its pack mules; stopping runtime.",
                runtime.EventId);
            CleanupRuntime(runtime);
            itr = _active.erase(itr);
            continue;
        }

        ApplyProtectedState(merchant);
        for (ObjectGuid const& guid : runtime.PackMuleGuids)
        {
            if (Creature* mule = GetCreature(runtime.MapId, guid))
                ApplyProtectedState(mule);
        }

        if (runtime.State == TravelingEventState::Traveling)
        {
            merchant->RemoveNpcFlag(UNIT_NPC_FLAG_VENDOR);

            if (!sMovementController.IsGroupMoving(runtime.RuntimeGroupId))
            {
                if (!BeginCamp(runtime, *definition))
                {
                    LOG_ERROR("server.loading",
                        "[LWI Travel] Event {} failed to establish stop state; stopping runtime.",
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
                    "[LWI Travel] Event {} failed to depart stop; stopping runtime.",
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
            << " merchantGuid=" << runtime.MerchantGuid.ToString()
            << " packMules=" << runtime.PackMuleGuids.size()
            << "\n";
    }

    return out.str();
}

}
