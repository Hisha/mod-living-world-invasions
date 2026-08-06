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
    _responseOrigins.clear();
    _definitions.clear();
}

void InvasionMgr::LoadDefinitions()
{
    Clear();

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `map_id`, `team`, `max_active_default`, `enabled` "
        "FROM `lwi_response_origin` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();

            ResponseOriginDefinition origin;
            origin.Id = fields[0].Get<uint32>();
            origin.Name = fields[1].Get<std::string>();
            origin.MapId = fields[2].Get<uint16>();
            origin.Team = fields[3].Get<uint8>();
            origin.MaxActiveDefault = fields[4].Get<uint32>();
            origin.Enabled = fields[5].Get<bool>();

            auto [iterator, inserted] = _responseOrigins.emplace(origin.Id, std::move(origin));
            if (!inserted)
            {
                LOG_ERROR("server.loading", "Living World Invasions: duplicate response origin id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "Living World Invasions: loaded {} enabled response origin definition(s).", _responseOrigins.size());

    if (QueryResult result = WorldDatabase.Query(
        "SELECT `id`, `name`, `map_id`, `zone_id`, `team`, `response_origin_id`, "
        "`recommended_min_level`, `recommended_max_level`, `selection_weight`, "
        "`minimum_cooldown_seconds`, `maximum_cooldown_seconds`, `allow_random_start`, `enabled` "
        "FROM `lwi_invasion` WHERE `enabled` = 1 ORDER BY `id`"))
    {
        do
        {
            Field* fields = result->Fetch();

            InvasionDefinition definition;
            definition.Id = fields[0].Get<uint32>();
            definition.Name = fields[1].Get<std::string>();
            definition.MapId = fields[2].Get<uint16>();
            definition.ZoneId = fields[3].Get<uint32>();
            definition.Team = fields[4].Get<uint8>();
            definition.ResponseOriginId = fields[5].Get<uint32>();
            definition.RecommendedMinLevel = fields[6].Get<uint8>();
            definition.RecommendedMaxLevel = fields[7].Get<uint8>();
            definition.SelectionWeight = fields[8].Get<uint32>();
            definition.MinimumCooldownSeconds = fields[9].Get<uint32>();
            definition.MaximumCooldownSeconds = fields[10].Get<uint32>();
            definition.AllowRandomStart = fields[11].Get<bool>();
            definition.Enabled = fields[12].Get<bool>();

            ResponseOriginDefinition const* origin = GetResponseOrigin(definition.ResponseOriginId);
            if (!origin)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: invasion {} ({}) references missing or disabled response origin {} and was ignored.",
                    definition.Id, definition.Name, definition.ResponseOriginId);
                continue;
            }

            if (origin->MapId != definition.MapId)
            {
                LOG_ERROR("server.loading",
                    "Living World Invasions: invasion {} ({}) uses map {}, but response origin {} ({}) uses map {}; invasion ignored.",
                    definition.Id, definition.Name, definition.MapId, origin->Id, origin->Name, origin->MapId);
                continue;
            }

            if (definition.RecommendedMinLevel > definition.RecommendedMaxLevel)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has an invalid level range and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            if (definition.MinimumCooldownSeconds > definition.MaximumCooldownSeconds)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has minimum cooldown greater than maximum cooldown and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            if (definition.SelectionWeight == 0)
            {
                LOG_ERROR("server.loading", "Living World Invasions: invasion {} ({}) has selection weight 0 and was ignored.",
                    definition.Id, definition.Name);
                continue;
            }

            auto [iterator, inserted] = _definitions.emplace(definition.Id, std::move(definition));
            if (!inserted)
            {
                LOG_ERROR("server.loading", "Living World Invasions: duplicate invasion id {} ignored.", iterator->first);
            }
        } while (result->NextRow());
    }

    LOG_INFO("server.loading", "Living World Invasions: loaded {} enabled invasion definition(s).", _definitions.size());
}

ResponseOriginDefinition const* InvasionMgr::GetResponseOrigin(uint32 responseOriginId) const
{
    auto const iterator = _responseOrigins.find(responseOriginId);
    return iterator != _responseOrigins.end() ? &iterator->second : nullptr;
}

InvasionDefinition const* InvasionMgr::GetDefinition(uint32 invasionId) const
{
    auto const iterator = _definitions.find(invasionId);
    return iterator != _definitions.end() ? &iterator->second : nullptr;
}

std::unordered_map<uint32, ResponseOriginDefinition> const& InvasionMgr::GetResponseOrigins() const
{
    return _responseOrigins;
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
