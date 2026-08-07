#include "InvasionRuntime.h"

#include "Log.h"
#include "InvasionSpawnManager.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace lwi
{
namespace
{
constexpr uint8 TimerCompletionType = 0;
}

InvasionRuntime::InvasionRuntime(uint64 runtimeId, uint32 invasionId,
    std::vector<InvasionStageDefinition> stages, uint64 startedAt)
    : _runtimeId(runtimeId),
      _invasionId(invasionId),
      _stages(std::move(stages)),
      _startedAt(startedAt)
{
    std::sort(_stages.begin(), _stages.end(), [](InvasionStageDefinition const& left,
        InvasionStageDefinition const& right)
    {
        return left.StageOrder < right.StageOrder;
    });
}

InvasionRuntime InvasionRuntime::Restore(uint64 runtimeId, uint32 invasionId,
    std::vector<InvasionStageDefinition> stages, uint32 currentStageId,
    uint64 stageStartedAt, uint64 stageEndsAt, uint64 startedAt)
{
    InvasionRuntime runtime(runtimeId, invasionId, std::move(stages), startedAt);
    std::size_t const index = runtime.FindStageIndex(currentStageId);

    if (index >= runtime._stages.size())
    {
        runtime._state = ActiveRuntimeState::Failed;
        return runtime;
    }

    runtime._currentStageIndex = index;
    runtime._stageStartedAt = stageStartedAt;
    runtime._stageEndsAt = stageEndsAt;
    runtime._state = ActiveRuntimeState::Running;
    return runtime;
}

bool InvasionRuntime::Start(uint64 now)
{
    if (_stages.empty())
    {
        _state = ActiveRuntimeState::Failed;
        LOG_ERROR("server.loading", "[LWI Runtime] Runtime #{} for invasion {} has no stages.",
            _runtimeId, _invasionId);
        return false;
    }

    _currentStageIndex = 0;
    _startedAt = now;
    _state = ActiveRuntimeState::Running;
    return BeginCurrentStage(now);
}

bool InvasionRuntime::Update(uint64 now)
{
    if (_state != ActiveRuntimeState::Running)
    {
        return _state == ActiveRuntimeState::Completed;
    }

    InvasionStageDefinition const* stage = GetCurrentStage();
    if (!stage)
    {
        _state = ActiveRuntimeState::Failed;
        return false;
    }

    if (stage->CompletionType != TimerCompletionType)
    {
        return false;
    }

    if (now < _stageEndsAt)
    {
        return false;
    }

    return Advance(now);
}

bool InvasionRuntime::Advance(uint64 now)
{
    if (_state != ActiveRuntimeState::Running)
    {
        return false;
    }

    ++_currentStageIndex;
    if (_currentStageIndex >= _stages.size())
    {
        _state = ActiveRuntimeState::Completed;
        _stageStartedAt = 0;
        _stageEndsAt = 0;

        LOG_INFO("server.loading", "[LWI Runtime] Runtime #{} for invasion {} completed all stages.",
            _runtimeId, _invasionId);
        return true;
    }

    return BeginCurrentStage(now) && false;
}

bool InvasionRuntime::BeginCurrentStage(uint64 now)
{
    InvasionStageDefinition const* stage = GetCurrentStage();
    if (!stage)
    {
        _state = ActiveRuntimeState::Failed;
        return false;
    }

    _stageStartedAt = now;
    _stageEndsAt = now + std::max<uint32>(1, stage->DurationSeconds);

    if (auto const* actions = sInvasionMgr.GetActions(stage->Id))
    {
        for (auto const& action : *actions)
        {
            if (action.ActionType == 1)
            {
                sInvasionSpawnMgr.SpawnGroup(_runtimeId, action.TargetId);
            }
        }
    }

    LOG_INFO("server.loading",
        "[LWI Runtime] Runtime #{} invasion {} entered stage {} ({}) for {} second(s).",
        _runtimeId, _invasionId, stage->StageOrder, stage->Name,
        std::max<uint32>(1, stage->DurationSeconds));

    return true;
}

std::size_t InvasionRuntime::FindStageIndex(uint32 stageId) const
{
    for (std::size_t index = 0; index < _stages.size(); ++index)
    {
        if (_stages[index].Id == stageId)
        {
            return index;
        }
    }

    return _stages.size();
}

uint64 InvasionRuntime::GetRuntimeId() const { return _runtimeId; }
uint32 InvasionRuntime::GetInvasionId() const { return _invasionId; }
ActiveRuntimeState InvasionRuntime::GetState() const { return _state; }

InvasionStageDefinition const* InvasionRuntime::GetCurrentStage() const
{
    return _currentStageIndex < _stages.size() ? &_stages[_currentStageIndex] : nullptr;
}

uint64 InvasionRuntime::GetStageStartedAt() const { return _stageStartedAt; }
uint64 InvasionRuntime::GetStageEndsAt() const { return _stageEndsAt; }
uint64 InvasionRuntime::GetStartedAt() const { return _startedAt; }

uint64 InvasionRuntime::GetStageTimeRemaining(uint64 now) const
{
    return _stageEndsAt > now ? _stageEndsAt - now : 0;
}

std::string InvasionRuntime::BuildStatusLine(uint64 now) const
{
    std::ostringstream output;
    output << "Runtime #" << _runtimeId;

    if (InvasionStageDefinition const* stage = GetCurrentStage())
    {
        output << ", stage " << stage->StageOrder << " (" << stage->Name << ")"
               << ", " << GetStageTimeRemaining(now) << " second(s) remaining";
    }
    else
    {
        output << ", no active stage";
    }

    return output.str();
}
}
