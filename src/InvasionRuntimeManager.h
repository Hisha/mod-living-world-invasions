#ifndef MOD_LIVING_WORLD_INVASIONS_RUNTIME_MANAGER_H
#define MOD_LIVING_WORLD_INVASIONS_RUNTIME_MANAGER_H

#include "Define.h"
#include "InvasionRuntime.h"

#include <string>
#include <unordered_map>

namespace lwi
{
class InvasionRuntimeManager
{
public:
    static InvasionRuntimeManager& Instance();

    void Initialize();
    void Reset();
    void Update(uint32 diff);

    bool StartInvasion(uint32 invasionId);
    bool AdvanceRuntime(uint64 runtimeId);

    [[nodiscard]] InvasionRuntime const* GetRuntimeForInvasion(uint32 invasionId) const;
    [[nodiscard]] InvasionRuntime const* GetRuntime(uint64 runtimeId) const;
    [[nodiscard]] std::string BuildStatusReport() const;

private:
    InvasionRuntimeManager() = default;

    void LoadActiveRuntimes(uint64 now);
    void SaveRuntime(InvasionRuntime const& runtime);
    void DeleteRuntime(uint64 runtimeId);
    void CompleteRuntime(uint64 runtimeId, uint64 now);
    uint64 GenerateRuntimeId(uint64 now);

    std::unordered_map<uint64, InvasionRuntime> _runtimes;
    std::unordered_map<uint32, uint64> _runtimeByInvasion;
    uint32 _updateTimerMs = 0;
    uint32 _runtimeSequence = 0;
    bool _initialized = false;
};
}

#define sInvasionRuntimeMgr lwi::InvasionRuntimeManager::Instance()

#endif
