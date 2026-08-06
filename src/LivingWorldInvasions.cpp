#include "LivingWorldInvasions.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "QueryResult.h"


namespace lwi
{
InvasionMgr& InvasionMgr::Instance()
{
    static InvasionMgr instance;
    return instance;
}

void InvasionMgr::Clear()
{
    _definitions.clear();
}

void InvasionMgr::LoadDefinitions()
{
    Clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `zone_id`, `team`, `recommended_min_level`, "
        "`recommended_max_level`, `cooldown_seconds`, `enabled` "
        "FROM `lwi_invasion` WHERE `enabled` = 1 ORDER BY `id`");

    if (!result)
    {
        LOG_INFO("module", "Living World Invasions: loaded 0 enabled invasion definitions.");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        InvasionDefinition definition;
        definition.Id = fields[0].Get<uint32>();
        definition.Name = fields[1].Get<std::string>();
        definition.ZoneId = fields[2].Get<uint32>();
        definition.Team = fields[3].Get<uint8>();
        definition.RecommendedMinLevel = fields[4].Get<uint8>();
        definition.RecommendedMaxLevel = fields[5].Get<uint8>();
        definition.CooldownSeconds = fields[6].Get<uint32>();
        definition.Enabled = fields[7].Get<bool>();

        auto [iterator, inserted] = _definitions.emplace(definition.Id, std::move(definition));
        if (!inserted)
        {
            LOG_ERROR("module", "Living World Invasions: duplicate invasion id {} ignored.", iterator->first);
        }
    } while (result->NextRow());

    LOG_INFO("module", "Living World Invasions: loaded {} enabled invasion definition(s).", _definitions.size());
}

InvasionDefinition const* InvasionMgr::GetDefinition(uint32 invasionId) const
{
    auto const iterator = _definitions.find(invasionId);
    return iterator != _definitions.end() ? &iterator->second : nullptr;
}

std::unordered_map<uint32, InvasionDefinition> const& InvasionMgr::GetDefinitions() const
{
    return _definitions;
}

std::size_t InvasionMgr::GetDefinitionCount() const
{
    return _definitions.size();
}
}
