#ifndef MOD_LIVING_WORLD_INVASIONS_H
#define MOD_LIVING_WORLD_INVASIONS_H

#include "Define.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace lwi
{
struct ResponseOriginDefinition
{
    uint32 Id = 0;
    std::string Name;
    uint16 MapId = 0;
    uint8 Team = 0;
    uint32 MaxActiveDefault = 1;
    bool Enabled = false;
};


struct StageActionDefinition
{
    uint32 Id = 0;
    uint32 StageId = 0;
    uint16 ActionOrder = 0;
    uint8 ActionType = 1;
    uint32 TargetId = 0;
    uint32 Parameter1 = 0;
    uint32 Parameter2 = 0;
    uint32 DelaySeconds = 0;
    bool Enabled = false;
};

struct SpawnMemberDefinition
{
    uint32 Id = 0;
    uint32 SpawnGroupId = 0;
    uint8 EntityType = 1;
    uint32 EntityEntry = 0;
    uint16 Count = 1;
    uint16 LevelOverride = 0;
    std::string Comment;
};

struct SpawnGroupDefinition
{
    uint32 Id = 0;
    std::string Name;
    uint16 MapId = 0;
    float X = 0;
    float Y = 0;
    float Z = 0;
    float Orientation = 0;
    float SpawnRadius = 5;
    bool Enabled = false;
};


struct MovementPathDefinition
{
    uint32 Id = 0;
    std::string Name;
    bool Enabled = false;
    std::string Comment;
};

struct MovementNodeDefinition
{
    uint32 Id = 0;
    uint32 PathId = 0;
    uint16 NodeOrder = 0;
    uint16 MapId = 0;
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float Orientation = 0.0f;
    uint32 WaitMs = 0;
    uint32 ProfileOverrideId = 0;
    bool Enabled = false;
    std::string Comment;
};

struct MovementProfileDefinition
{
    uint32 Id = 0;
    std::string Name;
    uint8 DefaultMode = 0;
    float WalkSpeedMultiplier = 1.0f;
    float RunSpeedMultiplier = 1.0f;
    bool StealthEnabled = false;
    bool Enabled = false;
    std::string Comment;
};

struct InvasionStageDefinition
{
    uint32 Id = 0;
    uint32 InvasionId = 0;
    uint16 StageOrder = 0;
    std::string Name;
    uint32 DurationSeconds = 30;
    uint8 CompletionType = 0;
    bool Enabled = false;
};

struct InvasionDefinition
{
    uint32 Id = 0;
    std::string Name;
    uint16 MapId = 0;
    uint32 ZoneId = 0;
    uint8 Team = 0;
    uint32 ResponseOriginId = 0;
    uint8 RecommendedMinLevel = 1;
    uint8 RecommendedMaxLevel = 80;
    uint32 SelectionWeight = 100;
    uint32 MinimumCooldownSeconds = 86400;
    uint32 MaximumCooldownSeconds = 604800;
    bool AllowRandomStart = true;
    bool Enabled = false;
};

class InvasionMgr
{
public:
    static InvasionMgr& Instance();

    void LoadDefinitions();
    void Clear();

    [[nodiscard]] ResponseOriginDefinition const* GetResponseOrigin(uint32 responseOriginId) const;
    [[nodiscard]] InvasionDefinition const* GetDefinition(uint32 invasionId) const;
    [[nodiscard]] std::unordered_map<uint32, ResponseOriginDefinition> const& GetResponseOrigins() const;
    [[nodiscard]] std::unordered_map<uint32, InvasionDefinition> const& GetDefinitions() const;
    [[nodiscard]] std::vector<InvasionStageDefinition> const* GetStages(uint32 invasionId) const;
    [[nodiscard]] std::vector<StageActionDefinition> const* GetActions(uint32 stageId) const;
    [[nodiscard]] SpawnGroupDefinition const* GetSpawnGroup(uint32 id) const;
    [[nodiscard]] std::vector<SpawnMemberDefinition> const* GetSpawnMembers(uint32 id) const;
    [[nodiscard]] MovementPathDefinition const* GetMovementPath(uint32 id) const;
    [[nodiscard]] std::vector<MovementNodeDefinition> const* GetMovementNodes(uint32 pathId) const;
    [[nodiscard]] MovementProfileDefinition const* GetMovementProfile(uint32 id) const;
    [[nodiscard]] std::size_t GetDefinitionCount() const;

private:
    InvasionMgr() = default;

    std::unordered_map<uint32, ResponseOriginDefinition> _responseOrigins;
    std::unordered_map<uint32, InvasionDefinition> _definitions;
    std::unordered_map<uint32, std::vector<InvasionStageDefinition>> _stagesByInvasion;
    std::unordered_map<uint32, std::vector<StageActionDefinition>> _actionsByStage;
    std::unordered_map<uint32, SpawnGroupDefinition> _spawnGroups;
    std::unordered_map<uint32, std::vector<SpawnMemberDefinition>> _spawnMembersByGroup;
    std::unordered_map<uint32, MovementPathDefinition> _movementPaths;
    std::unordered_map<uint32, std::vector<MovementNodeDefinition>> _movementNodesByPath;
    std::unordered_map<uint32, MovementProfileDefinition> _movementProfiles;
};
}

#define sInvasionMgr lwi::InvasionMgr::Instance()

#endif
