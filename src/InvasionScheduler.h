#ifndef MOD_LIVING_WORLD_INVASIONS_SCHEDULER_H
#define MOD_LIVING_WORLD_INVASIONS_SCHEDULER_H

#include "Define.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace lwi
{
enum class InvasionRuntimeState : uint8
{
    Available = 0,
    Active = 1,
    Cooldown = 2
};

struct InvasionRuntime
{
    uint32 InvasionId = 0;
    InvasionRuntimeState State = InvasionRuntimeState::Available;
    uint64 LastStartedAt = 0;
    uint64 LastCompletedAt = 0;
    uint64 NextEligibleAt = 0;
    uint64 ActiveSince = 0;
    uint64 ActiveUntil = 0;
    uint32 TimesStarted = 0;
    uint32 TimesCompleted = 0;
};

struct SchedulerSettings
{
    bool Enabled = true;
    bool Debug = false;
    uint32 CheckIntervalSeconds = 5;
    uint32 InitialDelayMinSeconds = 20;
    uint32 InitialDelayMaxSeconds = 40;
    uint32 NextDelayMinSeconds = 30;
    uint32 NextDelayMaxSeconds = 60;
    uint32 TestActiveDurationSeconds = 45;
    uint32 MaxActiveGlobal = 3;
    uint32 DefaultMaxActivePerMap = 2;
    uint32 DefaultMaxActivePerResponseOrigin = 1;
};

class InvasionScheduler
{
public:
    static InvasionScheduler& Instance();

    void Configure(SchedulerSettings settings);
    void Initialize();
    void Update(uint32 diff);
    void Reset();
	
	[[nodiscard]] std::string BuildStatusReport() const;

private:
    InvasionScheduler() = default;

    void LoadRuntimeState();
    void EnsureRuntimeRows();
    void RecoverExpiredActiveInvasions(uint64 now);
    void CompleteExpiredInvasions(uint64 now);
    void EvaluateDueMaps(uint64 now);
    void EvaluateMap(uint16 mapId, uint64 now);
    void ScheduleMap(uint16 mapId, uint64 now, bool initial);
    void StartTestInvasion(uint32 invasionId, uint64 now);
    void CompleteTestInvasion(uint32 invasionId, uint64 now);
    void SaveRuntime(InvasionRuntime const& runtime);

    [[nodiscard]] uint32 GetMapLimit(uint16 mapId) const;
    [[nodiscard]] uint32 GetResponseOriginLimit(uint32 responseOriginId, uint32 sqlDefault) const;
    [[nodiscard]] uint32 CountActiveGlobal() const;
    [[nodiscard]] uint32 CountActiveOnMap(uint16 mapId) const;
    [[nodiscard]] uint32 CountActiveForResponseOrigin(uint32 responseOriginId) const;

    SchedulerSettings _settings;
    std::unordered_map<uint32, InvasionRuntime> _runtime;
    std::unordered_map<uint16, uint64> _nextMapEvaluation;
    uint32 _updateTimerMs = 0;
    bool _initialized = false;
};
}

#define sInvasionScheduler lwi::InvasionScheduler::Instance()

#endif
