#ifndef MOD_LIVING_WORLD_INVASIONS_TRAVELING_EVENT_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_TRAVELING_EVENT_MANAGER_H

#include "Define.h"
#include "ObjectGuid.h"

#include <string>
#include <unordered_map>
#include <vector>

class Creature;
class GameObject;

namespace lwi
{
enum class TravelingEventState : uint8
{
    Traveling = 0,
    Camped = 1
};

enum class TravelingEventStartResult : uint8
{
    Started = 0,
    NotFound,
    Disabled,
    AlreadyActive,
    InvalidConfiguration,
    SpawnFailed
};

struct TravelingStopDefinition
{
    uint32 Id = 0;
    uint32 EventId = 0;
    uint32 StopOrder = 0;
    uint32 RouteNodeId = 0;
    uint32 CampLayoutId = 0;
    uint32 DwellSeconds = 120;
    std::string ArrivalText;
    std::string DepartureText;
};

struct TravelingCampPropDefinition
{
    uint32 Id = 0;
    uint32 LayoutId = 0;
    uint32 GameObjectEntry = 0;
    float ForwardOffset = 0.0f;
    float RightOffset = 0.0f;
    float ZOffset = 0.0f;
    float OrientationOffset = 0.0f;
};

enum class TravelingCampTargetType : uint8
{
    Merchant = 1,
    Mule1 = 2,
    Mule2 = 3,
    LayoutProp = 4
};

struct TravelingCampNodeZOverride
{
    uint32 RouteNodeId = 0;
    TravelingCampTargetType TargetType = TravelingCampTargetType::LayoutProp;
    uint32 TargetId = 0;
    float ZOverride = 0.0f;
};

struct TravelingCampLayoutDefinition
{
    uint32 Id = 0;
    std::string Name;

    float MerchantForward = 0.0f;
    float MerchantRight = 0.0f;
    float MerchantZ = 0.0f;
    float MerchantOrientationOffset = 0.0f;

    float Mule1Forward = 0.0f;
    float Mule1Right = 0.0f;
    float Mule1Z = 0.0f;
    float Mule1OrientationOffset = 0.0f;

    float Mule2Forward = 0.0f;
    float Mule2Right = 0.0f;
    float Mule2Z = 0.0f;
    float Mule2OrientationOffset = 0.0f;

    bool Enabled = true;
    std::vector<TravelingCampPropDefinition> Props;
};

struct TravelingEventDefinition
{
    uint32 Id = 0;
    std::string Name;

    // Compatibility note for the current prototype schema:
    //   lwi_traveling_event.leader_entry -> MerchantEntry
    //   lwi_traveling_event.wagon_entry  -> PackMuleEntry
    //
    // This lets us prove the all-creature caravan without forcing another
    // schema migration while the traveling-event data model is still evolving.
    uint32 MerchantEntry = 0;
    uint32 PackMuleEntry = 0;
    uint8 PackMuleCount = 2;

    bool Enabled = false;
    std::vector<TravelingStopDefinition> Stops;
};

struct ActiveTravelingEvent
{
    uint32 EventId = 0;
    uint64 RuntimeGroupId = 0;
    uint32 StopIndex = 0;
    TravelingEventState State = TravelingEventState::Camped;
    uint32 StateTimerMs = 0;

    ObjectGuid MerchantGuid;
    std::vector<ObjectGuid> PackMuleGuids;

    uint16 MapId = 0;
    std::vector<ObjectGuid> CampPropGuids;
};

class TravelingEventManager
{
public:
    static TravelingEventManager& Instance();

    void LoadDefinitions();
    void Reset();
    void Update(uint32 diff);

    TravelingEventStartResult Start(uint32 eventId, std::string* error = nullptr);
    bool Stop(uint32 eventId, std::string* error = nullptr);

    std::string BuildStatusReport() const;

private:
    TravelingEventDefinition const* GetDefinition(uint32 eventId) const;
    TravelingCampLayoutDefinition const* GetCampLayout(uint32 layoutId) const;
    float GetCampNodeZOverride(
        uint32 routeNodeId,
        TravelingCampTargetType targetType,
        uint32 targetId = 0) const;
    bool SpawnRuntime(TravelingEventDefinition const& definition, ActiveTravelingEvent& runtime, std::string* error);
    bool BeginTravel(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    bool BeginCamp(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    void EndCamp(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    void CleanupRuntime(ActiveTravelingEvent& runtime);

    void ApplyProtectedState(Creature* creature) const;
    Creature* GetCreature(uint16 mapId, ObjectGuid guid) const;

    std::unordered_map<uint32, TravelingCampLayoutDefinition> _campLayouts;
    std::unordered_map<uint64, TravelingCampNodeZOverride> _campNodeZOverrides;
    std::unordered_map<uint32, TravelingEventDefinition> _definitions;
    std::unordered_map<uint32, ActiveTravelingEvent> _active;
};

}

#define sTravelingEventMgr lwi::TravelingEventManager::Instance()

#endif
