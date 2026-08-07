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
    [[nodiscard]] std::size_t GetDefinitionCount() const;

private:
    InvasionMgr() = default;

    std::unordered_map<uint32, ResponseOriginDefinition> _responseOrigins;
    std::unordered_map<uint32, InvasionDefinition> _definitions;
    std::unordered_map<uint32, std::vector<InvasionStageDefinition>> _stagesByInvasion;
};
}

#define sInvasionMgr lwi::InvasionMgr::Instance()

#endif
