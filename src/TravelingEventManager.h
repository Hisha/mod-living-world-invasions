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

struct TravelingStopDefinition
{
    uint32 Id = 0;
    uint32 EventId = 0;
    uint32 StopOrder = 0;
    uint32 RouteNodeId = 0;
    uint32 DwellSeconds = 120;
    std::string ArrivalText;
    std::string DepartureText;
};

struct TravelingPropDefinition
{
    uint32 Id = 0;
    uint32 EventId = 0;
    uint32 GameObjectEntry = 0;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    float OffsetZ = 0.0f;
    float OrientationOffset = 0.0f;
};

struct TravelingEventDefinition
{
    uint32 Id = 0;
    std::string Name;

    // Route movement is owned by a normal Creature.  The visible wagon is a
    // GameObject that is relocated behind that leader while traveling.
    uint32 LeaderEntry = 0;
    uint32 WagonGameObjectEntry = 0;

    // Merchant is optional during the mobile-wagon proof-of-concept.  A zero
    // entry means "wagon movement test only".
    uint32 MerchantEntry = 0;

    float WagonDistanceBehind = 4.5f;
    float WagonLateralOffset = 0.0f;
    float WagonVerticalOffset = 0.0f;

    bool Enabled = false;
    std::vector<TravelingStopDefinition> Stops;
    std::vector<TravelingPropDefinition> Props;
};

struct ActiveTravelingEvent
{
    uint32 EventId = 0;
    uint64 RuntimeGroupId = 0;
    uint32 StopIndex = 0;
    TravelingEventState State = TravelingEventState::Camped;
    uint32 StateTimerMs = 0;
    uint32 WagonUpdateTimerMs = 0;

    ObjectGuid LeaderGuid;
    ObjectGuid WagonGuid;
    ObjectGuid MerchantGuid;

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

    bool Start(uint32 eventId, std::string* error = nullptr);
    bool Stop(uint32 eventId, std::string* error = nullptr);

    std::string BuildStatusReport() const;

private:
    TravelingEventDefinition const* GetDefinition(uint32 eventId) const;
    bool SpawnRuntime(TravelingEventDefinition const& definition, ActiveTravelingEvent& runtime, std::string* error);
    bool BeginTravel(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    bool BeginCamp(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    void EndCamp(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    void CleanupRuntime(ActiveTravelingEvent& runtime);

    bool UpdateMobileWagon(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);
    bool PlaceMobileWagon(ActiveTravelingEvent& runtime, TravelingEventDefinition const& definition);

    void ApplyProtectedState(Creature* creature) const;
    Creature* GetCreature(uint16 mapId, ObjectGuid guid) const;
    GameObject* GetGameObject(uint16 mapId, ObjectGuid guid) const;

    std::unordered_map<uint32, TravelingEventDefinition> _definitions;
    std::unordered_map<uint32, ActiveTravelingEvent> _active;
};

}

#define sTravelingEventMgr lwi::TravelingEventManager::Instance()

#endif
