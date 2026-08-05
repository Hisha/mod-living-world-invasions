#ifndef MOD_LIVING_WORLD_INVASIONS_H
#define MOD_LIVING_WORLD_INVASIONS_H

#include "Define.h"

#include <string>
#include <unordered_map>

namespace lwi
{
struct InvasionDefinition
{
    uint32 Id = 0;
    std::string Name;
    uint32 ZoneId = 0;
    uint8 Team = 0;
    uint8 RecommendedMinLevel = 1;
    uint8 RecommendedMaxLevel = 80;
    uint32 CooldownSeconds = 0;
    bool Enabled = false;
};

class InvasionMgr
{
public:
    static InvasionMgr& Instance();

    void LoadDefinitions();
    void Clear();

    [[nodiscard]] InvasionDefinition const* GetDefinition(uint32 invasionId) const;
    [[nodiscard]] std::unordered_map<uint32, InvasionDefinition> const& GetDefinitions() const;
    [[nodiscard]] std::size_t GetDefinitionCount() const;

private:
    InvasionMgr() = default;

    std::unordered_map<uint32, InvasionDefinition> _definitions;
};
}

#define sInvasionMgr lwi::InvasionMgr::Instance()

#endif
