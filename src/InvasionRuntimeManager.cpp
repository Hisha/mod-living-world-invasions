#include "InvasionRuntimeManager.h"

#include "InvasionScheduler.h"
#include "LivingWorldInvasions.h"
#include "MovementController.h"
#include "RuntimeSignalManager.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"

#include <ctime>
#include <sstream>
#include <vector>

namespace lwi
{
namespace
{
constexpr uint32 UpdateIntervalMs = 1000;

uint64 UnixTimeNow()
{
    return static_cast<uint64>(std::time(nullptr));
}
}

InvasionRuntimeManager& InvasionRuntimeManager::Instance()
{
    static InvasionRuntimeManager instance;
    return instance;
}

void InvasionRuntimeManager::Reset()
{
    sMovementController.Reset();
    sRuntimeSignalMgr.Reset();
    _runtimes.clear();
    _runtimeByInvasion.clear();
    _updateTimerMs = 0;
    _runtimeSequence = 0;
    _initialized = false;
}

void InvasionRuntimeManager::Initialize()
{
    Reset();
    uint64 const now = UnixTimeNow();
    LoadActiveRuntimes(now);
    _updateTimerMs = UpdateIntervalMs;
    _initialized = true;

    LOG_INFO("server.loading", "[LWI Runtime] Runtime manager initialized with {} active runtime(s).",
        _runtimes.size());
}

void InvasionRuntimeManager::Update(uint32 diff)
{
    if (!_initialized)
    {
        return;
    }

    sMovementController.Update(diff);

    if (_updateTimerMs > diff)
    {
        _updateTimerMs -= diff;
        return;
    }

    _updateTimerMs = UpdateIntervalMs;
    uint64 const now = UnixTimeNow();
    std::vector<uint64> completed;

    for (auto& [runtimeId, runtime] : _runtimes)
    {
        if (runtime.Update(now))
        {
            completed.push_back(runtimeId);
        }
        else if (runtime.GetState() == ActiveRuntimeState::Running)
        {
            SaveRuntime(runtime);
        }
    }

    for (uint64 runtimeId : completed)
    {
        CompleteRuntime(runtimeId, now);
    }
}

bool InvasionRuntimeManager::StartInvasion(uint32 invasionId)
{
    if (!_initialized || _runtimeByInvasion.find(invasionId) != _runtimeByInvasion.end())
    {
        return false;
    }

    std::vector<InvasionStageDefinition> const* stages = sInvasionMgr.GetStages(invasionId);
    if (!stages || stages->empty())
    {
        LOG_ERROR("server.loading", "[LWI Runtime] Invasion {} cannot start because it has no enabled stages.", invasionId);
        return false;
    }

    uint64 const now = UnixTimeNow();
    uint64 const runtimeId = GenerateRuntimeId(now);
    InvasionRuntime runtime(runtimeId, invasionId, *stages, now);

    if (!runtime.Start(now))
    {
        return false;
    }

    SaveRuntime(runtime);
    _runtimeByInvasion[invasionId] = runtimeId;
    _runtimes.emplace(runtimeId, std::move(runtime));

    LOG_INFO("server.loading", "[LWI Runtime] Started runtime #{} for invasion {}.", runtimeId, invasionId);
    return true;
}

bool InvasionRuntimeManager::AdvanceRuntime(uint64 runtimeId)
{
    auto iterator = _runtimes.find(runtimeId);
    if (iterator == _runtimes.end())
    {
        return false;
    }

    uint64 const now = UnixTimeNow();
    if (iterator->second.Advance(now))
    {
        CompleteRuntime(runtimeId, now);
        return true;
    }

    SaveRuntime(iterator->second);
    return true;
}

void InvasionRuntimeManager::LoadActiveRuntimes(uint64 now)
{
    if (QueryResult result = CharacterDatabase.Query(
        "SELECT `runtime_id`, `invasion_id`, `current_stage_id`, `stage_started_at`, `stage_ends_at`, `started_at` "
        "FROM `lwi_active_runtime` ORDER BY `runtime_id`"))
    {
        do
        {
            Field* fields = result->Fetch();
            uint64 const runtimeId = fields[0].Get<uint64>();
            uint32 const invasionId = fields[1].Get<uint32>();
            uint32 const currentStageId = fields[2].Get<uint32>();
            uint64 const stageStartedAt = fields[3].Get<uint64>();
            uint64 const stageEndsAt = fields[4].Get<uint64>();
            uint64 const startedAt = fields[5].Get<uint64>();

            std::vector<InvasionStageDefinition> const* stages = sInvasionMgr.GetStages(invasionId);
            if (!stages || stages->empty() || !sInvasionScheduler.IsInvasionActive(invasionId))
            {
                CharacterDatabase.Execute("DELETE FROM `lwi_active_runtime` WHERE `runtime_id` = {}", runtimeId);
                if (sInvasionScheduler.IsInvasionActive(invasionId))
                {
                    sInvasionScheduler.NotifyInvasionStartFailed(invasionId);
                }
                continue;
            }

            InvasionRuntime runtime = InvasionRuntime::Restore(runtimeId, invasionId, *stages,
                currentStageId, stageStartedAt, stageEndsAt, startedAt);

            if (runtime.GetState() == ActiveRuntimeState::Failed)
            {
                CharacterDatabase.Execute("DELETE FROM `lwi_active_runtime` WHERE `runtime_id` = {}", runtimeId);
                sInvasionScheduler.NotifyInvasionStartFailed(invasionId);
                continue;
            }

            _runtimeByInvasion[invasionId] = runtimeId;
            _runtimes.emplace(runtimeId, std::move(runtime));

            LOG_INFO("server.loading", "[LWI Runtime] Restored runtime #{} for invasion {}.", runtimeId, invasionId);
        } while (result->NextRow());
    }

    for (uint32 invasionId : sInvasionScheduler.GetActiveInvasionIds())
    {
        if (_runtimeByInvasion.find(invasionId) == _runtimeByInvasion.end())
        {
            LOG_ERROR("server.loading", "[LWI Runtime] Scheduler marked invasion {} active, but no runtime row exists; resetting it.",
                invasionId);
            sInvasionScheduler.NotifyInvasionStartFailed(invasionId);
        }
    }

    (void)now;
}

void InvasionRuntimeManager::SaveRuntime(InvasionRuntime const& runtime)
{
    InvasionStageDefinition const* stage = runtime.GetCurrentStage();
    if (!stage)
    {
        return;
    }

    CharacterDatabase.Execute(
        "REPLACE INTO `lwi_active_runtime` "
        "(`runtime_id`, `invasion_id`, `current_stage_id`, `stage_started_at`, `stage_ends_at`, `started_at`) "
        "VALUES ({}, {}, {}, {}, {}, {})",
        runtime.GetRuntimeId(), runtime.GetInvasionId(), stage->Id,
        runtime.GetStageStartedAt(), runtime.GetStageEndsAt(), runtime.GetStartedAt());
}

void InvasionRuntimeManager::DeleteRuntime(uint64 runtimeId)
{
    CharacterDatabase.Execute("DELETE FROM `lwi_active_runtime` WHERE `runtime_id` = {}", runtimeId);
}

void InvasionRuntimeManager::CompleteRuntime(uint64 runtimeId, uint64 now)
{
    auto iterator = _runtimes.find(runtimeId);
    if (iterator == _runtimes.end())
    {
        return;
    }

    uint32 const invasionId = iterator->second.GetInvasionId();
    sRuntimeSignalMgr.ClearRuntime(runtimeId);
    DeleteRuntime(runtimeId);
    _runtimeByInvasion.erase(invasionId);
    _runtimes.erase(iterator);
    sInvasionScheduler.NotifyInvasionCompleted(invasionId, now);
}

uint64 InvasionRuntimeManager::GenerateRuntimeId(uint64 now)
{
    ++_runtimeSequence;
    return now * 1000ULL + (_runtimeSequence % 1000U);
}

InvasionRuntime const* InvasionRuntimeManager::GetRuntimeForInvasion(uint32 invasionId) const
{
    auto idIterator = _runtimeByInvasion.find(invasionId);
    return idIterator != _runtimeByInvasion.end() ? GetRuntime(idIterator->second) : nullptr;
}

InvasionRuntime const* InvasionRuntimeManager::GetRuntime(uint64 runtimeId) const
{
    auto iterator = _runtimes.find(runtimeId);
    return iterator != _runtimes.end() ? &iterator->second : nullptr;
}

std::string InvasionRuntimeManager::BuildStatusReport() const
{
    std::ostringstream output;
    uint64 const now = UnixTimeNow();
    output << "\nActive runtimes:\n";

    if (_runtimes.empty())
    {
        output << "  None.\n";
        return output.str();
    }

    for (auto const& [runtimeId, runtime] : _runtimes)
    {
        (void)runtimeId;
        output << "  " << runtime.BuildStatusLine(now) << '\n';
    }

    return output.str();
}
}
