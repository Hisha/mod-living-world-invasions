#ifndef MOD_LIVING_WORLD_INVASIONS_RUNTIME_ENTITY_GROUP_H
#define MOD_LIVING_WORLD_INVASIONS_RUNTIME_ENTITY_GROUP_H

#include "Define.h"
#include "IEntityProvider.h"

#include <unordered_map>
#include <vector>

namespace lwi
{
enum class RuntimeEntityGroupState : uint8
{
    Active = 0,
    CleaningUp = 1,
    Completed = 2
};

struct RuntimeEntityGroup
{
    uint64 Id = 0;
    uint64 RuntimeId = 0;
    uint32 SpawnGroupId = 0;
    uint32 ActiveBehaviorId = 0;
    RuntimeEntityGroupState State = RuntimeEntityGroupState::Active;
    std::vector<RuntimeEntity> Entities;
};

class RuntimeEntityGroupManager
{
public:
    static RuntimeEntityGroupManager& Instance();

    void Reset();

    RuntimeEntityGroup& CreateGroup(uint64 runtimeId, uint32 spawnGroupId);
    RuntimeEntityGroup* GetGroup(uint64 runtimeGroupId);
    RuntimeEntityGroup const* GetGroup(uint64 runtimeGroupId) const;

    std::vector<uint64> GetGroupsForRuntime(uint64 runtimeId) const;
    std::vector<uint64> GetAllGroupIds() const;
    RuntimeEntityGroup* FindLatestGroup(uint64 runtimeId, uint32 spawnGroupId);

    bool RemoveGroup(uint64 runtimeGroupId);
    void RemoveRuntime(uint64 runtimeId);

private:
    uint64 NextGroupId();

    uint64 _nextGroupId = 1;
    std::unordered_map<uint64, RuntimeEntityGroup> _groups;
    std::unordered_map<uint64, std::vector<uint64>> _groupsByRuntime;
};
}

#define sRuntimeEntityGroupMgr lwi::RuntimeEntityGroupManager::Instance()

#endif
